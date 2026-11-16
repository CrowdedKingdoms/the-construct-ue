#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructModScriptOverlay.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

struct FConstructOverlayTestAccess
{
    static UStaticMesh* Mesh(const UConstructModScriptOverlay& Overlay, EConstructOverlayPrimitive Primitive)
    {
        return Overlay.MeshFor(Primitive);
    }

    static UMaterialInterface* Material(const UConstructModScriptOverlay& Overlay, bool bUnlit)
    {
        return bUnlit ? Overlay.UnlitMaterial.Get() : Overlay.LitMaterial.Get();
    }

    static UStaticMeshComponent* Component(const UConstructModScriptOverlay& Overlay, const FString& Source, const FString& Id)
    {
        const FConstructOverlaySource* State = Overlay.Sources.Find(Source);
        const TObjectPtr<UStaticMeshComponent>* Result = State ? State->Components.Find(Id) : nullptr;
        return Result ? Result->Get() : nullptr;
    }

    static int32 SourceRecords(const UConstructModScriptOverlay& Overlay)
    {
        return Overlay.Sources.Num();
    }

    static int32 ActiveSources(const UConstructModScriptOverlay& Overlay)
    {
        return Overlay.ActiveSourceCount;
    }
};

namespace
{
    struct FOverlayFixture
    {
        UWorld* World = nullptr;
        UConstructModScriptOverlay* Overlay = nullptr;

        bool Initialize(FAutomationTestBase& Test)
        {
            if (!Test.TestNotNull(TEXT("An engine is available for the overlay world fixture"), GEngine))
            {
                return false;
            }
            UWorld::InitializationValues Init;
            Init.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            if (!Test.TestNotNull(TEXT("The isolated EditorPreview world was created"), World))
            {
                return false;
            }
            GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
            Overlay = World->GetSubsystem<UConstructModScriptOverlay>();
            if (!Test.TestNotNull(TEXT("The world owns an overlay subsystem"), Overlay))
            {
                return false;
            }
            bool bAssetsReady = true;
            bAssetsReady &= Test.TestNotNull(TEXT("Required lit material /Game/Construct/ModScripts/Materials/M_ConstructScriptOverlayLit is present"), FConstructOverlayTestAccess::Material(*Overlay, false));
            bAssetsReady &= Test.TestNotNull(TEXT("Required unlit material /Game/Construct/ModScripts/Materials/M_ConstructScriptOverlayUnlit is present"), FConstructOverlayTestAccess::Material(*Overlay, true));
            for (bool bUnlit : {false, true})
            {
                if (const UMaterialInterface* Material = FConstructOverlayTestAccess::Material(*Overlay, bUnlit))
                {
                    bAssetsReady &= Test.TestEqual(TEXT("Overlay materials use translucent blending for per-shape opacity"), Material->GetBlendMode(), BLEND_Translucent);
                    bAssetsReady &= Test.TestEqual(TEXT("Overlay materials have the requested lit or unlit shading model"), Material->GetShadingModels().HasShadingModel(MSM_Unlit), bUnlit);
                }
            }
            for (int32 Index = 0; Index < 5; ++Index)
            {
                const FString Label = Index == 3 ? TEXT("Required mesh /Game/Construct/ModScripts/Meshes/SM_ConstructScriptOverlayCapsule is present") : FString::Printf(TEXT("Required Engine basic primitive mesh %d is present"), Index);
                bAssetsReady &= Test.TestNotNull(Label, FConstructOverlayTestAccess::Mesh(*Overlay, static_cast<EConstructOverlayPrimitive>(Index)));
            }
            return bAssetsReady;
        }

        ~FOverlayFixture()
        {
            if (World)
            {
                World->DestroyWorld(false);
                GEngine->DestroyWorldContext(World);
            }
        }
    };

    FConstructOverlayElement Element(const FString& Id, EConstructOverlayPrimitive Primitive = EConstructOverlayPrimitive::Box)
    {
        FConstructOverlayElement Result;
        Result.Id = Id;
        Result.Primitive = Primitive;
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOverlayGenerationTest, "Construct.ModScripts.Overlay.GenerationAndAtomicUpdates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructOverlayGenerationTest::RunTest(const FString& Parameters)
{
    FOverlayFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    UConstructModScriptOverlay& Overlay = *Fixture.Overlay;
    TestTrue(TEXT("A new run draws its first shape"), Overlay.Replace(TEXT("one"), 10, {Element(TEXT("box"))}));
    TestEqual(TEXT("The source retains one shape"), Overlay.Count(TEXT("one")), 1);
    TestFalse(TEXT("A stale replacement is refused"), Overlay.Replace(TEXT("one"), 9, {Element(TEXT("stale"))}));
    TestFalse(TEXT("A stale stop is refused"), Overlay.Clear(TEXT("one"), 9));
    TestEqual(TEXT("Stale work preserves the current presentation"), Overlay.Count(TEXT("one")), 1);

    TestFalse(TEXT("Duplicate ids reject the entire new generation"), Overlay.Replace(TEXT("one"), 11, {Element(TEXT("duplicate")), Element(TEXT("duplicate"))}));
    FTransform Drawn;
    TestTrue(TEXT("Rejected replacement retains the previous component"), Overlay.FindDrawn(TEXT("one"), TEXT("box"), Drawn));
    TestTrue(TEXT("Rejected input does not consume a generation"), Overlay.Replace(TEXT("one"), 10, {Element(TEXT("valid"))}));
    TestTrue(TEXT("An empty replacement removes the presentation"), Overlay.Replace(TEXT("one"), 10, {}));
    TestEqual(TEXT("An empty replacement retains no shape"), Overlay.Count(TEXT("one")), 0);
    TestTrue(TEXT("An empty replacement permits further updates from that run"), Overlay.Replace(TEXT("one"), 10, {Element(TEXT("again"))}));

    TestTrue(TEXT("Stopping the current generation clears its components"), Overlay.Clear(TEXT("one"), 10));
    TestEqual(TEXT("A stopped source retains no shape"), Overlay.Count(TEXT("one")), 0);
    TestFalse(TEXT("A stopped run cannot resurrect itself"), Overlay.Replace(TEXT("one"), 10, {Element(TEXT("late"))}));
    TestTrue(TEXT("A newer run may draw after a stop"), Overlay.Replace(TEXT("one"), 11, {Element(TEXT("new"))}));
    TestTrue(TEXT("A stop arriving before the first draw is remembered"), Overlay.Clear(TEXT("never-drawn"), 12));
    TestFalse(TEXT("An older unseen run cannot draw after its stop"), Overlay.Replace(TEXT("never-drawn"), 11, {Element(TEXT("late"))}));
    TestFalse(TEXT("The stopped unseen generation cannot draw either"), Overlay.Replace(TEXT("never-drawn"), 12, {Element(TEXT("late"))}));
    TestTrue(TEXT("A later unseen generation may start"), Overlay.Replace(TEXT("never-drawn"), 13, {Element(TEXT("new"))}));

    Overlay.ClearAll();
    TestEqual(TEXT("Leaving clears the first source"), Overlay.Count(TEXT("one")), 0);
    TestEqual(TEXT("Leaving clears the second source"), Overlay.Count(TEXT("never-drawn")), 0);
    TestEqual(TEXT("ClearAll releases every active slot"), FConstructOverlayTestAccess::ActiveSources(Overlay), 0);
    TestFalse(TEXT("Leaving keeps a tombstone for the first run"), Overlay.Replace(TEXT("one"), 11, {Element(TEXT("late"))}));
    TestFalse(TEXT("Leaving keeps a tombstone for the second run"), Overlay.Replace(TEXT("never-drawn"), 13, {Element(TEXT("late"))}));
    TestTrue(TEXT("A later run can return after leaving"), Overlay.Replace(TEXT("one"), 12, {Element(TEXT("returned"))}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOverlayLimitsTest, "Construct.ModScripts.Overlay.LimitsAndIndependentSources", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructOverlayLimitsTest::RunTest(const FString& Parameters)
{
    FOverlayFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    UConstructModScriptOverlay& Overlay = *Fixture.Overlay;
    TArray<FConstructOverlayElement> Elements;
    for (int32 Index = 0; Index < UConstructModScriptOverlay::MaxElementsPerSource; ++Index)
    {
        Elements.Add(Element(FString::Printf(TEXT("shape-%d"), Index)));
    }
    TestTrue(TEXT("Exactly 64 shapes are accepted"), Overlay.Replace(TEXT("source-0"), 0, Elements));
    Elements.Add(Element(TEXT("overflow")));
    TestFalse(TEXT("65 shapes reject the whole update"), Overlay.Replace(TEXT("source-0"), 1, Elements));
    TestEqual(TEXT("The over-cap update preserves all 64 good shapes"), Overlay.Count(TEXT("source-0")), 64);
    for (int32 Index = 1; Index < UConstructModScriptOverlay::MaxSources; ++Index)
    {
        TestTrue(TEXT("Each independent source may reuse the same shape id"), Overlay.Replace(FString::Printf(TEXT("source-%d"), Index), 0, {Element(TEXT("shared"))}));
    }
    TestEqual(TEXT("The active count reaches the source cap"), FConstructOverlayTestAccess::ActiveSources(Overlay), UConstructModScriptOverlay::MaxSources);
    TestFalse(TEXT("A ninth source retaining shapes is refused"), Overlay.Replace(TEXT("source-8"), 0, {Element(TEXT("shared"))}));
    TestEqual(TEXT("A rejected replacement does not change the active count"), FConstructOverlayTestAccess::ActiveSources(Overlay), UConstructModScriptOverlay::MaxSources);
    TestTrue(TEXT("An existing source may replace at the source cap"), Overlay.Replace(TEXT("source-1"), 0, {Element(TEXT("replaced"))}));
    TestEqual(TEXT("Replacing an active source does not consume another slot"), FConstructOverlayTestAccess::ActiveSources(Overlay), UConstructModScriptOverlay::MaxSources);
    TestTrue(TEXT("Stopping one source frees its active slot"), Overlay.Clear(TEXT("source-1"), 0));
    TestEqual(TEXT("Clear decrements the active count"), FConstructOverlayTestAccess::ActiveSources(Overlay), UConstructModScriptOverlay::MaxSources - 1);
    TestTrue(TEXT("Clearing the same tombstone again succeeds"), Overlay.Clear(TEXT("source-1"), 0));
    TestEqual(TEXT("Repeated Clear does not decrement twice"), FConstructOverlayTestAccess::ActiveSources(Overlay), UConstructModScriptOverlay::MaxSources - 1);
    TestTrue(TEXT("A new source can use the released slot"), Overlay.Replace(TEXT("source-8"), 0, {Element(TEXT("shared"))}));
    TestEqual(TEXT("Clearing another source does not change source 0"), Overlay.Count(TEXT("source-0")), 64);
    TestEqual(TEXT("Clearing another source does not change source 2"), Overlay.Count(TEXT("source-2")), 1);

    TestTrue(TEXT("An empty replacement frees a slot without stopping a run"), Overlay.Replace(TEXT("source-8"), 0, {}));
    TestEqual(TEXT("Empty replacement decrements the active count"), FConstructOverlayTestAccess::ActiveSources(Overlay), UConstructModScriptOverlay::MaxSources - 1);
    TestTrue(TEXT("Repeating an empty replacement succeeds"), Overlay.Replace(TEXT("source-8"), 0, {}));
    TestEqual(TEXT("Repeated empty replacement does not decrement twice"), FConstructOverlayTestAccess::ActiveSources(Overlay), UConstructModScriptOverlay::MaxSources - 1);
    FConstructOverlayElement Invalid = Element(TEXT(""));
    TestFalse(TEXT("An empty id is rejected"), Overlay.Replace(TEXT("invalid"), 0, {Invalid}));
    Invalid = Element(FString::ChrN(33, TEXT('x')));
    TestFalse(TEXT("An id longer than 32 characters is rejected"), Overlay.Replace(TEXT("invalid"), 0, {Invalid}));
    Invalid = Element(TEXT("bad-opacity"));
    Invalid.Opacity = 1.1f;
    TestFalse(TEXT("An opacity outside 0 to 1 is rejected"), Overlay.Replace(TEXT("invalid"), 0, {Invalid}));
    Invalid = Element(TEXT("bad-primitive"), static_cast<EConstructOverlayPrimitive>(255));
    TestFalse(TEXT("An unknown primitive is rejected"), Overlay.Replace(TEXT("invalid"), 0, {Invalid}));
    Invalid = Element(TEXT("bad-scale"));
    Invalid.Transform.SetScale3D(FVector(1.0, 0.0, 1.0));
    TestFalse(TEXT("A zero scale is rejected"), Overlay.Replace(TEXT("invalid"), 0, {Invalid}));
    TestEqual(TEXT("Invalid sources allocate no records"), FConstructOverlayTestAccess::SourceRecords(Overlay), 9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOverlayFinalizationTest, "Construct.ModScripts.Overlay.FinalizedScriptRecordsAreReleased", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructOverlayFinalizationTest::RunTest(const FString& Parameters)
{
    FOverlayFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    UConstructModScriptOverlay& Overlay = *Fixture.Overlay;
    for (int32 Index = 0; Index < 256; ++Index)
    {
        TestTrue(TEXT("A finalized script that never drew is released"), Overlay.ReleaseFinalizedScript(FString::Printf(TEXT("never-drawn-%d"), Index), Index));
    }
    TestEqual(TEXT("Finalized scripts retain no historical source records"), FConstructOverlayTestAccess::SourceRecords(Overlay), 0);
    TestEqual(TEXT("Finalizing unseen scripts consumes no active slots"), FConstructOverlayTestAccess::ActiveSources(Overlay), 0);
    if (!TestTrue(TEXT("A newer active run draws"), Overlay.Replace(TEXT("running"), 3, {Element(TEXT("box"))})))
    {
        return false;
    }
    UStaticMeshComponent* Component = FConstructOverlayTestAccess::Component(Overlay, TEXT("running"), TEXT("box"));
    TestFalse(TEXT("An older finalization cannot release a newer run"), Overlay.ReleaseFinalizedScript(TEXT("running"), 2));
    TestTrue(TEXT("The newer component survives stale finalization"), FConstructOverlayTestAccess::Component(Overlay, TEXT("running"), TEXT("box")) == Component);
    TestEqual(TEXT("Stale finalization preserves the active count"), FConstructOverlayTestAccess::ActiveSources(Overlay), 1);
    TestTrue(TEXT("Finalizing the current run clears and releases it"), Overlay.ReleaseFinalizedScript(TEXT("running"), 3));
    TestEqual(TEXT("Current finalization removes its presentation"), Overlay.Count(TEXT("running")), 0);
    TestEqual(TEXT("Current finalization releases its record"), FConstructOverlayTestAccess::SourceRecords(Overlay), 0);
    TestEqual(TEXT("Current finalization releases its active slot"), FConstructOverlayTestAccess::ActiveSources(Overlay), 0);
    TestTrue(TEXT("Ordinary Clear still records a stopped generation"), Overlay.Clear(TEXT("direct"), 7));
    TestEqual(TEXT("Ordinary Clear retains its tombstone"), FConstructOverlayTestAccess::SourceRecords(Overlay), 1);
    TestFalse(TEXT("Ordinary Clear still rejects that run's late draw"), Overlay.Replace(TEXT("direct"), 7, {Element(TEXT("late"))}));
    TestFalse(TEXT("Stale finalization cannot remove a newer tombstone"), Overlay.ReleaseFinalizedScript(TEXT("direct"), 6));
    TestEqual(TEXT("The newer tombstone survives stale finalization"), FConstructOverlayTestAccess::SourceRecords(Overlay), 1);
    TestTrue(TEXT("Finalizing a stopped generation releases its tombstone"), Overlay.ReleaseFinalizedScript(TEXT("direct"), 7));
    TestEqual(TEXT("Finalized tombstones retain no source records"), FConstructOverlayTestAccess::SourceRecords(Overlay), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOverlayPresentationTest, "Construct.ModScripts.Overlay.WorldTransformAndPresentation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructOverlayPresentationTest::RunTest(const FString& Parameters)
{
    FOverlayFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    UConstructModScriptOverlay& Overlay = *Fixture.Overlay;
    TArray<FConstructOverlayElement> Elements;
    for (int32 Index = 0; Index < 5; ++Index)
    {
        FConstructOverlayElement Shape = Element(FString::Printf(TEXT("primitive-%d"), Index), static_cast<EConstructOverlayPrimitive>(Index));
        Shape.Transform = FTransform(FRotator(17.0, 37.0, -9.0), FVector(3800.0, -6400.0, 250.0 + Index * 300.0), FVector(1.5, 0.75, 2.0));
        Shape.Color = FLinearColor(0.2f, 0.4f, 0.7f);
        Shape.Opacity = 0.35f;
        Shape.bUnlit = (Index % 2) != 0;
        Shape.bVisible = Index != 4;
        Elements.Add(Shape);
    }
    if (!TestTrue(TEXT("Every supported primitive draws with the required material parameters"), Overlay.Replace(TEXT("presentation"), 1, Elements)))
    {
        return false;
    }
    TestEqual(TEXT("Hidden elements remain part of the shape limit and retained count"), Overlay.Count(TEXT("presentation")), 5);
    for (const FConstructOverlayElement& Shape : Elements)
    {
        UStaticMeshComponent* Component = FConstructOverlayTestAccess::Component(Overlay, TEXT("presentation"), Shape.Id);
        if (!TestNotNull(TEXT("A submitted shape has an actual registered mesh component"), Component))
        {
            return false;
        }
        TestTrue(TEXT("The component is registered in the fixture world"), Component->IsRegistered() && Component->GetWorld() == Fixture.World);
        TestTrue(TEXT("The component is transient"), Component->HasAnyFlags(RF_Transient));
        TestTrue(TEXT("The owner is transient and does not replicate"), Component->GetOwner()->HasAnyFlags(RF_Transient) && !Component->GetOwner()->GetIsReplicated());
        TestFalse(TEXT("The owner does not enable collision"), Component->GetOwner()->GetActorEnableCollision());
        TestEqual(TEXT("No collision query or physics is enabled"), Component->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
        TestEqual(TEXT("The visibility trace channel is ignored"), Component->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
        TestEqual(TEXT("The camera trace channel is ignored"), Component->GetCollisionResponseToChannel(ECC_Camera), ECR_Ignore);
        TestFalse(TEXT("No overlap events are generated"), Component->GetGenerateOverlapEvents());
        TestFalse(TEXT("No navigation geometry is contributed"), Component->CanEverAffectNavigation());
        TestFalse(TEXT("No shadows are cast"), Component->CastShadow);
        TestEqual(TEXT("Visibility follows the input"), Component->IsVisible(), Shape.bVisible);
        FTransform Actual;
        TestTrue(TEXT("FindDrawn reads an existing component"), Overlay.FindDrawn(TEXT("presentation"), Shape.Id, Actual));
        TestTrue(TEXT("World location is supplied directly without grid or axis conversion"), Actual.GetLocation().Equals(Shape.Transform.GetLocation(), 0.001));
        TestTrue(TEXT("World rotation is supplied directly"), Actual.GetRotation().Equals(Shape.Transform.GetRotation(), 0.001));
        const FVector BoundsSize = Component->GetStaticMesh()->GetBounds().BoxExtent * 2.0;
        const FVector Dimensions = BoundsSize * Actual.GetScale3D();
        TestTrue(TEXT("X and Y dimensions normalize each mesh to a 100 uu primitive"), FMath::IsNearlyEqual(Dimensions.X, 150.0, 0.01) && FMath::IsNearlyEqual(Dimensions.Y, 75.0, 0.01));
        if (Shape.Primitive != EConstructOverlayPrimitive::Plane)
        {
            TestTrue(TEXT("Z dimension normalizes the volumetric mesh to a 100 uu primitive"), FMath::IsNearlyEqual(Dimensions.Z, 200.0, 0.01));
        }
        UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
        if (!TestNotNull(TEXT("Each shape has a dynamic material instance"), Dynamic))
        {
            return false;
        }
        TestTrue(TEXT("Lit and unlit input selects the corresponding material"), Dynamic->Parent == FConstructOverlayTestAccess::Material(Overlay, Shape.bUnlit));
        FLinearColor Color;
        float Opacity = -1.0f;
        TestTrue(TEXT("The material exposes its Color parameter"), Dynamic->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color));
        TestTrue(TEXT("The material exposes its Opacity parameter"), Dynamic->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Opacity")), Opacity));
        TestTrue(TEXT("Per-shape color reaches the material"), Color.Equals(FLinearColor(0.2f, 0.4f, 0.7f, 1.0f), 0.001f));
        TestTrue(TEXT("Per-shape opacity reaches the material"), FMath::IsNearlyEqual(Opacity, 0.35f));
        const FTransform Moved(FRotator(0.0, -48.0, 0.0), FVector(-123.0, 456.0, 789.0), FVector(2.0));
        Component->SetWorldTransform(Moved);
        Overlay.FindDrawn(TEXT("presentation"), Shape.Id, Actual);
        TestTrue(TEXT("FindDrawn reads the component rather than cached submission data"), Actual.Equals(Moved, 0.001));
    }
    FTransform Missing;
    TestFalse(TEXT("An unknown source has no drawn transform"), Overlay.FindDrawn(TEXT("missing"), TEXT("primitive-0"), Missing));
    TestFalse(TEXT("An unknown id has no drawn transform"), Overlay.FindDrawn(TEXT("presentation"), TEXT("missing"), Missing));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOverlayReuseTest, "Construct.ModScripts.Overlay.RetainedComponentAndMaterialReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructOverlayReuseTest::RunTest(const FString& Parameters)
{
    FOverlayFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    UConstructModScriptOverlay& Overlay = *Fixture.Overlay;
    FConstructOverlayElement Moving = Element(TEXT("moving"));
    const FConstructOverlayElement Still = Element(TEXT("still"), EConstructOverlayPrimitive::Sphere);
    if (!TestTrue(TEXT("Two initial ids create their components"), Overlay.Replace(TEXT("reuse"), 4, {Moving, Still})))
    {
        return false;
    }
    UStaticMeshComponent* MovingComponent = FConstructOverlayTestAccess::Component(Overlay, TEXT("reuse"), Moving.Id);
    UStaticMeshComponent* StillComponent = FConstructOverlayTestAccess::Component(Overlay, TEXT("reuse"), Still.Id);
    UMaterialInterface* MovingMaterial = MovingComponent->GetMaterial(0);
    UMaterialInterface* StillMaterial = StillComponent->GetMaterial(0);
    AActor* Owner = MovingComponent->GetOwner();
    const FTransform StillTransform = StillComponent->GetComponentTransform();
    Moving.Transform.SetLocation(FVector(250.0, -900.0, 125.0));
    Moving.Color = FLinearColor(0.8f, 0.1f, 0.2f);
    Moving.Opacity = 0.45f;
    TestTrue(TEXT("An animated update succeeds in the current generation"), Overlay.Replace(TEXT("reuse"), 4, {Moving, Still}));
    TestTrue(TEXT("The animated id reuses its mesh component"), FConstructOverlayTestAccess::Component(Overlay, TEXT("reuse"), Moving.Id) == MovingComponent);
    TestTrue(TEXT("The unchanged id reuses its mesh component"), FConstructOverlayTestAccess::Component(Overlay, TEXT("reuse"), Still.Id) == StillComponent);
    TestTrue(TEXT("An animated update reuses its dynamic material"), MovingComponent->GetMaterial(0) == MovingMaterial);
    TestTrue(TEXT("The unchanged id keeps its material and transform"), StillComponent->GetMaterial(0) == StillMaterial && StillComponent->GetComponentTransform().Equals(StillTransform));
    TestTrue(TEXT("The source actor remains the same"), MovingComponent->GetOwner() == Owner && StillComponent->GetOwner() == Owner);
    TestTrue(TEXT("The animated id receives its updated location"), MovingComponent->GetComponentLocation().Equals(Moving.Transform.GetLocation()));

    Moving.bUnlit = true;
    Moving.Primitive = EConstructOverlayPrimitive::Cylinder;
    TestTrue(TEXT("Changing shading and primitive succeeds"), Overlay.Replace(TEXT("reuse"), 4, {Moving, Still}));
    TestTrue(TEXT("A shading or primitive change keeps the existing mesh component"), FConstructOverlayTestAccess::Component(Overlay, TEXT("reuse"), Moving.Id) == MovingComponent);
    TestTrue(TEXT("A shading change creates the required new material instance"), MovingComponent->GetMaterial(0) != MovingMaterial);
    TestTrue(TEXT("A shading change leaves the other id's material untouched"), StillComponent->GetMaterial(0) == StillMaterial);
    TestTrue(TEXT("The primitive changes on the retained component"), MovingComponent->GetStaticMesh() == FConstructOverlayTestAccess::Mesh(Overlay, EConstructOverlayPrimitive::Cylinder));
    MovingMaterial = MovingComponent->GetMaterial(0);
    Moving.Primitive = EConstructOverlayPrimitive::Box;
    TestTrue(TEXT("A primitive-only change succeeds"), Overlay.Replace(TEXT("reuse"), 4, {Moving, Still}));
    TestTrue(TEXT("A primitive-only change retains its material instance"), MovingComponent->GetMaterial(0) == MovingMaterial);

    const FTransform BeforeInvalid = MovingComponent->GetComponentTransform();
    Moving.Transform.SetLocation(FVector(9999.0));
    TestFalse(TEXT("A duplicate id rejects an otherwise animated replacement"), Overlay.Replace(TEXT("reuse"), 4, {Moving, Moving}));
    TestTrue(TEXT("Rejected input preserves the component's prior transform and material"), MovingComponent->GetComponentTransform().Equals(BeforeInvalid) && MovingComponent->GetMaterial(0) == MovingMaterial);
    TWeakObjectPtr<UStaticMeshComponent> Removed = StillComponent;
    TestTrue(TEXT("A removed id and a new id can replace one another"), Overlay.Replace(TEXT("reuse"), 4, {Moving, Element(TEXT("added"))}));
    TestTrue(TEXT("An unchanged id survives a membership change"), FConstructOverlayTestAccess::Component(Overlay, TEXT("reuse"), Moving.Id) == MovingComponent && MovingComponent->GetMaterial(0) == MovingMaterial);
    TestTrue(TEXT("The removed id is destroyed or unregistered"), !Removed.IsValid() || !Removed->IsRegistered());
    TestNotNull(TEXT("The new id gets a component"), FConstructOverlayTestAccess::Component(Overlay, TEXT("reuse"), TEXT("added")));
    TestEqual(TEXT("The source still owns exactly two shape components"), Overlay.Count(TEXT("reuse")), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructOverlayLifetimeTest, "Construct.ModScripts.Overlay.WorldLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructOverlayLifetimeTest::RunTest(const FString& Parameters)
{
    TWeakObjectPtr<UStaticMeshComponent> Component;
    TWeakObjectPtr<AActor> Actor;
    {
        FOverlayFixture Fixture;
        if (!Fixture.Initialize(*this))
        {
            return false;
        }
        if (!TestTrue(TEXT("A world-owned overlay can draw before teardown"), Fixture.Overlay->Replace(TEXT("lifetime"), 1, {Element(TEXT("box"))})))
        {
            return false;
        }
        Component = FConstructOverlayTestAccess::Component(*Fixture.Overlay, TEXT("lifetime"), TEXT("box"));
        Actor = Component->GetOwner();
        Fixture.Overlay->Deinitialize();
        TestEqual(TEXT("Deinitialize releases source records and tombstones"), FConstructOverlayTestAccess::SourceRecords(*Fixture.Overlay), 0);
        TestEqual(TEXT("Deinitialize releases every active slot"), FConstructOverlayTestAccess::ActiveSources(*Fixture.Overlay), 0);
        TestTrue(TEXT("Deinitialize destroys the local actor"), !Actor.IsValid() || Actor->IsActorBeingDestroyed());
        TestTrue(TEXT("Deinitialize destroys or unregisters the mesh component"), !Component.IsValid() || !Component->IsRegistered());
        TestFalse(TEXT("A late replacement cannot recreate presentation after deinitialization"), Fixture.Overlay->Replace(TEXT("lifetime"), 2, {Element(TEXT("late"))}));
        TestFalse(TEXT("A late stop cannot recreate records after deinitialization"), Fixture.Overlay->Clear(TEXT("lifetime"), 2));
        TestFalse(TEXT("A late finalization cannot recreate records after deinitialization"), Fixture.Overlay->ReleaseFinalizedScript(TEXT("lifetime"), 2));
        TestEqual(TEXT("Late calls retain no records after deinitialization"), FConstructOverlayTestAccess::SourceRecords(*Fixture.Overlay), 0);
    }
    TestTrue(TEXT("World teardown leaves no registered overlay component"), !Component.IsValid() || !Component->IsRegistered());
    return true;
}

#endif
