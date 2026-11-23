#include "ConstructModPanel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructModMenu.h"
#include "ConstructModSources.h"
#include "ConstructModsTestReceiver.h"
#include "ConstructClaimPad.h"
#include "ConstructClaimsSubsystem.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "CrowdyServerObjectTypes.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

// Feed real SDK answer structs to the same guarded acceptance handlers as the live adapters.
// No SDK classes, transports or private SDK state are replaced.
struct FConstructModsTestAccess
{
    static void Arm(UConstructModPanel& Panel, UWorld* World, EConstructModPanelState State)
    {
        Panel.CloseBinding();
        Panel.BoundWorld = World;
        Panel.bBound = true;
        Panel.GridId = 900000000000000001LL;
        Panel.UserId = 7;
        Panel.State = Panel.StableBefore = State;
        Panel.bStatusPending = false;
        Panel.bListKnown = State != EConstructModPanelState::Loading;
    }
    static uint64 Binding(const UConstructModPanel& Panel) { return Panel.Binding; }
    static uint64 Mutation(const UConstructModPanel& Panel) { return Panel.Mutation; }
    static bool Begin(UConstructModPanel& Panel, int32 Kind) { return Panel.BeginMutation(static_cast<UConstructModPanel::EMutation>(Kind)); }
    static void Close(UConstructModPanel& Panel) { Panel.CloseBinding(); }
    static void Sources(UConstructModPanel& Panel, const TArray<FCrowdyModFile>& Files) { Panel.FreezeSources(Files); }
    static const TArray<FCrowdyModFile>& Files(const UConstructModPanel& Panel) { return Panel.SubmittedFiles; }
    static void Build(UConstructModPanel& Panel, uint64 Binding, uint64 Mutation, const FCrowdyModBuild& Reply) { Panel.BuildReturned(Binding, Mutation, Reply); }
    static void Progress(UConstructModPanel& Panel, const FCrowdyModBuild& Reply) { Panel.BuildProgress(Panel.Binding, Panel.Mutation, Reply); }
    static void Deploy(UConstructModPanel& Panel, const FCrowdyModInfo& Reply) { Panel.DeployReturned(Panel.Binding, Panel.Mutation, Reply); }
    static bool Enable(UConstructModPanel& Panel, const FCrowdyModInfo& Reply, bool bEnabled = true) { return Panel.EnabledReturned(Panel.Binding, Panel.Mutation, bEnabled, Reply); }
    static void Wake(UConstructModPanel& Panel, const FCrowdyModCall& Reply) { Panel.WakeReturned(Panel.Binding, Panel.Mutation, Reply); }
    static void Delete(UConstructModPanel& Panel, const FCrowdyModDelete& Reply) { Panel.DeleteReturned(Panel.Binding, Panel.Mutation, Reply); }
    static void Status(UConstructModPanel& Panel, uint64 Binding, uint64 Revision, const FCrowdyModList& Reply)
    {
        Panel.StatusReturned(Binding, Panel.StatusRequest, Revision, Reply);
    }
    static uint64 Revision(const UConstructModPanel& Panel) { return Panel.StateRevision; }
    static void Apply(UConstructModPanel& Panel, const FCrowdyMod& Mod) { Panel.ApplyMod(Mod); }
    static void Starter(UConstructModPanel& Panel, const FCrowdyModStarter& Reply) { Panel.StarterReturned(Panel.Binding, Reply); }
    static void Select(UConstructModPanel& Panel, const FString& Key) { Panel.SelectStarter(Key); }
    static FString Name(const UConstructModPanel& Panel) { return Panel.ModName; }
    static FString Method(const UConstructModPanel& Panel) { return Panel.WakeMethod(); }
    static FString Notice(const UConstructModPanel& Panel) { return Panel.Notice; }
    static void Error(UConstructModPanel& Panel, bool bRestore) { Panel.ShowError(ECrowdyModResult::Denied, TEXT("test refusal"), bRestore); }
    static TArray<bool> Buttons(const UConstructModPanel& Panel)
    {
        return { Panel.CanBuild(), Panel.CanDeploy(), Panel.CanToggle(true), Panel.CanToggle(false), Panel.CanLog(), Panel.CanDelete(), Panel.CanRefresh() };
    }
    static void HasBuild(UConstructModPanel& Panel) { Panel.LastBuildId = TEXT("build-id"); }
    static FString Caption(UConstructModPanel& Panel, double Seconds)
    {
        Panel.BuildStartedAt = FPlatformTime::Seconds() - Seconds;
        return Panel.Caption().ToString();
    }
    static bool Busy(const UConstructModPanel& Panel) { return Panel.MutationKind != UConstructModPanel::EMutation::None; }
    static void Logs(UConstructModPanel& Panel, uint64 Binding, const FCrowdyModLogs& Reply)
    {
        Panel.bLogPending = true;
        Panel.LogReturned(Binding, Panel.LogRequest, Reply);
    }
    static FString Log(const UConstructModPanel& Panel) { return Panel.LogText; }
    static void Menu(UConstructModMenu& Menu, UWorld* World)
    {
        Menu.bOpen = true;
        Menu.BoundWorld = World;
        Menu.UserId = 7;
        ++Menu.Generation;
        Menu.ObserveClaims(NewObject<UConstructClaimsSubsystem>(World));
    }
    static void Candidates(UConstructModMenu& Menu, const TArray<FInt64Vector>& Pads, const TArray<FConstructHubClaim>& Hub)
    {
        Menu.PadChunks = Pads;
        Menu.HubRows = Hub;
        Menu.RebuildCandidates();
    }
    static const TArray<FConstructModLand>& Rows(const UConstructModMenu& Menu) { return Menu.Rows; }
    static int32 Omitted(const UConstructModMenu& Menu) { return Menu.Omitted; }
    static void Pending(UConstructModPanel& Panel, bool bStatus, bool bReload)
    {
        Panel.bStatusPending = bStatus;
        Panel.bReloadPending = bReload;
    }
    static void BindPanel(UConstructModMenu& Menu, UConstructModPanel* Panel)
    {
        Menu.Panel = Panel;
        Menu.OpenGrid = Panel->GridId;
    }
    static bool CanSelect(const UConstructModMenu& Menu, int64 Grid) { return Menu.CanSelectGrid(Grid); }
    static void HubReply(UConstructModMenu& Menu, uint64 Generation, uint64 Attempt, const FCrowdyServerCallResult& Reply) { Menu.HubReturned(Generation, Attempt, Reply); }
    static uint64 Attempt(const UConstructModMenu& Menu) { return Menu.HubAttempt; }
    static FString HubError(const UConstructModMenu& Menu) { return Menu.HubError; }
    static double Retry(UConstructModMenu& Menu) { return Menu.GetWorld()->GetTimerManager().GetTimerRemaining(Menu.RetryTimer); }
    static uint64 Generation(const UConstructModMenu& Menu) { return Menu.Generation; }
    static void Confirm(UConstructModMenu& Menu, FInt64Vector Chunk, uint64 Generation, uint64 Request, const FCrowdyClaimSearch& Reply)
    {
        if (UConstructClaimsSubsystem* Owner = Menu.Confirmations.Get())
        {
            Owner->FinishChunkLookup(Chunk, Request, Reply);
        }
    }
    static void Issue(UConstructModMenu& Menu, FInt64Vector Chunk, uint64 Request)
    {
        Menu.Confirmations->Serial = FMath::Max(Menu.Confirmations->Serial, Request);
        Menu.Confirmations->MarkLookup(Chunk, Request);
        Menu.ConfirmRequests.Add(Chunk, Request);
    }
    static uint64 SharedLookup(UConstructModMenu& Menu, FInt64Vector Chunk, bool bMenu)
    {
        const uint64 Request = Menu.Confirmations->BeginChunkLookup(Chunk);
        if (bMenu)
        {
            Menu.ConfirmRequests.Add(Chunk, Request);
        }
        return Request;
    }
    static void SharedAnswer(UConstructModMenu& Menu, FInt64Vector Chunk, uint64 Request, const FCrowdyClaimSearch& Reply)
    {
        Menu.Confirmations->FinishChunkLookup(Chunk, Request, Reply);
    }
    static UConstructClaimsSubsystem* Owner(UConstructModMenu& Menu) { return Menu.Confirmations.Get(); }
    static void Discover(UConstructModMenu& Menu) { Menu.Discover(); }
    static bool FreshPad(const AConstructClaimPad& Pad) { return Pad.bLatestSearchSucceeded; }
    static void HubFailure(UConstructModMenu& Menu) { Menu.HubFailed(TEXT("registry refused")); }
    static bool Current(const UConstructModMenu& Menu, uint64 Generation) { return Menu.IsCurrent(Generation); }
};

namespace
{
    struct FModsFixture
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
        UConstructModPanel* Panel = NewObject<UConstructModPanel>(World);
        UConstructModsTestReceiver* Receiver = NewObject<UConstructModsTestReceiver>(World);
        FModsFixture()
        {
            World->AddToRoot();
            Receiver->Panel = Panel;
            Panel->OnBuildDone.AddDynamic(Receiver, &UConstructModsTestReceiver::Built);
            Panel->OnEnabledChanged.AddDynamic(Receiver, &UConstructModsTestReceiver::Enabled);
            Panel->OnWakeDone.AddDynamic(Receiver, &UConstructModsTestReceiver::Woke);
            Panel->OnLogShown.AddDynamic(Receiver, &UConstructModsTestReceiver::Logs);
            Panel->OnDeleted.AddDynamic(Receiver, &UConstructModsTestReceiver::Deleted);
        }
        ~FModsFixture()
        {
            FConstructModsTestAccess::Close(*Panel);
            World->DestroyWorld(false);
            World->RemoveFromRoot();
        }
        void Arm(EConstructModPanelState State)
        {
            FConstructModsTestAccess::Arm(*Panel, World, State);
        }
    };

    FCrowdyMod AMod(bool bEnabled = false)
    {
        FCrowdyMod Mod;
        Mod.GridId = 900000000000000001LL;
        Mod.OwnerUserId = 7;
        Mod.Name = TEXT("construct-spinning-child");
        Mod.bEnabled = bEnabled;
        return Mod;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsButtonsTest, "Construct.Mods.StateButtonsAndNoOps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsButtonsTest::RunTest(const FString&)
{
    FModsFixture F;
    const TArray<TArray<bool>> Expected =
    {
        {false,false,false,false,false,false,false}, // Loading
        {true,false,false,false,false,false,true}, // NoMod
        {false,false,false,false,false,false,false}, // Building
        {true,false,false,false,false,false,true}, // BuildFailed
        {false,true,false,false,true,false,true}, // Built
        {false,false,true,false,true,true,true}, // Off
        {false,false,false,true,true,false,true}, // On
        {false,false,false,true,false,true,true}, // Blocked
        {false,false,false,false,false,false,true} // Error
    };
    for (int32 State = 0; State < Expected.Num(); ++State)
    {
        F.Arm(static_cast<EConstructModPanelState>(State));
        FConstructModsTestAccess::HasBuild(*F.Panel);
        const TArray<bool> Actual = FConstructModsTestAccess::Buttons(*F.Panel);
        for (int32 Button = 0; Button < Actual.Num(); ++Button)
        {
            TestEqual(FString::Printf(TEXT("State %d button %d"), State, Button), Actual[Button], Expected[State][Button]);
        }
    }
    F.Arm(EConstructModPanelState::Loading);
    F.Panel->Build(TEXT("construct-spinning-child"));
    F.Panel->Deploy();
    F.Panel->SetEnabled(true);
    F.Panel->SetEnabled(false);
    F.Panel->ShowLog();
    F.Panel->Delete();
    F.Panel->Refresh();
    TestEqual(TEXT("Unavailable calls emit nothing"), F.Receiver->Events.Num(), 0);
    TestEqual(TEXT("Unavailable calls preserve Loading"), F.Panel->GetState(), EConstructModPanelState::Loading);
    F.Arm(EConstructModPanelState::NoMod);
    F.Panel->Build(TEXT("unknown"));
    TestFalse(TEXT("Unknown starter starts no operation"), FConstructModsTestAccess::Busy(*F.Panel));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsBuildTest, "Construct.Mods.BuildCompletionAndFrozenHash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsBuildTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::NoMod);
    FCrowdyModFile Source;
    Source.Path = TEXT("src/lib.rs");
    Source.Content = TEXT("abc");
    TArray<FCrowdyModFile> Files{Source};
    FConstructModsTestAccess::Sources(*F.Panel, Files);
    Files[0].Content = TEXT("edited after submission");
    TestEqual(TEXT("Frozen payload retained"), FConstructModsTestAccess::Files(*F.Panel)[0].Content, FString(TEXT("abc")));
    TestTrue(TEXT("Build admitted"), FConstructModsTestAccess::Begin(*F.Panel, 1));
    const uint64 Binding = FConstructModsTestAccess::Binding(*F.Panel);
    const uint64 Mutation = FConstructModsTestAccess::Mutation(*F.Panel);
    FCrowdyModBuild Reply;
    Reply.Result = ECrowdyModResult::Success;
    Reply.Status = ECrowdyModBuildStatus::Building;
    Reply.Log = TEXT("compiler progress");
    FConstructModsTestAccess::Progress(*F.Panel, Reply);
    Reply.Status = ECrowdyModBuildStatus::Succeeded;
    Reply.BuildId = TEXT("opaque-build-id");
    FConstructModsTestAccess::Build(*F.Panel, Binding, Mutation, Reply);
    TestEqual(TEXT("Successful state"), F.Panel->GetState(), EConstructModPanelState::Built);
    TestEqual(TEXT("ID visible inside event"), F.Receiver->BuildId, Reply.BuildId);
    TestEqual(TEXT("Hash snapshot visible inside event"), F.Receiver->Hashes.Num(), 1);
    TestEqual(TEXT("Exact UTF8 bytes without null"), F.Receiver->Hashes[0].Sha1, FString(TEXT("a9993e364706816aba3e25717850c26c9cd0d89d")));
    FConstructModsTestAccess::Build(*F.Panel, Binding, Mutation, Reply);
    TestEqual(TEXT("Duplicate done ignored"), F.Receiver->Events.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsBuildFailuresTest, "Construct.Mods.BuildFailuresAndElapsedTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsBuildFailuresTest::RunTest(const FString&)
{
    FModsFixture F;
    for (const ECrowdyModResult Result : {ECrowdyModResult::Success, ECrowdyModResult::Failed, ECrowdyModResult::Refused})
    {
        F.Arm(EConstructModPanelState::NoMod);
        FConstructModsTestAccess::Begin(*F.Panel, 1);
        FCrowdyModBuild Reply;
        Reply.Result = Result;
        Reply.Status = ECrowdyModBuildStatus::Failed;
        Reply.Message = TEXT("compiler, request timeout or source refusal");
        FConstructModsTestAccess::Build(*F.Panel, FConstructModsTestAccess::Binding(*F.Panel), FConstructModsTestAccess::Mutation(*F.Panel), Reply);
        TestEqual(TEXT("Every failed completion leaves Building"), F.Panel->GetState(), EConstructModPanelState::BuildFailed);
    }
    F.Arm(EConstructModPanelState::Building);
    TestTrue(TEXT("Elapsed seconds shown"), FConstructModsTestAccess::Caption(*F.Panel, 17.1).Contains(TEXT("17")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsWakeTest, "Construct.Mods.ManualEnableWakeAndRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsWakeTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::Off);
    FConstructModsTestAccess::Begin(*F.Panel, 3);
    FCrowdyModInfo Enabled;
    Enabled.Result = ECrowdyModResult::Success;
    Enabled.Mod = AMod(true);
    TestTrue(TEXT("Accepted enable requests one wake"), FConstructModsTestAccess::Enable(*F.Panel, Enabled));
    TestFalse(TEXT("Duplicate enable cannot request another wake"), FConstructModsTestAccess::Enable(*F.Panel, Enabled));
    TestEqual(TEXT("Enable event first"), F.Receiver->Events[0], FString(TEXT("enable")));
    TestTrue(TEXT("Enable and wake remain one operation"), FConstructModsTestAccess::Busy(*F.Panel));
    const auto Buttons = FConstructModsTestAccess::Buttons(*F.Panel);
    for (bool bOffered : Buttons)
    {
        TestFalse(TEXT("Conflicting operation unavailable through wake"), bOffered);
    }
    FCrowdyModCall Wake;
    Wake.Result = ECrowdyModResult::Denied;
    Wake.Message = TEXT("startup refused");
    FConstructModsTestAccess::Wake(*F.Panel, Wake);
    TestEqual(TEXT("Failed wake preserves confirmed On"), F.Panel->GetState(), EConstructModPanelState::On);
    TestTrue(TEXT("Wake reason retained"), FConstructModsTestAccess::Notice(*F.Panel).Contains(Wake.Message));
    TestEqual(TEXT("Wake event second"), F.Receiver->Events[1], FString(TEXT("wake")));
    FConstructModsTestAccess::Wake(*F.Panel, Wake);
    TestEqual(TEXT("Duplicate wake ignored"), F.Receiver->Events.Num(), 2);
    TestFalse(TEXT("Wake terminal releases operation"), FConstructModsTestAccess::Busy(*F.Panel));
    FConstructModsTestAccess::Begin(*F.Panel, 4);
    Enabled.Mod.bEnabled = false;
    TestFalse(TEXT("Off never requests a wake"), FConstructModsTestAccess::Enable(*F.Panel, Enabled, false));
    TestEqual(TEXT("Off is confirmed"), F.Panel->GetState(), EConstructModPanelState::Off);
    FConstructModsTestAccess::Begin(*F.Panel, 3);
    Enabled.Result = ECrowdyModResult::Denied;
    TestFalse(TEXT("Failed enable sends no wake"), FConstructModsTestAccess::Enable(*F.Panel, Enabled));
    TestEqual(TEXT("Enable refusal shows Error"), F.Panel->GetState(), EConstructModPanelState::Error);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(2.1f);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(2.1f);
    TestEqual(TEXT("Failed enable returns to Off"), F.Panel->GetState(), EConstructModPanelState::Off);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsReentrantCloseTest, "Construct.Mods.CloseBetweenEnableAndWake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsReentrantCloseTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::Off);
    FConstructModsTestAccess::Begin(*F.Panel, 3);
    F.Receiver->OnEnabled = [&F]() { FConstructModsTestAccess::Close(*F.Panel); };
    FCrowdyModInfo Reply;
    Reply.Result = ECrowdyModResult::Success;
    Reply.Mod = AMod(true);
    TestFalse(TEXT("Close during event prevents unsent wake"), FConstructModsTestAccess::Enable(*F.Panel, Reply));
    const uint64 Old = FConstructModsTestAccess::Binding(*F.Panel) - 1;
    F.Arm(EConstructModPanelState::Loading);
    FCrowdyModBuild Build;
    Build.Result = ECrowdyModResult::Success;
    Build.Status = ECrowdyModBuildStatus::Succeeded;
    Build.BuildId = TEXT("obsolete");
    FConstructModsTestAccess::Build(*F.Panel, Old, 1, Build);
    TestEqual(TEXT("Reopened binding unaffected by old completion"), F.Panel->GetState(), EConstructModPanelState::Loading);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsStatusTest, "Construct.Mods.StatusOrderingBlockedAndNoAutomaticWake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsStatusTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::Loading);
    FCrowdyModList List;
    List.Result = ECrowdyModResult::Success;
    FConstructModsTestAccess::Status(*F.Panel, FConstructModsTestAccess::Binding(*F.Panel), FConstructModsTestAccess::Revision(*F.Panel), List);
    TestEqual(TEXT("Land with no mods"), F.Panel->GetState(), EConstructModPanelState::NoMod);
    F.Arm(EConstructModPanelState::Off);
    const uint64 Before = FConstructModsTestAccess::Revision(*F.Panel);
    FConstructModsTestAccess::Begin(*F.Panel, 3);
    List.Mods = {AMod(false)};
    FConstructModsTestAccess::Status(*F.Panel, FConstructModsTestAccess::Binding(*F.Panel), Before, List);
    TestTrue(TEXT("Superseded read cannot complete mutation"), FConstructModsTestAccess::Busy(*F.Panel));
    F.Arm(EConstructModPanelState::Loading);
    List.Mods = {AMod(true)};
    List.Mods[0].bBlocked = true;
    List.Mods[0].BlockedReason = TEXT("wallet blocked");
    FConstructModsTestAccess::Status(*F.Panel, FConstructModsTestAccess::Binding(*F.Panel), FConstructModsTestAccess::Revision(*F.Panel), List);
    TestEqual(TEXT("Blocked takes precedence"), F.Panel->GetState(), EConstructModPanelState::Blocked);
    TestTrue(TEXT("Blocked reason shown"), FConstructModsTestAccess::Caption(*F.Panel, 0).Contains(TEXT("wallet blocked")));
    TestEqual(TEXT("Reading On/Blocked never emits enable or wake"), F.Receiver->Events.Num(), 0);
    for (EConstructModPanelState Protected : {EConstructModPanelState::Built, EConstructModPanelState::BuildFailed})
    {
        F.Arm(Protected);
        List.Mods.Reset();
        FConstructModsTestAccess::Status(*F.Panel, FConstructModsTestAccess::Binding(*F.Panel), FConstructModsTestAccess::Revision(*F.Panel), List);
        TestEqual(TEXT("Read preserves undeployed build"), F.Panel->GetState(), Protected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsStartersTest, "Construct.Mods.BothStartersAndFetchFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsStartersTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::NoMod);
    TestEqual(TEXT("Spinner fixed wake method"), FConstructModsTestAccess::Method(*F.Panel), FString(TEXT("state")));
    FConstructModsTestAccess::Select(*F.Panel, TEXT("platform-greeter"));
    FCrowdyModStarter Starter;
    Starter.Result = ECrowdyModResult::Failed;
    Starter.Message = TEXT("starter fetch failed");
    FConstructModsTestAccess::Starter(*F.Panel, Starter);
    TestEqual(TEXT("Fetch failure shown"), F.Panel->GetState(), EConstructModPanelState::Error);
    TestFalse(TEXT("No empty crate build"), FConstructModsTestAccess::Buttons(*F.Panel)[0]);
    Starter.Result = ECrowdyModResult::Success;
    Starter.Name = TEXT("grid-mod");
    FCrowdyModFile File;
    File.Path = TEXT("Cargo.toml");
    File.Content = TEXT("source");
    Starter.Files = {File};
    FConstructModsTestAccess::Starter(*F.Panel, Starter);
    TestEqual(TEXT("Greeter uses platform crate name"), FConstructModsTestAccess::Name(*F.Panel), FString(TEXT("grid-mod")));
    TestEqual(TEXT("Greeter fixed method"), FConstructModsTestAccess::Method(*F.Panel), FString(TEXT("visit")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsErrorsTest, "Construct.Mods.StableErrorRecoveryAndInitialRetry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsErrorsTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::Loading);
    FConstructModsTestAccess::Error(*F.Panel, false);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(3);
    TestEqual(TEXT("Initial error stays visible"), F.Panel->GetState(), EConstructModPanelState::Error);
    TestTrue(TEXT("Initial failure permits Refresh"), FConstructModsTestAccess::Buttons(*F.Panel)[6]);
    F.Arm(EConstructModPanelState::On);
    FConstructModsTestAccess::Error(*F.Panel, true);
    FConstructModsTestAccess::Error(*F.Panel, true);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(3);
    ++GFrameCounter;
    F.World->GetTimerManager().Tick(3);
    TestEqual(TEXT("Repeated failures preserve last stable state"), F.Panel->GetState(), EConstructModPanelState::On);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsLogsDeleteTest, "Construct.Mods.LogsDeleteAndLateReply", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsLogsDeleteTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::Off);
    FCrowdyModLogs Reply;
    Reply.Result = ECrowdyModResult::Success;
    FCrowdyModLogLine First;
    First.AtMs = 10;
    First.Level = 1;
    First.Text = TEXT("oldest");
    FCrowdyModLogLine Last = First;
    Last.AtMs = 20;
    Last.Text = TEXT("newest");
    Reply.Lines = {First, Last};
    const uint64 Binding = FConstructModsTestAccess::Binding(*F.Panel);
    FConstructModsTestAccess::Logs(*F.Panel, Binding, Reply);
    const FString Log = FConstructModsTestAccess::Log(*F.Panel);
    TestTrue(TEXT("SDK oldest-first order retained"), Log.Find(TEXT("oldest")) < Log.Find(TEXT("newest")));
    TestEqual(TEXT("Terminal log count"), F.Receiver->Lines, 2);
    FConstructModsTestAccess::Begin(*F.Panel, 5);
    FCrowdyModDelete Deleted;
    Deleted.Result = ECrowdyModResult::Success;
    FConstructModsTestAccess::Delete(*F.Panel, Deleted);
    TestEqual(TEXT("Delete leaves NoMod"), F.Panel->GetState(), EConstructModPanelState::NoMod);
    FConstructModsTestAccess::Close(*F.Panel);
    FConstructModsTestAccess::Logs(*F.Panel, Binding, Reply);
    TestTrue(TEXT("Late log cannot restore closed data"), FConstructModsTestAccess::Log(*F.Panel).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsLandOrderTest, "Construct.Mods.LandDedupOrderAndCap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsLandOrderTest::RunTest(const FString&)
{
    FModsFixture F;
    UConstructModMenu* Menu = NewObject<UConstructModMenu>(F.World);
    FConstructModsTestAccess::Menu(*Menu, F.World);
    TArray<FConstructHubClaim> Hub;
    for (int32 Index = 70; Index >= 1; --Index)
    {
        FConstructHubClaim Claim;
        Claim.GridId = Index;
        Claim.Chunk.X = Index;
        Hub.Add(Claim);
    }
    FConstructModsTestAccess::Candidates(*Menu, {FInt64Vector(2,0,0), FInt64Vector(-1,0,0)}, Hub);
    const auto& Rows = FConstructModsTestAccess::Rows(*Menu);
    TestEqual(TEXT("Unique capped count"), Rows.Num(), 64);
    TestEqual(TEXT("Pad first lexically"), Rows[0].Chunk.X, int64(-1));
    TestEqual(TEXT("Second pad not duplicated by hub"), Rows[1].Chunk.X, int64(2));
    TestEqual(TEXT("Remaining hub grid-id order"), Rows[2].Chunk.X, int64(1));
    TestEqual(TEXT("Separate omitted candidate count"), FConstructModsTestAccess::Omitted(*Menu), 7);
    TestNull(TEXT("Candidate alone never opens"), Menu->OpenForGrid(1));
    Menu->Close();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsConfirmationTest, "Construct.Mods.FreshOwnershipAndDiscoveryLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsConfirmationTest::RunTest(const FString&)
{
    FModsFixture F;
    UConstructModMenu* Menu = NewObject<UConstructModMenu>(F.World);
    FConstructModsTestAccess::Menu(*Menu, F.World);
    const FInt64Vector Chunk(5,0,0);
    FConstructModsTestAccess::Candidates(*Menu, {Chunk}, {});
    const uint64 Generation = FConstructModsTestAccess::Generation(*Menu);
    FConstructModsTestAccess::Issue(*Menu, Chunk, 1);
    FCrowdyClaimSearch Reply;
    Reply.Result = ECrowdyClaimResult::Success;
    FCrowdyChunkClaimInfo Claim;
    Claim.GridId = 900000000000000001LL;
    Claim.OwnerUserId = 7;
    Claim.bOwnedByMe = true;
    Claim.ChunkX = 5;
    Reply.Claims = {Claim};
    FConstructModsTestAccess::Confirm(*Menu, Chunk, Generation, 1, Reply);
    TestTrue(TEXT("Fresh SDK owner confirms"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    FConstructModsTestAccess::HubFailure(*Menu);
    TestTrue(TEXT("Hub failure retains independently confirmed pad"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    FConstructModsTestAccess::Issue(*Menu, Chunk, 2);
    Reply.Result = ECrowdyClaimResult::Failed;
    Reply.Message = TEXT("denied");
    FConstructModsTestAccess::Confirm(*Menu, Chunk, Generation, 2, Reply);
    TestFalse(TEXT("Latest failure disables row"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    Reply.Result = ECrowdyClaimResult::Success;
    FConstructModsTestAccess::Confirm(*Menu, Chunk, Generation, 1, Reply);
    TestFalse(TEXT("Older success cannot restore row"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    Menu->Close();
    FConstructModsTestAccess::Confirm(*Menu, Chunk, Generation, 2, Reply);
    TestFalse(TEXT("Close invalidates generation"), FConstructModsTestAccess::Current(*Menu, Generation));
    TestTrue(TEXT("Late discovery never reopens rows"), FConstructModsTestAccess::Rows(*Menu).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsSourceTest, "Construct.Mods.SourceUtf8NewlinesAndAllowedFiles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsSourceTest::RunTest(const FString&)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ModsAutomation"), FGuid::NewGuid().ToString());
    IFileManager::Get().MakeDirectory(*FPaths::Combine(Directory, TEXT("src/nested")), true);
    const TArray<uint8> Bytes{0xef,0xbb,0xbf,'a','\r','\n',0xc3,0xa6,'\n'};
    FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(Directory, TEXT("Cargo.toml")));
    FFileHelper::SaveStringToFile(TEXT("abc"), *FPaths::Combine(Directory, TEXT("src/nested/lib.rs")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FFileHelper::SaveStringToFile(TEXT("ignored"), *FPaths::Combine(Directory, TEXT("secret.txt")));
    TArray<FCrowdyModFile> Files;
    FString Error;
    TestTrue(TEXT("Nested source readable"), ConstructModSources::Read(Directory, Files, Error));
    if (!TestEqual(TEXT("Only Cargo and Rust files"), Files.Num(), 2))
    {
        IFileManager::Get().DeleteDirectory(*Directory, false, true);
        return false;
    }
    TestEqual(TEXT("Forward slash nested path"), Files[1].Path, FString(TEXT("src/nested/lib.rs")));
    FTCHARToUTF8 RoundTrip(*Files[0].Content, Files[0].Content.Len());
    TestEqual(TEXT("BOM unicode and newlines retained"), RoundTrip.Length(), Bytes.Num());
    TestTrue(TEXT("Exact original bytes retained"), FMemory::Memcmp(RoundTrip.Get(), Bytes.GetData(), Bytes.Num()) == 0);
    TArray<uint8> Bad{0xc3,0x28};
    FFileHelper::SaveArrayToFile(Bad, *FPaths::Combine(Directory, TEXT("Cargo.toml")));
    TestFalse(TEXT("Invalid UTF8 refused before SDK"), ConstructModSources::Read(Directory, Files, Error));
    TestTrue(TEXT("No partial invalid payload"), Files.IsEmpty());
    // Only this test's GUID folder beneath Saved is removed.
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    TestFalse(TEXT("Missing source remains explicit error"), ConstructModSources::Read(Directory, Files, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsPendingTest, "Construct.Mods.GridSelectionAndRefreshPendingNoOps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsPendingTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::Off);
    for (const bool bStatus : {false, true})
    {
        FConstructModsTestAccess::Pending(*F.Panel, bStatus, !bStatus);
        TestFalse(TEXT("Refresh shared predicate blocks pending read"), FConstructModsTestAccess::Buttons(*F.Panel)[6]);
        F.Panel->Refresh();
        TestEqual(TEXT("Unavailable Refresh preserves state"), F.Panel->GetState(), EConstructModPanelState::Off);
    }
    FConstructModsTestAccess::Pending(*F.Panel, false, false);
    UConstructModMenu* Menu = NewObject<UConstructModMenu>(F.World);
    FConstructModsTestAccess::Menu(*Menu, F.World);
    FConstructModsTestAccess::BindPanel(*Menu, F.Panel);
    FConstructModsTestAccess::Begin(*F.Panel, 3);
    TestFalse(TEXT("Another grid unavailable through enable/wake"), FConstructModsTestAccess::CanSelect(*Menu, 2));
    TestTrue(TEXT("Same-grid opening reuses pending panel"), FConstructModsTestAccess::CanSelect(*Menu, 900000000000000001LL));
    Menu->Close();
    TestFalse(TEXT("Menu close releases the pending binding"), FConstructModsTestAccess::Busy(*F.Panel));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsHubLifetimeTest, "Construct.Mods.HubCooldownCancellationAndLateTimeoutReply", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsHubLifetimeTest::RunTest(const FString&)
{
    FModsFixture F;
    UConstructModMenu* Menu = NewObject<UConstructModMenu>(F.World);
    FConstructModsTestAccess::Menu(*Menu, F.World);
    const uint64 Generation = FConstructModsTestAccess::Generation(*Menu);
    uint64 Attempt = FConstructModsTestAccess::Attempt(*Menu);
    FCrowdyServerCallResult Reply;
    Reply.Outcome = ECrowdyServerCallOutcome::Denied;
    Reply.bRetryable = true;
    FConstructModsTestAccess::HubReply(*Menu, Generation, Attempt, Reply);
    TestTrue(TEXT("First cooldown schedules one second retry"), FConstructModsTestAccess::Retry(*Menu) >= 0.99);
    Attempt = FConstructModsTestAccess::Attempt(*Menu);
    FConstructModsTestAccess::HubReply(*Menu, Generation, Attempt, Reply);
    TestTrue(TEXT("Second refusal ends as error"), !FConstructModsTestAccess::HubError(*Menu).IsEmpty());
    const FString Error = FConstructModsTestAccess::HubError(*Menu);
    Reply.Outcome = ECrowdyServerCallOutcome::Success;
    FConstructModsTestAccess::HubReply(*Menu, Generation, Attempt, Reply);
    TestEqual(TEXT("Terminal attempt ignores late success"), FConstructModsTestAccess::HubError(*Menu), Error);
    Menu->Close();
    TestTrue(TEXT("Close cancels cooldown timer"), FConstructModsTestAccess::Retry(*Menu) < 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsInputHookTest, "Construct.Mods.ShellOpenHookAndModalPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsInputHookTest::RunTest(const FString&)
{
    ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
    UConstructUIInputSubsystem* Input = NewObject<UConstructUIInputSubsystem>(Player);
    int32 Opens = 0;
    const FDelegateHandle Handle = Input->OnModsOpenRequested.AddLambda([&Opens]() { ++Opens; });
    Input->RequestOpenMods();
    TestEqual(TEXT("Signed-out shell cannot request Mods"), Opens, 0);
    Input->SetSessionReady(true);
    Input->RequestOpenMods();
    TestEqual(TEXT("Signed-in shell hook reaches its owner"), Opens, 1);
    Input->SetModal(TEXT("Construct.Mods"), true);
    TestFalse(TEXT("Mods participates in existing modal owner"), Input->IsGameplayInputAllowed());
    Input->SetModal(TEXT("Chat"), true);
    Input->SetModal(TEXT("Construct.Mods"), false);
    TestFalse(TEXT("Closing Mods preserves another UI hold"), Input->IsGameplayInputAllowed());
    Input->SetModal(TEXT("Chat"), false);
    TestTrue(TEXT("Last hold restores gameplay"), Input->IsGameplayInputAllowed());
    Input->OnModsOpenRequested.Remove(Handle);
    Input->ReleaseManagement();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsGreeterOrderingTest, "Construct.Mods.GreeterErrorSurvivesUnrelatedList", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsGreeterOrderingTest::RunTest(const FString&)
{
    FModsFixture F;
    F.Arm(EConstructModPanelState::Loading);
    FConstructModsTestAccess::Select(*F.Panel, TEXT("platform-greeter"));
    FCrowdyModStarter Starter;
    Starter.Result = ECrowdyModResult::Denied;
    Starter.Message = TEXT("greeter source refused");
    FConstructModsTestAccess::Starter(*F.Panel, Starter);
    FCrowdyModList List;
    List.Result = ECrowdyModResult::Success;
    FConstructModsTestAccess::Status(*F.Panel, FConstructModsTestAccess::Binding(*F.Panel), FConstructModsTestAccess::Revision(*F.Panel), List);
    TestEqual(TEXT("Missing starter keeps Error"), F.Panel->GetState(), EConstructModPanelState::Error);
    TestTrue(TEXT("Unrelated list cannot erase reason"), FConstructModsTestAccess::Notice(*F.Panel).Contains(Starter.Message));
    TestTrue(TEXT("Unrelated list cannot erase Result"), FConstructModsTestAccess::Notice(*F.Panel).Contains(TEXT("Denied")));
    Starter.Result = ECrowdyModResult::Success;
    Starter.Name = TEXT("grid-mod");
    FCrowdyModFile File;
    File.Path = TEXT("Cargo.toml");
    File.Content = TEXT("source");
    Starter.Files = {File};
    FConstructModsTestAccess::Starter(*F.Panel, Starter);
    TestEqual(TEXT("Successful retry recovers"), F.Panel->GetState(), EConstructModPanelState::NoMod);
    TestTrue(TEXT("Recovered starter clears old reason"), FConstructModsTestAccess::Notice(*F.Panel).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsSharedCurrencyTest, "Construct.Mods.PadAndMenuShareOwnershipCurrency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsSharedCurrencyTest::RunTest(const FString&)
{
    FModsFixture F;
    UConstructModMenu* Menu = NewObject<UConstructModMenu>(F.World);
    FConstructModsTestAccess::Menu(*Menu, F.World);
    const FInt64Vector Chunk(5,0,0);
    FConstructModsTestAccess::Candidates(*Menu, {Chunk}, {});
    const uint64 Generation = FConstructModsTestAccess::Generation(*Menu);
    FCrowdyClaimSearch Owned;
    Owned.Result = ECrowdyClaimResult::Success;
    FCrowdyChunkClaimInfo Claim;
    Claim.GridId = 900000000000000001LL;
    Claim.OwnerUserId = 7;
    Claim.bOwnedByMe = true;
    Claim.ChunkX = 5;
    Owned.Claims = {Claim};
    uint64 MenuRequest = FConstructModsTestAccess::SharedLookup(*Menu, Chunk, true);
    FConstructModsTestAccess::Confirm(*Menu, Chunk, Generation, MenuRequest, Owned);
    TestTrue(TEXT("Fresh owner initially openable"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    uint64 PadRequest = FConstructModsTestAccess::SharedLookup(*Menu, Chunk, false);
    TestFalse(TEXT("New pad read immediately disables opening"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    FCrowdyClaimSearch Failed;
    Failed.Result = ECrowdyClaimResult::Refused;
    Failed.Message = TEXT("newer pad read refused");
    FConstructModsTestAccess::SharedAnswer(*Menu, Chunk, PadRequest, Failed);
    FConstructModsTestAccess::Confirm(*Menu, Chunk, Generation, MenuRequest, Owned);
    TestFalse(TEXT("Old menu success cannot undo newer pad failure"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    PadRequest = FConstructModsTestAccess::SharedLookup(*Menu, Chunk, false);
    MenuRequest = FConstructModsTestAccess::SharedLookup(*Menu, Chunk, true);
    FCrowdyClaimSearch NotOwned;
    NotOwned.Result = ECrowdyClaimResult::Success;
    FConstructModsTestAccess::Confirm(*Menu, Chunk, Generation, MenuRequest, NotOwned);
    FConstructModsTestAccess::SharedAnswer(*Menu, Chunk, PadRequest, Owned);
    TestFalse(TEXT("Old pad success cannot undo newer menu not-owned"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    Menu->Close();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsClosedReadTest, "Construct.Mods.SharedLookupFinishesAfterMenuClose", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsClosedReadTest::RunTest(const FString&)
{
    FModsFixture F;
    UConstructModMenu* Menu = NewObject<UConstructModMenu>(F.World);
    FConstructModsTestAccess::Menu(*Menu, F.World);
    UConstructClaimsSubsystem* Owner = FConstructModsTestAccess::Owner(*Menu);
    const FInt64Vector Chunk(5,0,0);
    AConstructClaimPad* Pad = F.World->SpawnActor<AConstructClaimPad>();
    Pad->SetActorLocation(UCrowdyVoxelLibrary::GetChunkOrigin(F.World, 5,0,0) + FVector(25));
    FConstructModsTestAccess::Candidates(*Menu, {Chunk}, {});
    const uint64 Request = Owner->BeginChunkLookup(Chunk);
    TestFalse(TEXT("Started lookup invalidates pad eligibility"), FConstructModsTestAccess::FreshPad(*Pad));
    Menu->Close();
    FCrowdyClaimSearch Reply;
    Reply.Result = ECrowdyClaimResult::Success;
    FCrowdyChunkClaimInfo Claim;
    Claim.GridId = 900000000000000001LL;
    Claim.OwnerUserId = 7;
    Claim.bOwnedByMe = true;
    Claim.ChunkX = 5;
    Reply.Claims = {Claim};
    Owner->FinishChunkLookup(Chunk, Request, Reply);
    TestTrue(TEXT("World-owned answer restores pad eligibility after menu close"), FConstructModsTestAccess::FreshPad(*Pad));
    TestFalse(TEXT("Shared lookup is no longer pending"), Owner->GetLookup(Chunk)->bPending);
    TestTrue(TEXT("Answer cannot reopen closed menu rows"), FConstructModsTestAccess::Rows(*Menu).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsDiscoveryFloorTest, "Construct.Mods.OldDiscoveryCannotConfirmRefreshedRows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsDiscoveryFloorTest::RunTest(const FString&)
{
    FModsFixture F;
    UConstructModMenu* Menu = NewObject<UConstructModMenu>(F.World);
    FConstructModsTestAccess::Menu(*Menu, F.World);
    UConstructClaimsSubsystem* Owner = FConstructModsTestAccess::Owner(*Menu);
    const FInt64Vector Chunk(5,0,0);
    FConstructHubClaim Candidate;
    Candidate.GridId = 900000000000000001LL;
    Candidate.Chunk.X = 5;
    FConstructModsTestAccess::Candidates(*Menu, {}, {Candidate});
    const uint64 OldRequest = Owner->BeginChunkLookup(Chunk);
    // A genuine refresh retains the old registry row while its new hub read is pending or fails.
    FConstructModsTestAccess::Discover(*Menu);
    FCrowdyClaimSearch Reply;
    Reply.Result = ECrowdyClaimResult::Success;
    FCrowdyChunkClaimInfo Claim;
    Claim.GridId = Candidate.GridId;
    Claim.OwnerUserId = 7;
    Claim.bOwnedByMe = true;
    Claim.ChunkX = 5;
    Reply.Claims = {Claim};
    Owner->FinishChunkLookup(Chunk, OldRequest, Reply);
    TestFalse(TEXT("Old generation settles owner data without confirming refreshed menu"), FConstructModsTestAccess::Rows(*Menu)[0].bConfirmed);
    Menu->Close();
    return true;
}
#endif