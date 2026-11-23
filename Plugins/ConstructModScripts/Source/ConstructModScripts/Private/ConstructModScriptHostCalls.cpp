#include "ConstructModScriptHost.h"

#include "ConstructModScriptInput.h"
#include "ConstructModScriptOverlay.h"
#include "ConstructModScriptPanel.h"
#include "ConstructPresencePawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Base64.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "Subsystem/CrowdyVoxelWorld.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
    int32 BoundedHeldMs(int64 Value)
    {
        return static_cast<int32>(FMath::Clamp<int64>(Value, 0, MAX_int32));
    }

    bool OverlayPrimitive(ECrowdyModScriptShapeKind Kind, EConstructOverlayPrimitive& Out)
    {
        switch (Kind)
        {
        case ECrowdyModScriptShapeKind::Box:
            Out = EConstructOverlayPrimitive::Box;
            return true;
        case ECrowdyModScriptShapeKind::Sphere:
            Out = EConstructOverlayPrimitive::Sphere;
            return true;
        case ECrowdyModScriptShapeKind::Cylinder:
            Out = EConstructOverlayPrimitive::Cylinder;
            return true;
        case ECrowdyModScriptShapeKind::Capsule:
            Out = EConstructOverlayPrimitive::Capsule;
            return true;
        case ECrowdyModScriptShapeKind::Plane:
            Out = EConstructOverlayPrimitive::Plane;
            return true;
        default:
            return false;
        }
    }
}

FConstructScriptLabel UConstructModScriptHost::Label(const FCrowdyModScript& Script)
{
    FConstructScriptLabel Result;
    Result.Source = Script.ModId;
    Result.Name = Script.Name;
    Result.AuthorId = Script.AuthorId;
    Result.Generation = Script.Generation;
    return Result;
}

void UConstructModScriptHost::DrainClicks_Implementation(FCrowdyModScriptClicks& Clicks)
{
    Clicks = FCrowdyModScriptClicks();
    UConstructModScriptInput* Input = Panel ? Panel->GetPointerInput() : nullptr;
    if (!Input)
    {
        return;
    }
    const FConstructPointerSnapshot Snapshot = Input->DrainClicks();
    Clicks.Buttons = Snapshot.Buttons;
    Clicks.LeftHeldMs = BoundedHeldMs(Snapshot.LeftHeldMs);
    Clicks.MiddleHeldMs = BoundedHeldMs(Snapshot.MiddleHeldMs);
    Clicks.RightHeldMs = BoundedHeldMs(Snapshot.RightHeldMs);
    Clicks.Clicks.Reserve(Snapshot.Clicks.Num());
    for (const FConstructPointerClick& Native : Snapshot.Clicks)
    {
        FCrowdyModScriptClick& Click = Clicks.Clicks.AddDefaulted_GetRef();
        Click.bDown = Native.bDown;
        Click.Button = Native.Button;
        Click.AtMs = Native.AtMs;
        Click.HeldMs = BoundedHeldMs(Native.HeldMs);
        Click.X = Native.X;
        Click.Y = Native.Y;
    }
}

void UConstructModScriptHost::BuildChunk(TFunctionRef<FCrowdyVoxel(FIntVector)> Read, FCrowdyModScriptChunk& Out)
{
    Out = FCrowdyModScriptChunk();
    Out.Types.SetNumZeroed(4096);
    for (int32 Z = 0; Z < 16; ++Z)
    {
        for (int32 Y = 0; Y < 16; ++Y)
        {
            for (int32 X = 0; X < 16; ++X)
            {
                const FIntVector Position(X, Y, Z);
                const FCrowdyVoxel Voxel = Read(Position);
                Out.Types[X + 16 * Y + 256 * Z] = Voxel.Type >= 0 && Voxel.Type <= 255 ? static_cast<uint8>(Voxel.Type) : 0;
                if (!Voxel.State.IsEmpty())
                {
                    FCrowdyModScriptCellState& State = Out.States.AddDefaulted_GetRef();
                    State.X = X;
                    State.Y = Y;
                    State.Z = Z;
                    State.State = FBase64::Encode(Voxel.State);
                }
            }
        }
    }
}

bool UConstructModScriptHost::ReadChunk_Implementation(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FCrowdyModScriptChunk& Chunk)
{
    Chunk = FCrowdyModScriptChunk();
    const UCrowdyVoxelWorld* World = VoxelWorld.Get();
    if (!World || !World->IsChunkLoaded(ChunkX, ChunkY, ChunkZ))
    {
        return false;
    }
    BuildChunk([World, ChunkX, ChunkY, ChunkZ](FIntVector Position) { return World->GetVoxel(ChunkX, ChunkY, ChunkZ, Position); }, Chunk);
    return true;
}

void UConstructModScriptHost::ListPlayers_Implementation(const FCrowdyModScriptGrid& Box, TArray<FCrowdyModScriptPlayer>& Players)
{
    Players.Reset();
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    const double VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
    if (!FMath::IsFinite(VoxelSize) || VoxelSize <= 0.0)
    {
        return;
    }
    const APlayerController* Controller = PlayerController.Get();
    const APawn* Self = Controller ? Controller->GetPawn() : nullptr;
    for (TActorIterator<AConstructPresencePawn> It(World); It && Players.Num() < 64; ++It)
    {
        const AConstructPresencePawn* Pawn = *It;
        const UCrowdyEntityComponent* Entity = Pawn->FindComponentByClass<UCrowdyEntityComponent>();
        if (Pawn->IsHidden() || Pawn->IsActorBeingDestroyed() || !Entity)
        {
            continue;
        }
        const FGuid EntityId = Entity->GetNetID();
        if (!EntityId.IsValid())
        {
            continue;
        }
        FInt64Vector Chunk;
        FIntVector Position;
        const FVector Location = Pawn->GetActorLocation();
        const FRotator Rotation = Pawn->GetActorRotation();
        if (Location.ContainsNaN() || Rotation.ContainsNaN())
        {
            continue;
        }
        UCrowdyVoxelLibrary::WorldToVoxel(this, Location, Chunk.X, Chunk.Y, Chunk.Z, Position);
        if (!Inside(Box, Chunk))
        {
            continue;
        }
        FCrowdyModScriptPlayer& Player = Players.AddDefaulted_GetRef();
        Player.ActorId = EntityId.ToString(EGuidFormats::Digits);
        Player.bSelf = Pawn == Self;
        Player.Name = Pawn->GetPlayerDisplayName().Left(64);
        Player.Location = Location / VoxelSize;
        Player.Yaw = Rotation.Yaw;
        Player.Pitch = Rotation.Pitch;
        Player.Program = 0;
    }
}

bool UConstructModScriptHost::SetVoxel_Implementation(const FCrowdyModScript& Script, const FCrowdyVoxelWrite& Write, FString& Refusal)
{
    Refusal.Reset();
    UCrowdyVoxelWorld* World = VoxelWorld.Get();
    if (!World)
    {
        Refusal = TEXT("The voxel world is unavailable.");
        return false;
    }
    if (!World->IsChunkLoaded(Write.ChunkX, Write.ChunkY, Write.ChunkZ))
    {
        Refusal = TEXT("The chunk is not loaded.");
        return false;
    }
    const int64 EditId = World->SetVoxel(Write);
    SubmittedEdits.Add(EditId, Label(Script));
    return true;
}

void UConstructModScriptHost::ShowHud_Implementation(const FCrowdyModScript& Script, const FString& Text)
{
    Presentation->ShowHud(Label(Script), Text);
}

void UConstructModScriptHost::DrawOverlay_Implementation(const FCrowdyModScript& Script, const TArray<FCrowdyModScriptShape>& Shapes)
{
    if (Shapes.Num() > 64)
    {
        ShowFeedback(NSLOCTEXT("ConstructModScripts", "OverlayTooLarge", "The Mod Script overlay has too many shapes."));
        return;
    }
    UWorld* World = GetWorld();
    UConstructModScriptOverlay* Overlay = World ? World->GetSubsystem<UConstructModScriptOverlay>() : nullptr;
    if (!Overlay)
    {
        ShowFeedback(NSLOCTEXT("ConstructModScripts", "OverlayUnavailable", "The Mod Script overlay is unavailable."));
        return;
    }
    const double VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
    if (!FMath::IsFinite(VoxelSize) || VoxelSize <= 0.0)
    {
        ShowFeedback(NSLOCTEXT("ConstructModScripts", "OverlayScaleUnavailable", "The Mod Script overlay could not determine the voxel size."));
        return;
    }
    TArray<FConstructOverlayElement> Elements;
    Elements.Reserve(Shapes.Num());
    for (const FCrowdyModScriptShape& Shape : Shapes)
    {
        FConstructOverlayElement& Element = Elements.AddDefaulted_GetRef();
        if (!OverlayPrimitive(Shape.Kind, Element.Primitive))
        {
            ShowFeedback(NSLOCTEXT("ConstructModScripts", "OverlayShapeUnsupported", "The Mod Script requested an unsupported overlay shape."));
            return;
        }
        Element.Id = Shape.Id;
        Element.Transform = FTransform(Shape.Rotation, Shape.Location * VoxelSize, Shape.Size * (VoxelSize / 100.0));
        Element.Color = Shape.Color;
        Element.Opacity = Shape.Opacity;
        Element.bVisible = Shape.bVisible;
        Element.bUnlit = Shape.bUnlit;
    }
    if (!Overlay->Replace(Script.ModId, Script.Generation, Elements))
    {
        ShowFeedback(NSLOCTEXT("ConstructModScripts", "OverlayUpdateFailed", "The Mod Script overlay update could not be displayed."));
    }
}

void UConstructModScriptHost::ClearScript_Implementation(const FCrowdyModScript& Script)
{
    const FConstructScriptCard* Card = Presentation->Find(Script.ModId);
    const FText Reason = Card && Card->Label.Generation == Script.Generation && !Card->bRunning && !Card->StopReason.IsEmpty() ? Card->StopReason : NSLOCTEXT("ConstructModScripts", "Stopped", "Stopped");
    Presentation->Stopped(Script.ModId, Script.Generation, Reason);
    if (UWorld* World = GetWorld())
    {
        if (UConstructModScriptOverlay* Overlay = World->GetSubsystem<UConstructModScriptOverlay>())
        {
            Overlay->ReleaseFinalizedScript(Script.ModId, Script.Generation);
        }
    }
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostHeldDurationConversionTest, "Construct.ModScripts.Host.HeldDurationSaturation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostHeldDurationConversionTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Negative native durations clamp to zero"), BoundedHeldMs(-1), 0);
    TestEqual(TEXT("Zero native duration stays zero"), BoundedHeldMs(0), 0);
    TestEqual(TEXT("Ordinary native duration is preserved"), BoundedHeldMs(1234), 1234);
    TestEqual(TEXT("The SDK duration's maximum is preserved"), BoundedHeldMs(MAX_int32), MAX_int32);
    TestEqual(TEXT("A native duration beyond int32 saturates"), BoundedHeldMs(static_cast<int64>(MAX_int32) + 1), MAX_int32);
    TestEqual(TEXT("An extreme native duration cannot wrap negative"), BoundedHeldMs(MAX_int64), MAX_int32);
    return true;
}

#endif
