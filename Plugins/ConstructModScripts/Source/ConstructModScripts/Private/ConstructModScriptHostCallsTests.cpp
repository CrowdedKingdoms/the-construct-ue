#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructModScriptHost.h"

#include "ConstructModScriptHostTestAccess.h"
#include "ConstructModScriptOverlay.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Subsystem/CrowdyVoxelWorld.h"

namespace
{
    struct FHostCallsFixture
    {
        UWorld* World = nullptr;
        UConstructModScriptHost* Host = nullptr;
        UConstructModScriptOverlay* Overlay = nullptr;

        bool Initialize(FAutomationTestBase& Test)
        {
            if (!Test.TestNotNull(TEXT("An engine is available"), GEngine))
            {
                return false;
            }
            UWorld::InitializationValues Init;
            Init.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            if (!Test.TestNotNull(TEXT("An isolated preview world was created"), World))
            {
                return false;
            }
            GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
            Host = NewObject<UConstructModScriptHost>(World);
            Overlay = World->GetSubsystem<UConstructModScriptOverlay>();
            return Test.TestNotNull(TEXT("The host was constructed without binding a live source"), Host) && Test.TestNotNull(TEXT("The world owns its overlay renderer"), Overlay);
        }

        ~FHostCallsFixture()
        {
            if (World)
            {
                World->DestroyWorld(false);
                GEngine->DestroyWorldContext(World);
            }
        }
    };

    FCrowdyModScript ScriptForCalls()
    {
        FCrowdyModScript Script;
        Script.ModId = TEXT("host-call-example");
        Script.Name = TEXT("Host call example");
        Script.AuthorId = 7;
        Script.Generation = 3;
        return Script;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostChunkConversionTest, "Construct.ModScripts.Host.ChunkConversion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostChunkConversionTest::RunTest(const FString& Parameters)
{
    FCrowdyModScriptChunk Chunk;
    Chunk.Types = {1, 2, 3};
    Chunk.States.AddDefaulted();
    int32 Reads = 0;
    bool bCanonicalOrder = true;
    FConstructModScriptHostTestAccess::BuildChunk([&Reads, &bCanonicalOrder](FIntVector Position)
    {
        const int32 Index = Position.X + 16 * Position.Y + 256 * Position.Z;
        bCanonicalOrder &= Index == Reads++;
        FCrowdyVoxel Voxel;
        Voxel.Position = FIntVector(99, 99, 99);
        Voxel.Type = Index % 256;
        if (Index == 0)
        {
            Voxel.Type = -1;
            Voxel.State = {0, 1, 255};
        }
        else if (Index == 15)
        {
            Voxel.Type = 256;
            Voxel.State = {1};
        }
        else if (Index == 16)
        {
            Voxel.Type = 0;
            Voxel.State = {2};
        }
        else if (Index == 4095)
        {
            Voxel.State.Init(127, 1024);
        }
        return Voxel;
    }, Chunk);
    TestEqual(TEXT("The adapter reads each of the 4096 cells once"), Reads, 4096);
    TestTrue(TEXT("Reads follow x + 16y + 256z order"), bCanonicalOrder);
    if (!TestEqual(TEXT("The adapter replaces stale type output with exactly 4096 bytes"), Chunk.Types.Num(), 4096))
    {
        return false;
    }
    bool bTypesMatch = true;
    for (int32 Index = 0; Index < Chunk.Types.Num(); ++Index)
    {
        const uint8 Expected = Index == 0 || Index == 15 || Index == 16 ? 0 : static_cast<uint8>(Index % 256);
        bTypesMatch &= Chunk.Types[Index] == Expected;
    }
    TestTrue(TEXT("Byte-range types are preserved and out-of-range types become empty"), bTypesMatch);
    if (!TestEqual(TEXT("Only nonempty states are included and old output is replaced"), Chunk.States.Num(), 4))
    {
        return false;
    }
    TestEqual(TEXT("A negative-type cell retains its state"), Chunk.States[0].State, FString(TEXT("AAH/")));
    TestEqual(TEXT("The second state has x=15"), Chunk.States[1].X, 15);
    TestEqual(TEXT("The second state has y=0"), Chunk.States[1].Y, 0);
    TestEqual(TEXT("The third state has x=0"), Chunk.States[2].X, 0);
    TestEqual(TEXT("The third state has y=1"), Chunk.States[2].Y, 1);
    TestEqual(TEXT("An empty-type cell retains its state"), Chunk.States[2].State, FString(TEXT("Ag==")));
    TestEqual(TEXT("The final state has x=15"), Chunk.States[3].X, 15);
    TestEqual(TEXT("The final state has y=15"), Chunk.States[3].Y, 15);
    TestEqual(TEXT("The final state has z=15"), Chunk.States[3].Z, 15);
    TArray<uint8> Decoded;
    TestTrue(TEXT("The maximum-size state is valid base64"), FBase64::Decode(Chunk.States[3].State, Decoded));
    TestEqual(TEXT("The maximum-size state retains all bytes"), Decoded.Num(), 1024);
    TestTrue(TEXT("The maximum-size state's bytes are unchanged"), Decoded.ContainsByPredicate([](uint8 Byte) { return Byte != 127; }) == false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostUnavailableDataTest, "Construct.ModScripts.Host.UnavailableAndUnloadedData", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostUnavailableDataTest::RunTest(const FString& Parameters)
{
    FHostCallsFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    FCrowdyModScriptChunk Chunk;
    Chunk.Types.Add(1);
    Chunk.States.AddDefaulted();
    TestFalse(TEXT("A missing voxel world cannot answer a chunk"), Fixture.Host->ReadChunk_Implementation(0, 0, 0, Chunk));
    TestTrue(TEXT("An unavailable chunk answer clears previous output"), Chunk.Types.IsEmpty() && Chunk.States.IsEmpty());
    const FCrowdyModScript Script = ScriptForCalls();
    FCrowdyVoxelWrite Write;
    FString Refusal;
    TestFalse(TEXT("A missing voxel world cannot submit an edit"), Fixture.Host->SetVoxel_Implementation(Script, Write, Refusal));
    TestFalse(TEXT("A missing voxel world gives a player-facing refusal"), Refusal.IsEmpty());
    UCrowdyVoxelWorld* EmptyWorld = NewObject<UCrowdyVoxelWorld>(Fixture.World);
    FConstructModScriptHostTestAccess::SetVoxelWorld(*Fixture.Host, EmptyWorld);
    Chunk.Types.Add(1);
    TestFalse(TEXT("An unloaded chunk is not read or fetched"), Fixture.Host->ReadChunk_Implementation(0, 0, 0, Chunk));
    TestTrue(TEXT("An unloaded chunk answer clears previous output"), Chunk.Types.IsEmpty() && Chunk.States.IsEmpty());
    Refusal = TEXT("stale refusal");
    TestFalse(TEXT("An edit to an unloaded chunk is not submitted"), Fixture.Host->SetVoxel_Implementation(Script, Write, Refusal));
    TestEqual(TEXT("An unloaded chunk gives the current refusal"), Refusal, FString(TEXT("The chunk is not loaded.")));
    TestNull(TEXT("An unavailable edit does not enter the submitted edit map"), FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 0));
    FCrowdyModScriptClicks Clicks;
    Clicks.Buttons = 7;
    Clicks.LeftHeldMs = 100;
    Clicks.Clicks.AddDefaulted();
    Fixture.Host->DrainClicks_Implementation(Clicks);
    TestEqual(TEXT("A missing panel has no held buttons"), Clicks.Buttons, 0);
    TestEqual(TEXT("A missing panel clears old held durations"), Clicks.LeftHeldMs, 0);
    TestTrue(TEXT("A missing panel clears old click output"), Clicks.Clicks.IsEmpty());
    TArray<FCrowdyModScriptPlayer> Players;
    Players.AddDefaulted();
    Fixture.Host->ListPlayers_Implementation(FCrowdyModScriptGrid(), Players);
    TestTrue(TEXT("The isolated world does not manufacture players"), Players.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostPresentationCallsTest, "Construct.ModScripts.Host.HudAndClearKeepEditIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostPresentationCallsTest::RunTest(const FString& Parameters)
{
    FHostCallsFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    FCrowdyModScript Script = ScriptForCalls();
    Fixture.Host->ShowHud_Implementation(Script, TEXT("local host text"));
    const FConstructScriptCard* Card = Fixture.Host->GetPresentation()->Find(Script.ModId);
    if (!TestNotNull(TEXT("ShowHud creates the script's player-facing card"), Card))
    {
        return false;
    }
    TestEqual(TEXT("The SDK source maps to the Construct label"), Card->Label.Source, Script.ModId);
    TestEqual(TEXT("The script name maps to the Construct label"), Card->Label.Name, Script.Name);
    TestEqual(TEXT("The author maps to the Construct label"), Card->Label.AuthorId, Script.AuthorId);
    TestEqual(TEXT("The generation maps to the Construct label"), Card->Label.Generation, int64(Script.Generation));
    TestEqual(TEXT("HUD text reaches the existing presentation owner"), Card->Text, FString(TEXT("local host text")));
    Fixture.Host->ShowHud_Implementation(Script, FString());
    TestTrue(TEXT("An empty HUD update removes displayed text"), Fixture.Host->GetPresentation()->Find(Script.ModId)->Text.IsEmpty());
    Fixture.Host->ShowHud_Implementation(Script, TEXT("visible until stopped"));
    FConstructModScriptHostTestAccess::RetainEdit(*Fixture.Host, 1, Script);
    Fixture.Host->ClearScript_Implementation(Script);
    Card = Fixture.Host->GetPresentation()->Find(Script.ModId);
    TestFalse(TEXT("ClearScript marks the current run stopped"), Card->bRunning);
    TestTrue(TEXT("ClearScript removes its HUD text"), Card->Text.IsEmpty());
    TestEqual(TEXT("ClearScript records the stop label"), Card->StopReason.ToString(), FString(TEXT("Stopped")));
    TestEqual(TEXT("ClearScript removes its drawn overlay elements"), Fixture.Overlay->Count(Script.ModId), 0);
    // The SDK finalizes this run, so it makes no further host call for it.
    TestTrue(TEXT("ClearScript releases its stopped record instead of retaining a newer tombstone"), Fixture.Overlay->ReleaseFinalizedScript(Script.ModId, Script.Generation - 1));
    const FConstructScriptLabel* Retained = FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 1);
    if (!TestNotNull(TEXT("A stopped script's submitted edit keeps its identity for a later refusal"), Retained))
    {
        return false;
    }
    TestEqual(TEXT("The late-feedback label retains the source name"), Retained->Name, Script.Name);
    TestEqual(TEXT("The late-feedback label retains its generation"), Retained->Generation, int64(Script.Generation));
    ++Script.Generation;
    Fixture.Host->ShowHud_Implementation(Script, TEXT("new generation"));
    FCrowdyModScript Old = Script;
    --Old.Generation;
    Fixture.Host->ClearScript_Implementation(Old);
    Card = Fixture.Host->GetPresentation()->Find(Script.ModId);
    TestTrue(TEXT("An older stop leaves the new run active"), Card->bRunning);
    TestEqual(TEXT("An older stop leaves the new HUD intact"), Card->Text, FString(TEXT("new generation")));
    Fixture.Host->GetPresentation()->Stopped(Script.ModId, Script.Generation, FText::FromString(TEXT("You left this land.")));
    Fixture.Host->ClearScript_Implementation(Script);
    TestEqual(TEXT("A later clear preserves the source's specific stopped reason"), Fixture.Host->GetPresentation()->Find(Script.ModId)->StopReason.ToString(), FString(TEXT("You left this land.")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostOverlayConversionTest, "Construct.ModScripts.Host.OverlayUnitsAndQuaternion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostOverlayConversionTest::RunTest(const FString& Parameters)
{
    FHostCallsFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    const FCrowdyModScript Script = ScriptForCalls();
    TArray<FCrowdyModScriptShape> Shapes;
    const ECrowdyModScriptShapeKind Kinds[] = {ECrowdyModScriptShapeKind::Box, ECrowdyModScriptShapeKind::Sphere, ECrowdyModScriptShapeKind::Cylinder, ECrowdyModScriptShapeKind::Capsule, ECrowdyModScriptShapeKind::Plane};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Kinds); ++Index)
    {
        FCrowdyModScriptShape& Shape = Shapes.AddDefaulted_GetRef();
        Shape.Id = FString::Printf(TEXT("shape-%d"), Index);
        Shape.Kind = Kinds[Index];
        Shape.Location = FVector(1.5, -2.0, 3.25);
        Shape.Size = FVector(2.0, 3.0, 4.0);
        Shape.Rotation = FRotator(15.0, 35.0, 5.0).Quaternion();
        Shape.Color = FLinearColor(0.2f, 0.4f, 0.6f);
        Shape.Opacity = 0.4f;
        Shape.bVisible = Index % 2 == 0;
        Shape.bUnlit = Index % 2 != 0;
    }
    Fixture.Host->DrawOverlay_Implementation(Script, Shapes);
    if (!TestEqual(TEXT("The host maps all five SDK primitives into the real renderer; required overlay assets must be installed"), Fixture.Overlay->Count(Script.ModId), 5))
    {
        return false;
    }
    FTransform Drawn;
    if (!TestTrue(TEXT("The converted SDK box has an actual drawn component"), Fixture.Overlay->FindDrawn(Script.ModId, Shapes[0].Id, Drawn)))
    {
        return false;
    }
    TestTrue(TEXT("Voxel locations convert to the isolated world's default 100 uu cells"), Drawn.GetLocation().Equals(FVector(150.0, -200.0, 325.0), 0.001));
    TestTrue(TEXT("Voxel dimensions convert to the renderer's 100 uu primitive scale"), Drawn.GetScale3D().Equals(FVector(2.0, 3.0, 4.0), 0.001));
    TestTrue(TEXT("The SDK quaternion reaches the rendered transform unchanged"), Drawn.GetRotation().Equals(Shapes[0].Rotation, 0.0001));
    TestTrue(TEXT("An invisible SDK shape is retained for later updates"), Fixture.Overlay->FindDrawn(Script.ModId, Shapes[1].Id, Drawn));
    Fixture.Host->DrawOverlay_Implementation(Script, {});
    TestEqual(TEXT("An empty SDK replacement clears the script's overlay"), Fixture.Overlay->Count(Script.ModId), 0);
    Fixture.Host->DrawOverlay_Implementation(Script, Shapes);
    TestEqual(TEXT("An empty replacement permits more shapes from the same run"), Fixture.Overlay->Count(Script.ModId), 5);
    Fixture.Host->ClearScript_Implementation(Script);
    TestEqual(TEXT("ClearScript removes the actual drawn SDK shapes"), Fixture.Overlay->Count(Script.ModId), 0);
    return true;
}

#endif
