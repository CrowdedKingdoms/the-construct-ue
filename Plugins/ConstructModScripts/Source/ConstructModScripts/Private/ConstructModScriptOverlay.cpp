#include "ConstructModScriptOverlay.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    const FName ColorParameter(TEXT("Color"));
    const FName OpacityParameter(TEXT("Opacity"));

    bool HasPresentationParameters(const UMaterialInterface* Material)
    {
        if (!Material)
        {
            return false;
        }
        TArray<FMaterialParameterInfo> Vectors;
        TArray<FMaterialParameterInfo> Scalars;
        TArray<FGuid> Ids;
        Material->GetAllVectorParameterInfo(Vectors, Ids);
        Material->GetAllScalarParameterInfo(Scalars, Ids);
        return Vectors.ContainsByPredicate([](const FMaterialParameterInfo& Info)
        {
            return Info.Name == ColorParameter;
        }) && Scalars.ContainsByPredicate([](const FMaterialParameterInfo& Info)
        {
            return Info.Name == OpacityParameter;
        });
    }

    FVector MeshNormalization(const UStaticMesh& Mesh)
    {
        const FVector Size = Mesh.GetBounds().BoxExtent * 2.0;
        // A plane has no thickness; its Z scale remains available to the caller.
        return FVector(100.0 / Size.X, 100.0 / Size.Y, Size.Z > UE_SMALL_NUMBER ? 100.0 / Size.Z : 1.0);
    }
}

UConstructModScriptOverlay::UConstructModScriptOverlay()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Capsule(TEXT("/Game/Construct/ModScripts/Meshes/SM_ConstructScriptOverlayCapsule.SM_ConstructScriptOverlayCapsule"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Lit(TEXT("/Game/Construct/ModScripts/Materials/M_ConstructScriptOverlayLit.M_ConstructScriptOverlayLit"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Unlit(TEXT("/Game/Construct/ModScripts/Materials/M_ConstructScriptOverlayUnlit.M_ConstructScriptOverlayUnlit"));
    Meshes = {Box.Object, Sphere.Object, Cylinder.Object, Capsule.Object, Plane.Object};
    LitMaterial = Lit.Object;
    UnlitMaterial = Unlit.Object;
    bLitMaterialReady = HasPresentationParameters(LitMaterial);
    bUnlitMaterialReady = HasPresentationParameters(UnlitMaterial);
}

bool UConstructModScriptOverlay::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::EditorPreview;
}

UStaticMesh* UConstructModScriptOverlay::MeshFor(EConstructOverlayPrimitive Primitive) const
{
    const int32 Index = static_cast<int32>(Primitive);
    return Meshes.IsValidIndex(Index) ? Meshes[Index].Get() : nullptr;
}

bool UConstructModScriptOverlay::Validate(const TArray<FConstructOverlayElement>& Elements) const
{
    if (Elements.Num() > MaxElementsPerSource)
    {
        return false;
    }
    TSet<FString> Ids;
    for (const FConstructOverlayElement& Element : Elements)
    {
        const UStaticMesh* Mesh = MeshFor(Element.Primitive);
        const FVector Scale = Element.Transform.GetScale3D();
        const FLinearColor& Color = Element.Color;
        if (Element.Id.IsEmpty() || Element.Id.Len() > 32 || Ids.Contains(Element.Id) || !Mesh
            || Element.Transform.ContainsNaN() || !Element.Transform.GetRotation().IsNormalized()
            || Scale.X <= 0.0 || Scale.Y <= 0.0 || Scale.Z <= 0.0
            || !FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)
            || Color.R < 0.0f || Color.R > 1.0f || Color.G < 0.0f || Color.G > 1.0f || Color.B < 0.0f || Color.B > 1.0f
            || !FMath::IsFinite(Element.Opacity) || Element.Opacity < 0.0f || Element.Opacity > 1.0f)
        {
            return false;
        }
        const FVector Bounds = Mesh->GetBounds().BoxExtent;
        if (Bounds.X <= UE_SMALL_NUMBER || Bounds.Y <= UE_SMALL_NUMBER || (Element.Primitive != EConstructOverlayPrimitive::Plane && Bounds.Z <= UE_SMALL_NUMBER)
            || !(Element.bUnlit ? bUnlitMaterialReady : bLitMaterialReady))
        {
            return false;
        }
        Ids.Add(Element.Id);
    }
    return true;
}

bool UConstructModScriptOverlay::Replace(const FString& Source, int64 Generation, const TArray<FConstructOverlayElement>& Elements)
{
    if (!IsInGameThread() || bDeinitialized || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer || Source.IsEmpty() || Generation < 0)
    {
        return false;
    }
    FConstructOverlaySource* Existing = Sources.Find(Source);
    if (Existing && (Generation < Existing->Generation || (Existing->bStopped && Generation == Existing->Generation)))
    {
        return false;
    }
    if (!Validate(Elements))
    {
        return false;
    }
    const bool bHadComponents = Existing && !Existing->Components.IsEmpty();
    if (!Elements.IsEmpty() && !bHadComponents && ActiveSourceCount >= MaxSources)
    {
        return false;
    }

    if (Elements.IsEmpty())
    {
        FConstructOverlaySource& State = Sources.FindOrAdd(Source);
        DestroyPresentation(State);
        ActiveSourceCount -= bHadComponents ? 1 : 0;
        State.Generation = Generation;
        State.bStopped = false;
        return true;
    }

    // Stage every allocation before touching the retained presentation. Staged components
    // stay invisible until commit, including when they belong to a retained actor.
    FConstructOverlaySource Staged;
    AActor* Actor = Existing && IsValid(Existing->Actor) && !Existing->Actor->IsActorBeingDestroyed() ? Existing->Actor.Get() : nullptr;
    if (!Actor)
    {
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags = RF_Transient;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Staged.Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
        if (!Staged.Actor)
        {
            return false;
        }
        Actor = Staged.Actor.Get();
        Actor->SetReplicates(false);
        Actor->SetActorEnableCollision(false);
        Actor->SetActorHiddenInGame(true);
        USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None, RF_Transient);
        Actor->AddInstanceComponent(Root);
        Actor->SetRootComponent(Root);
        Root->SetMobility(EComponentMobility::Movable);
        Root->RegisterComponent();
        if (!Root->IsRegistered())
        {
            DestroyPresentation(Staged);
            return false;
        }
    }

    TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> ChangedMaterials;
    TSet<FString> DesiredIds;
    for (const FConstructOverlayElement& Element : Elements)
    {
        DesiredIds.Add(Element.Id);
        const TObjectPtr<UStaticMeshComponent>* Retained = Existing ? Existing->Components.Find(Element.Id) : nullptr;
        UStaticMeshComponent* Component = Retained && IsValid(Retained->Get()) && (*Retained)->GetOwner() == Actor && (*Retained)->IsRegistered() ? Retained->Get() : nullptr;
        UMaterialInterface* BaseMaterial = Element.bUnlit ? UnlitMaterial.Get() : LitMaterial.Get();
        if (!Component)
        {
            Component = NewObject<UStaticMeshComponent>(Actor, NAME_None, RF_Transient);
            UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, Component);
            if (!Material)
            {
                Component->DestroyComponent();
                DestroyPresentation(Staged);
                return false;
            }
            Actor->AddInstanceComponent(Component);
            Component->SetupAttachment(Actor->GetRootComponent());
            Component->SetMobility(EComponentMobility::Movable);
            Component->SetStaticMesh(MeshFor(Element.Primitive));
            Component->SetMaterial(0, Material);
            Component->bDisallowNanite = true;
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCollisionResponseToAllChannels(ECR_Ignore);
            Component->SetGenerateOverlapEvents(false);
            Component->SetCanEverAffectNavigation(false);
            Component->SetCastShadow(false);
            Component->SetReceivesDecals(false);
            Component->SetVisibleInRayTracing(false);
            Component->SetVisibility(false);
            Component->RegisterComponent();
            if (!Component->IsRegistered())
            {
                Component->DestroyComponent();
                DestroyPresentation(Staged);
                return false;
            }
            Staged.Components.Add(Element.Id, Component);
        }
        else
        {
            UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
            if (!Material || Material->Parent != BaseMaterial)
            {
                Material = UMaterialInstanceDynamic::Create(BaseMaterial, Component);
                if (!Material)
                {
                    DestroyPresentation(Staged);
                    return false;
                }
                ChangedMaterials.Add(Element.Id, Material);
            }
        }
    }

    FConstructOverlaySource& State = Sources.FindOrAdd(Source);
    for (auto It = State.Components.CreateIterator(); It; ++It)
    {
        if (!DesiredIds.Contains(It.Key()) || !IsValid(It.Value()) || It.Value()->GetOwner() != Actor || !It.Value()->IsRegistered())
        {
            if (IsValid(It.Value()))
            {
                It.Value()->DestroyComponent();
            }
            It.RemoveCurrent();
        }
    }
    for (const TPair<FString, TObjectPtr<UStaticMeshComponent>>& Pair : Staged.Components)
    {
        State.Components.Add(Pair.Key, Pair.Value);
    }
    State.Actor = Actor;
    ActiveSourceCount += bHadComponents ? 0 : 1;
    State.Generation = Generation;
    State.bStopped = false;
    for (const FConstructOverlayElement& Element : Elements)
    {
        UStaticMeshComponent* Component = State.Components.FindChecked(Element.Id);
        if (const TObjectPtr<UMaterialInstanceDynamic>* Changed = ChangedMaterials.Find(Element.Id))
        {
            Component->SetMaterial(0, Changed->Get());
        }
        UMaterialInstanceDynamic* Material = CastChecked<UMaterialInstanceDynamic>(Component->GetMaterial(0));
        Material->SetVectorParameterValue(ColorParameter, FLinearColor(Element.Color.R, Element.Color.G, Element.Color.B, 1.0f));
        Material->SetScalarParameterValue(OpacityParameter, Element.Opacity);
        UStaticMesh* Mesh = MeshFor(Element.Primitive);
        if (Component->GetStaticMesh() != Mesh)
        {
            Component->SetStaticMesh(Mesh);
        }
        FTransform DrawTransform = Element.Transform;
        DrawTransform.SetScale3D(Element.Transform.GetScale3D() * MeshNormalization(*Mesh));
        Component->SetWorldTransform(DrawTransform);
        Component->SetVisibility(Element.bVisible);
    }
    Actor->SetActorHiddenInGame(false);
    return true;
}

void UConstructModScriptOverlay::DestroyPresentation(FConstructOverlaySource& State)
{
    for (const TPair<FString, TObjectPtr<UStaticMeshComponent>>& Pair : State.Components)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->DestroyComponent();
        }
    }
    State.Components.Empty();
    if (IsValid(State.Actor))
    {
        State.Actor->Destroy();
    }
    State.Actor = nullptr;
}

bool UConstructModScriptOverlay::Clear(const FString& Source, int64 Generation)
{
    if (!IsInGameThread() || bDeinitialized || Source.IsEmpty() || Generation < 0)
    {
        return false;
    }
    FConstructOverlaySource* Existing = Sources.Find(Source);
    if (Existing && Generation < Existing->Generation)
    {
        return false;
    }
    FConstructOverlaySource& State = Sources.FindOrAdd(Source);
    ActiveSourceCount -= State.Components.IsEmpty() ? 0 : 1;
    DestroyPresentation(State);
    State.Generation = Generation;
    State.bStopped = true;
    return true;
}

bool UConstructModScriptOverlay::ReleaseFinalizedScript(const FString& Source, int64 Generation)
{
    if (!Clear(Source, Generation))
    {
        return false;
    }
    Sources.Remove(Source);
    return true;
}

void UConstructModScriptOverlay::ClearAll()
{
    check(IsInGameThread());
    for (TPair<FString, FConstructOverlaySource>& Pair : Sources)
    {
        DestroyPresentation(Pair.Value);
        Pair.Value.bStopped = true;
    }
    ActiveSourceCount = 0;
}

int32 UConstructModScriptOverlay::Count(const FString& Source) const
{
    const FConstructOverlaySource* State = Sources.Find(Source);
    return State ? State->Components.Num() : 0;
}

bool UConstructModScriptOverlay::FindDrawn(const FString& Source, const FString& Id, FTransform& OutWorldTransform) const
{
    const FConstructOverlaySource* State = Sources.Find(Source);
    const TObjectPtr<UStaticMeshComponent>* Component = State ? State->Components.Find(Id) : nullptr;
    if (!Component || !IsValid(Component->Get()))
    {
        return false;
    }
    OutWorldTransform = (*Component)->GetComponentTransform();
    return true;
}

void UConstructModScriptOverlay::Deinitialize()
{
    bDeinitialized = true;
    ClearAll();
    Sources.Empty();
    Super::Deinitialize();
}
