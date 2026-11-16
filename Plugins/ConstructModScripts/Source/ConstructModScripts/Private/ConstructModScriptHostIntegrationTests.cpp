#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructModScriptHost.h"

#include "Components/SceneComponent.h"
#include "ConstructClaimsSubsystem.h"
#include "ConstructModScriptHostTestAccess.h"
#include "ConstructModScriptOverlay.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Subsystem/CrowdyModScripts.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    // These are host event-consumption tests. The SDK object is never initialized, and the
    // test inputs do not establish SDK consent, platform replies or a running script VM.
    struct FHostSourceFixture
    {
        UWorld* World = nullptr;
        TStrongObjectPtr<UConstructModScriptHost> Host;
        TStrongObjectPtr<UCrowdyModScripts> Source;
        TStrongObjectPtr<UConstructClaimsSubsystem> Claims;
        APawn* Pawn = nullptr;
        APlayerController* Controller = nullptr;

        bool Initialize(FAutomationTestBase& Test)
        {
            if (!Test.TestNotNull(TEXT("An engine is available for the source fixture"), GEngine))
            {
                return false;
            }
            UWorld::InitializationValues Init;
            Init.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            if (!Test.TestNotNull(TEXT("The isolated preview world was created"), World))
            {
                return false;
            }
            GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
            if (!Test.TestNull(TEXT("The fixture has no game instance or signed-in session"), World->GetGameInstance()))
            {
                return false;
            }
            Host.Reset(NewObject<UConstructModScriptHost>(World));
            Source.Reset(NewObject<UCrowdyModScripts>(World));
            FConstructModScriptHostTestAccess::Bind(*Host, Source.Get());
            return Test.TestEqual(TEXT("Production binding subscribes to all five public SDK events"), FConstructModScriptHostTestAccess::SourceBindingCount(*Host, *Source), 5);
        }

        const FConstructScriptCard* Card(const FString& Id) const
        {
            return Host->GetPresentation()->Find(Id);
        }

        bool InitializeVisit(FAutomationTestBase& Test)
        {
            Claims.Reset(NewObject<UConstructClaimsSubsystem>(World));
            Pawn = World->SpawnActor<APawn>();
            Controller = World->SpawnActor<APlayerController>();
            if (!Test.TestNotNull(TEXT("The visit fixture creates a plain pawn"), Pawn) || !Test.TestNotNull(TEXT("The visit fixture creates a plain controller"), Controller))
            {
                return false;
            }
            if (!Pawn->GetRootComponent())
            {
                USceneComponent* Root = NewObject<USceneComponent>(Pawn);
                Pawn->SetRootComponent(Root);
                Root->RegisterComponent();
            }
            Controller->Possess(Pawn);
            if (!Test.TestTrue(TEXT("The plain controller possesses the visit pawn"), Controller->GetPawn() == Pawn))
            {
                return false;
            }
            FConstructModScriptHostTestAccess::SetVisitDependencies(*Host, Claims.Get(), Controller);
            return true;
        }

        bool MoveToChunk(FInt64Vector Chunk)
        {
            return Pawn->SetActorLocation(UCrowdyVoxelLibrary::VoxelToWorld(World, Chunk.X, Chunk.Y, Chunk.Z, FIntVector::ZeroValue));
        }

        ~FHostSourceFixture()
        {
            if (Host.IsValid())
            {
                FConstructModScriptHostTestAccess::Unbind(*Host);
                World->GetTimerManager().ClearAllTimersForObject(Host.Get());
            }
            Source.Reset();
            Host.Reset();
            Claims.Reset();
            if (World)
            {
                World->DestroyWorld(false);
                GEngine->DestroyWorldContext(World);
            }
        }
    };

    FCrowdyModScript ScriptInput(int32 Generation = 7)
    {
        FCrowdyModScript Script;
        Script.ModId = TEXT("host-event-example");
        Script.Name = TEXT("Host event example");
        Script.AuthorId = 41;
        Script.GridId = 23;
        Script.Generation = Generation;
        Script.Calls = {TEXT("hud_set"), TEXT("overlay_draw")};
        return Script;
    }

    FCrowdyModScriptPrompt PromptInput(int32 Id = 17)
    {
        FCrowdyModScriptPrompt Prompt;
        Prompt.PromptId = Id;
        Prompt.AuthorId = 41;
        Prompt.GridId = 23;
        Prompt.Scripts = {TEXT("Host event example")};
        Prompt.Calls = {TEXT("hud_set"), TEXT("actor_spawn")};
        Prompt.Groups = {ECrowdyModScriptGroup::Present};
        Prompt.NotOffered = {TEXT("actor_spawn")};
        return Prompt;
    }

    FCrowdyClaimSearch ClaimedResult(FInt64Vector Chunk)
    {
        FCrowdyClaimSearch Result;
        Result.Result = ECrowdyClaimResult::Success;
        FCrowdyChunkClaimInfo& Claim = Result.Claims.AddDefaulted_GetRef();
        Claim.GridId = 23;
        Claim.ChunkX = Chunk.X;
        Claim.ChunkY = Chunk.Y;
        Claim.ChunkZ = Chunk.Z;
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostSourcePromptTest, "Construct.ModScripts.Host.Source.PublicPromptLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostSourcePromptTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    const FCrowdyModScriptPrompt Input = PromptInput();
    Fixture.Source->OnModScriptPrompt.Broadcast(Input);
    const FConstructScriptConsent* Prompt = Fixture.Host->GetPresentation()->GetPrompt();
    if (!TestNotNull(TEXT("The public SDK prompt delegate reaches production presentation"), Prompt))
    {
        return false;
    }
    TestEqual(TEXT("The host preserves the SDK request id"), Prompt->RequestId, Input.PromptId);
    TestEqual(TEXT("The host displays the supplied script name"), Prompt->Scripts.ToString(), Input.Scripts[0]);
    TestEqual(TEXT("The supplied supported capability is described"), Prompt->Calls.Num(), 1);
    TestEqual(TEXT("The supplied refused capability is described separately"), Prompt->NotOffered.Num(), 1);
    TestFalse(TEXT("The prompt starts answerable"), Prompt->bPending);
    TestTrue(TEXT("Run is accepted by the bound presentation callback"), Fixture.Host->GetPresentation()->Answer(Input.PromptId, true));
    TestNull(TEXT("The host consumes a submitted dialog without waiting for SDK consent"), Fixture.Host->GetPresentation()->GetPrompt());
    TestTrue(TEXT("A submitted answer alone does not manufacture a running card"), Fixture.Host->GetPresentation()->GetCards().IsEmpty());
    TestFalse(TEXT("The consumed request cannot be submitted twice"), Fixture.Host->GetPresentation()->Answer(Input.PromptId, true));
    Fixture.Source->OnModScriptPrompt.Broadcast(PromptInput(18));
    TestTrue(TEXT("Not now follows the same bound callback"), Fixture.Host->GetPresentation()->Answer(18, false));
    TestNull(TEXT("The host consumes the declined dialog"), Fixture.Host->GetPresentation()->GetPrompt());
    Fixture.Source->OnModScriptPrompt.Broadcast(PromptInput(19));
    Fixture.Source->OnModScriptPromptWithdrawn.Broadcast(18);
    Prompt = Fixture.Host->GetPresentation()->GetPrompt();
    if (!TestNotNull(TEXT("Withdrawal of an old id preserves the current prompt"), Prompt))
    {
        return false;
    }
    TestEqual(TEXT("The current prompt keeps its identity"), Prompt->RequestId, 19);
    Fixture.Source->OnModScriptPromptWithdrawn.Broadcast(19);
    TestNull(TEXT("The matching public withdrawal closes the prompt"), Fixture.Host->GetPresentation()->GetPrompt());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostSourceLifecycleTest, "Construct.ModScripts.Host.Source.PublicEventsAndTypedHostCalls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostSourceLifecycleTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    FCrowdyModScript Script = ScriptInput();
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    const FConstructScriptCard* Card = Fixture.Card(Script.ModId);
    if (!TestNotNull(TEXT("The bound public start event creates its labelled card"), Card))
    {
        return false;
    }
    TestEqual(TEXT("The card preserves the supplied author"), Card->Label.AuthorId, Script.AuthorId);
    TestEqual(TEXT("The card preserves the supplied generation"), Card->Label.Generation, int64(Script.Generation));
    ICrowdyModScriptHost::Execute_ShowHud(Fixture.Host.Get(), Script, TEXT("Typed host HUD"));
    TestEqual(TEXT("The public host interface updates the same card"), Fixture.Card(Script.ModId)->Text, FString(TEXT("Typed host HUD")));
    UConstructModScriptOverlay* Overlay = Fixture.World->GetSubsystem<UConstructModScriptOverlay>();
    if (!TestNotNull(TEXT("The preview world owns its local overlay renderer"), Overlay))
    {
        return false;
    }
    FCrowdyModScriptShape Shape;
    Shape.Id = TEXT("event-shape");
    ICrowdyModScriptHost::Execute_DrawOverlay(Fixture.Host.Get(), Script, {Shape});
    TestEqual(TEXT("The typed host call draws its local shape"), Overlay->Count(Script.ModId), 1);
    Fixture.Source->OnModScriptStopped.Broadcast(Script, ECrowdyModScriptStop::LeftGrid);
    TestFalse(TEXT("The bound public stop event marks the card stopped"), Fixture.Card(Script.ModId)->bRunning);
    TestTrue(TEXT("The stop event removes HUD text"), Fixture.Card(Script.ModId)->Text.IsEmpty());
    const FString StopReason = Fixture.Card(Script.ModId)->StopReason.ToString();
    TestFalse(TEXT("The SDK stop reason has a player-facing caption"), StopReason.IsEmpty());
    ICrowdyModScriptHost::Execute_ClearScript(Fixture.Host.Get(), Script);
    TestEqual(TEXT("The typed clear removes its overlay"), Overlay->Count(Script.ModId), 0);
    TestEqual(TEXT("The typed clear preserves the public stop reason"), Fixture.Card(Script.ModId)->StopReason.ToString(), StopReason);
    const FCrowdyModScript Old = Script;
    ++Script.Generation;
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    ICrowdyModScriptHost::Execute_ShowHud(Fixture.Host.Get(), Script, TEXT("New generation"));
    Fixture.Source->OnModScriptStopped.Broadcast(Old, ECrowdyModScriptStop::Revoked);
    ICrowdyModScriptHost::Execute_ClearScript(Fixture.Host.Get(), Old);
    ICrowdyModScriptHost::Execute_ShowHud(Fixture.Host.Get(), Old, TEXT("Stale generation"));
    TestTrue(TEXT("An older event and host calls cannot stop the newer card"), Fixture.Card(Script.ModId)->bRunning);
    TestEqual(TEXT("The newer HUD survives stale typed inputs"), Fixture.Card(Script.ModId)->Text, FString(TEXT("New generation")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostSourceRequestsTest, "Construct.ModScripts.Host.Source.PublicErrorsAndPendingRequests", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostSourceRequestsTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    const FCrowdyModScript First = ScriptInput();
    FCrowdyModScript Second = First;
    Second.ModId = TEXT("other-host-event-example");
    Second.Name = TEXT("Other host event example");
    Fixture.Source->OnModScriptStarted.Broadcast(First);
    Fixture.Source->OnModScriptStarted.Broadcast(Second);
    TestTrue(TEXT("The bound revoke callback accepts a displayed running script"), Fixture.Host->GetPresentation()->Revoke(First.ModId));
    TestTrue(TEXT("A request alone leaves its card running"), Fixture.Card(First.ModId)->bRunning);
    TestTrue(TEXT("The requested revoke waits for a source result"), Fixture.Card(First.ModId)->bPending);
    TestFalse(TEXT("Another script has no pending request"), Fixture.Card(Second.ModId)->bPending);
    FCrowdyModScriptError Error;
    Error.ModId = First.ModId;
    Error.Code = TEXT("refused");
    Error.Message = TEXT("The request was refused.");
    Fixture.Source->OnModScriptError.Broadcast(Error);
    TestFalse(TEXT("The public refusal releases the matching pending state"), Fixture.Card(First.ModId)->bPending);
    TestTrue(TEXT("The refusal notice names its supplied script"), Fixture.Host->GetPresentation()->GetNotice().ToString().Contains(First.Name));
    TestTrue(TEXT("The refusal notice carries the source message"), Fixture.Host->GetPresentation()->GetNotice().ToString().Contains(Error.Message));
    TestTrue(TEXT("The bound forget callback accepts the displayed author"), Fixture.Host->GetPresentation()->ForgetAuthor(First.AuthorId));
    TestTrue(TEXT("Forget marks both of the author's cards pending"), Fixture.Card(First.ModId)->bPending && Fixture.Card(Second.ModId)->bPending);
    Fixture.Source->OnModScriptStopped.Broadcast(First, ECrowdyModScriptStop::Revoked);
    Fixture.Source->OnModScriptStopped.Broadcast(Second, ECrowdyModScriptStop::Revoked);
    TestFalse(TEXT("Public stop events release pending state"), Fixture.Card(First.ModId)->bPending || Fixture.Card(Second.ModId)->bPending);
    TestFalse(TEXT("Public stop events stop both displayed cards"), Fixture.Card(First.ModId)->bRunning || Fixture.Card(Second.ModId)->bRunning);
    Error.Message.Reset();
    Fixture.Source->OnModScriptError.Broadcast(Error);
    TestTrue(TEXT("An empty source message falls back to its code"), Fixture.Host->GetPresentation()->GetNotice().ToString().Contains(Error.Code));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostSourceUnbindTest, "Construct.ModScripts.Host.Source.UnbindAndRebind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostSourceUnbindTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    FCrowdyModScript Script = ScriptInput();
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    ICrowdyModScriptHost::Execute_ShowHud(Fixture.Host.Get(), Script, TEXT("Before unbinding"));
    FConstructModScriptHostTestAccess::Unbind(*Fixture.Host);
    TestEqual(TEXT("Unbinding removes all five production SDK event subscriptions"), FConstructModScriptHostTestAccess::SourceBindingCount(*Fixture.Host, *Fixture.Source), 0);
    TestFalse(TEXT("Unbinding removes the host's answer callback"), Fixture.Host->GetPresentation()->OnAnswerRequested.IsBound());
    TestFalse(TEXT("Unbinding removes the host's revoke callback"), Fixture.Host->GetPresentation()->OnRevokeRequested.IsBound());
    TestFalse(TEXT("Unbinding removes the host's forget callback"), Fixture.Host->GetPresentation()->OnForgetRequested.IsBound());
    TestTrue(TEXT("Unbinding removes source context"), Fixture.Host->GetPresentation()->GetContext().IsEmpty());
    int32 Changes = 0;
    const FDelegateHandle Handle = Fixture.Host->GetPresentation()->OnChanged.AddLambda([&Changes]()
    {
        ++Changes;
    });
    ++Script.Generation;
    Fixture.Source->OnModScriptPrompt.Broadcast(PromptInput());
    Fixture.Source->OnModScriptPromptWithdrawn.Broadcast(17);
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    Fixture.Source->OnModScriptStopped.Broadcast(Script, ECrowdyModScriptStop::Failed);
    FCrowdyModScriptError Error;
    Error.ModId = Script.ModId;
    Error.Message = TEXT("Detached source error");
    Fixture.Source->OnModScriptError.Broadcast(Error);
    TestEqual(TEXT("Detached public source events cause no presentation updates"), Changes, 0);
    TestEqual(TEXT("Detached events cannot replace the previously displayed generation"), Fixture.Card(Script.ModId)->Label.Generation, int64(Script.Generation - 1));
    TestEqual(TEXT("Detached events leave the prior host text unchanged"), Fixture.Card(Script.ModId)->Text, FString(TEXT("Before unbinding")));
    TestNull(TEXT("A detached prompt never opens a dialog"), Fixture.Host->GetPresentation()->GetPrompt());
    FConstructModScriptHostTestAccess::Bind(*Fixture.Host, Fixture.Source.Get());
    FConstructModScriptHostTestAccess::Bind(*Fixture.Host, Fixture.Source.Get());
    TestEqual(TEXT("Rebinding restores all public source subscriptions"), FConstructModScriptHostTestAccess::SourceBindingCount(*Fixture.Host, *Fixture.Source), 5);
    Changes = 0;
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    TestEqual(TEXT("Binding the same source twice consumes a start only once"), Changes, 1);
    TestEqual(TEXT("A rebound event reaches the displayed card"), Fixture.Card(Script.ModId)->Label.Generation, int64(Script.Generation));
    Fixture.Host->GetPresentation()->OnChanged.Remove(Handle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostSourceLateRefusalTest, "Construct.ModScripts.Host.Source.LateEditRefusalOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostSourceLateRefusalTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    const FCrowdyModScript Script = ScriptInput();
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    FConstructModScriptHostTestAccess::RetainEdit(*Fixture.Host, 1, Script);
    Fixture.Source->OnModScriptStopped.Broadcast(Script, ECrowdyModScriptStop::LeftGrid);
    ICrowdyModScriptHost::Execute_ClearScript(Fixture.Host.Get(), Script);
    TestNotNull(TEXT("The stopped script's submitted edit retains its display identity"), FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 1));
    const FCrowdyVoxelWrite Write;
    FConstructModScriptHostTestAccess::Complete(*Fixture.Host, 1, Write, ECrowdyVoxelResult::Refused, TEXT("The block edit was refused."));
    const FString FirstNotice = Fixture.Host->GetPresentation()->GetNotice().ToString();
    TestTrue(TEXT("The late refusal names the stopped script"), FirstNotice.Contains(Script.Name));
    TestTrue(TEXT("The late refusal includes the completion's reason"), FirstNotice.Contains(TEXT("The block edit was refused.")));
    TestNull(TEXT("The completed edit releases pending feedback"), FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 1));
    int32 Changes = 0;
    const FDelegateHandle Handle = Fixture.Host->GetPresentation()->OnHudChanged.AddLambda([&Changes]()
    {
        ++Changes;
    });
    FConstructModScriptHostTestAccess::Complete(*Fixture.Host, 1, Write, ECrowdyVoxelResult::Refused, TEXT("Duplicate completion"));
    TestEqual(TEXT("A duplicate completion emits no second HUD update"), Changes, 0);
    TestEqual(TEXT("A duplicate completion preserves the original feedback"), Fixture.Host->GetPresentation()->GetNotice().ToString(), FirstNotice);
    FConstructModScriptHostTestAccess::RetainEdit(*Fixture.Host, 2, Script);
    FConstructModScriptHostTestAccess::Complete(*Fixture.Host, 2, Write, ECrowdyVoxelResult::Ok, FString());
    TestNull(TEXT("A successful completion also releases its label"), FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 2));
    TestEqual(TEXT("A successful completion produces no refusal feedback"), Changes, 0);
    Fixture.Host->GetPresentation()->OnHudChanged.Remove(Handle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostSourceSessionTest, "Construct.ModScripts.Host.Source.SessionCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostSourceSessionTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this) || !Fixture.InitializeVisit(*this))
    {
        return false;
    }
    Fixture.Claims->BeginChunkLookup(FInt64Vector::ZeroValue);
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestTrue(TEXT("The host has visit bookkeeping before session cleanup"), FConstructModScriptHostTestAccess::HasChunk(*Fixture.Host));
    TestTrue(TEXT("The host has a controller before session cleanup"), FConstructModScriptHostTestAccess::HasController(*Fixture.Host));
    const FCrowdyModScript Script = ScriptInput();
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    Fixture.Source->OnModScriptPrompt.Broadcast(PromptInput());
    ICrowdyModScriptHost::Execute_ShowHud(Fixture.Host.Get(), Script, TEXT("Session HUD"));
    FConstructModScriptHostTestAccess::RetainEdit(*Fixture.Host, 1, Script);
    FConstructModScriptHostTestAccess::SignedOut(*Fixture.Host, false);
    TestNotNull(TEXT("An unsuccessful sign-out does not erase the current card"), Fixture.Card(Script.ModId));
    TestNotNull(TEXT("An unsuccessful sign-out preserves pending edit feedback"), FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 1));
    FConstructModScriptHostTestAccess::SignedOut(*Fixture.Host, true);
    TestTrue(TEXT("A successful sign-out removes presentation cards"), Fixture.Host->GetPresentation()->GetCards().IsEmpty());
    TestNull(TEXT("A successful sign-out removes consent dialogs"), Fixture.Host->GetPresentation()->GetPrompt());
    TestFalse(TEXT("A successful sign-out releases the player controller"), FConstructModScriptHostTestAccess::HasController(*Fixture.Host));
    TestFalse(TEXT("A successful sign-out releases host visit state"), FConstructModScriptHostTestAccess::HasChunk(*Fixture.Host));
    TestNull(TEXT("A successful sign-out releases submitted edit identity"), FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 1));
    FConstructModScriptHostTestAccess::Complete(*Fixture.Host, 1, FCrowdyVoxelWrite(), ECrowdyVoxelResult::Refused, TEXT("Old session refusal"));
    TestTrue(TEXT("An old session's edit result produces no new notice"), Fixture.Host->GetPresentation()->GetNotice().IsEmpty());
    Fixture.Source->OnModScriptStarted.Broadcast(Script);
    Fixture.Source->OnModScriptPrompt.Broadcast(PromptInput(18));
    FConstructModScriptHostTestAccess::RetainEdit(*Fixture.Host, 2, Script);
    FConstructModScriptHostTestAccess::AccessEnded(*Fixture.Host);
    TestTrue(TEXT("Access end also removes cards"), Fixture.Host->GetPresentation()->GetCards().IsEmpty());
    TestNull(TEXT("Access end also removes prompts"), Fixture.Host->GetPresentation()->GetPrompt());
    TestNull(TEXT("Access end releases pending edit identity"), FConstructModScriptHostTestAccess::SubmittedEdit(*Fixture.Host, 2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostClaimVisitStateTest, "Construct.ModScripts.Host.Source.ClaimLookupVisitStates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostClaimVisitStateTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this) || !Fixture.InitializeVisit(*this))
    {
        return false;
    }
    // Inspect only the host's lookup bookkeeping. An uninitialized SDK does not supply
    // a grid or running snapshot, and these checks make no claim about entering one.
    const FInt64Vector Chunk(0, 0, 0);
    TestTrue(TEXT("The pawn is placed in the chunk being checked"), Fixture.MoveToChunk(Chunk));
    uint64 Request = Fixture.Claims->BeginChunkLookup(Chunk);
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("A pending ownership lookup is not applied"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), uint64(0));
    TestEqual(TEXT("The pending lookup remains the only request"), Fixture.Claims->GetLookupSequence(), Request);
    Fixture.Claims->FinishChunkLookup(Chunk, Request, ClaimedResult(Chunk));
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("The host applies the current successful claim lookup"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), Request);
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("A repeated visit update retains the applied request"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), Request);
    Request = Fixture.Claims->BeginChunkLookup(Chunk);
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("A pending refresh invalidates the applied lookup"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), uint64(0));
    FCrowdyClaimSearch Failure;
    Failure.Result = ECrowdyClaimResult::Failed;
    Failure.Message = TEXT("The ownership lookup did not complete.");
    Fixture.Claims->FinishChunkLookup(Chunk, Request, Failure);
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("A failed refresh is not applied"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), uint64(0));
    TestEqual(TEXT("Without a game instance a retry cannot create an SDK request"), Fixture.Claims->GetLookupSequence(), Request);
    Request = Fixture.Claims->BeginChunkLookup(Chunk);
    FCrowdyClaimSearch Unclaimed;
    Unclaimed.Result = ECrowdyClaimResult::Success;
    Fixture.Claims->FinishChunkLookup(Chunk, Request, Unclaimed);
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("A successful unclaimed lookup is consumed as a current result"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), Request);
    Fixture.Controller->UnPossess();
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestFalse(TEXT("Losing the pawn clears host visit state"), FConstructModScriptHostTestAccess::HasChunk(*Fixture.Host));
    TestEqual(TEXT("Losing the pawn releases the applied lookup"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), uint64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostClaimVisitMovementTest, "Construct.ModScripts.Host.Source.ClaimLookupMovementCurrency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostClaimVisitMovementTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this) || !Fixture.InitializeVisit(*this))
    {
        return false;
    }
    const FInt64Vector FirstChunk(0, 0, 0);
    const FInt64Vector NextChunk(10, 0, 0);
    TestTrue(TEXT("The pawn begins in the first chunk"), Fixture.MoveToChunk(FirstChunk));
    const uint64 Initial = Fixture.Claims->BeginChunkLookup(FirstChunk);
    Fixture.Claims->FinishChunkLookup(FirstChunk, Initial, ClaimedResult(FirstChunk));
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("The first current claim lookup is applied"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), Initial);
    const uint64 DelayedFirst = Fixture.Claims->BeginChunkLookup(FirstChunk);
    const uint64 PendingNext = Fixture.Claims->BeginChunkLookup(NextChunk);
    TestTrue(TEXT("The pawn moves into the next chunk"), Fixture.MoveToChunk(NextChunk));
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestTrue(TEXT("The host now tracks the pawn's current chunk"), FConstructModScriptHostTestAccess::VisitChunk(*Fixture.Host) == NextChunk);
    TestEqual(TEXT("Movement invalidates the previous chunk's applied result"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), uint64(0));
    Fixture.Claims->FinishChunkLookup(FirstChunk, DelayedFirst, ClaimedResult(FirstChunk));
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("A delayed result for the old chunk is not applied to the current visit"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), uint64(0));
    TestEqual(TEXT("The pending current lookup causes no duplicate request"), Fixture.Claims->GetLookupSequence(), PendingNext);
    Fixture.Claims->FinishChunkLookup(NextChunk, PendingNext, ClaimedResult(NextChunk));
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("Only the current chunk's successful reply is applied"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), PendingNext);
    const uint64 OldFailureRequest = Fixture.Claims->BeginChunkLookup(FirstChunk);
    FCrowdyClaimSearch OldFailure;
    OldFailure.Result = ECrowdyClaimResult::Failed;
    Fixture.Claims->FinishChunkLookup(FirstChunk, OldFailureRequest, OldFailure);
    FConstructModScriptHostTestAccess::UpdateVisit(*Fixture.Host);
    TestEqual(TEXT("An old chunk's later failure does not replace the current applied lookup"), FConstructModScriptHostTestAccess::AppliedLookup(*Fixture.Host), PendingNext);
    TestTrue(TEXT("An old chunk's failure does not move the host's tracked chunk"), FConstructModScriptHostTestAccess::VisitChunk(*Fixture.Host) == NextChunk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHostPendingEntryTest, "Construct.ModScripts.Host.Source.PendingEntryCancellation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHostPendingEntryTest::RunTest(const FString& Parameters)
{
    FHostSourceFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }
    const FInt64Vector Previous(2, 0, 0);
    const FInt64Vector Current(3, 0, 0);
    FCrowdyModScriptGrid Grid;
    FConstructModScriptHostTestAccess::PrepareVisit(*Fixture.Host, Previous, 17);
    TestTrue(TEXT("Movement cancels an unresolved SDK entry even if the next claim is confirmed"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, Current, true));
    TestTrue(TEXT("Movement into a pending claim cancels the unresolved entry"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, Current, false));
    TestTrue(TEXT("A pending or failed claim refresh cancels an unresolved entry in place"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, Previous, false));
    TestFalse(TEXT("A current confirmed claim preserves its unresolved entry in place"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, Previous, true));
    Grid.GridId = 23;
    Grid.LowChunkX = 1;
    Grid.HighChunkX = 5;
    TestFalse(TEXT("Movement inside a resolved grid with a current claim preserves its scripts"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, Current, true));
    TestTrue(TEXT("A pending claim also cancels a resolved grid"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, Current, false));
    TestTrue(TEXT("Leaving the resolved grid cancels before entering another one"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, FInt64Vector(6, 0, 0), true));
    FConstructModScriptHostTestAccess::PrepareVisit(*Fixture.Host, Previous, 0);
    Grid = FCrowdyModScriptGrid();
    TestFalse(TEXT("An empty grid with no issued entry needs no repeated cancellation"), FConstructModScriptHostTestAccess::ShouldCancelVisit(*Fixture.Host, Grid, Current, false));
    return true;
}
#endif