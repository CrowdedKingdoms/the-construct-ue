#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioPanel.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "ConstructShellStyle.h"
#include "ConstructStudioCaptureTestListener.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Text/STextBlock.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ImageCore.h"
#include "TextureResource.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SVirtualWindow.h"
#include "ConstructStudioDock.h"
#include "ConstructStudioPanelTestAccess.h"
#include "ConstructClaimsSubsystem.h"
#include "ConstructShellFramework.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Slate/SceneViewport.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"

namespace ConstructStudioPanelTests
{
constexpr auto PanelTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

void CollectStudioWidgets(const TSharedRef<SWidget>& Widget, FName Type, TArray<TSharedRef<SWidget>>& Out)
{
    if (Widget->GetType() == Type)
    {
        Out.Add(Widget);
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
    {
        CollectStudioWidgets(Children->GetChildAt(Index), Type, Out);
    }
}

struct FPanelRig
{
    UWorld* World = nullptr;
    APlayerController* Controller = nullptr;
    UConstructUIInputSubsystem* Gate = nullptr;
    TStrongObjectPtr<UGameViewportClient> Viewport;
    TStrongObjectPtr<ULocalPlayer> Local;
    TStrongObjectPtr<UConstructStudioPanel> Panel;
    TSharedPtr<SViewport> ViewportWidget;
    TSharedPtr<FSceneViewport> SceneViewport;
    TSharedPtr<SWidget> Slate;

    bool Initialize(EWorldType::Type Type = EWorldType::EditorPreview)
    {
        if (!GEngine)
        {
            return false;
        }
        UWorld::InitializationValues Init;
        Init.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(Type, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
        if (!World)
        {
            return false;
        }
        FWorldContext& Context = GEngine->CreateNewWorldContext(Type);
        Context.SetCurrentWorld(World);
        Viewport.Reset(NewObject<UGameViewportClient>(GEngine));
        ViewportWidget = SNew(SViewport);
        SceneViewport = Viewport->CreateViewport(ViewportWidget.ToSharedRef());
        Context.GameViewport = Viewport.Get();
        World->InitializeActorsForPlay(FURL());
        Controller = World->SpawnActor<APlayerController>();
        Local.Reset(NewObject<ULocalPlayer>(GEngine));
        Local->PlayerAdded(Viewport.Get(), int32(0));
        Controller->SetPlayer(Local.Get());
        Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
        Panel.Reset(CreateWidget<UConstructStudioPanel>(Controller));
        if (!Gate || !Panel.IsValid())
        {
            return false;
        }
        Gate->SetSessionReady(true);
        Slate = Panel->TakeWidget();
        FConstructStudioPanelTestAccess::Modal(*Panel, Gate);
        return true;
    }

    ~FPanelRig()
    {
        if (Panel.IsValid())
        {
            Panel->Shutdown();
        }
        Slate.Reset();
        Panel.Reset();
        if (Local.IsValid())
        {
            Local->PlayerRemoved();
        }
        SceneViewport.Reset();
        ViewportWidget.Reset();
        Local.Reset();
        Viewport.Reset();
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioPanelModalTest, "Construct.StudioPanel.Input.OtherModalsSurviveFocusPromptAndClose", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioPanelModalTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::Focus(*Rig.Panel, true);
    TestFalse(TEXT("Browser focus blocks gameplay"), Rig.Gate->IsGameplayInputAllowed());
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 42;
    FConstructStudioPanelTestAccess::Prompt(*Rig.Panel, Prompt);
    FConstructStudioPanelTestAccess::Focus(*Rig.Panel, false);
    TestFalse(TEXT("Prompt still blocks gameplay after browser focus leaves"), Rig.Gate->IsGameplayInputAllowed());
    Rig.Gate->SetModal(TEXT("CrowdyLegalConsent"), true);
    FConstructStudioPanelTestAccess::Withdraw(*Rig.Panel, 42);
    Rig.Panel->CloseStudio();
    TestFalse(TEXT("Closing dock preserves legal modal"), Rig.Gate->IsGameplayInputAllowed());
    Rig.Gate->SetModal(TEXT("CrowdyLegalConsent"), false);
    TestTrue(TEXT("Gameplay restores after final external modal closes"), Rig.Gate->IsGameplayInputAllowed());
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::Prompt(*Rig.Panel, Prompt);
    FConstructStudioPanelTestAccess::Withdraw(*Rig.Panel, 42);
    TestTrue(TEXT("Prompt to game restores movement when no other modal holds it"), Rig.Gate->IsGameplayInputAllowed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioPanelLiveEventsTest, "Construct.StudioPanel.Live.BootConsentAndPromptReplacement", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioPanelLiveEventsTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }

    FConstructStudioPanelTestAccess::Bind(*Rig.Panel);
    UConstructStudioDock* Dock = Rig.Panel->GetDock();
    const FConstructStudioPresentation& State = FConstructStudioPanelTestAccess::State(*Rig.Panel);
    Dock->OnStateChanged.Broadcast(EConstructStudioDockState::Booting, EConstructStudioDockReason::None);
    TestTrue(TEXT("Booting opens the frame"), State.IsOpen());

    Dock->OnStateChanged.Broadcast(EConstructStudioDockState::NeedsConsent, EConstructStudioDockReason::NeedsConsent);
    TestTrue(TEXT("Consent is a native prompt"), State.HasNativePrompt());
    TestFalse(TEXT("Consent pauses gameplay"), Rig.Gate->IsGameplayInputAllowed());
    TestTrue(TEXT("The panel uses the modal name recognized by Studio observation"), Rig.Gate->CanUseNavigationInput(TEXT("StudioDock")));
    Dock->OnStateChanged.Broadcast(EConstructStudioDockState::Booting, EConstructStudioDockReason::None);
    TestFalse(TEXT("Continuing boot dismisses consent"), State.HasNativePrompt());
    Dock->OnStateChanged.Broadcast(EConstructStudioDockState::Ready, EConstructStudioDockReason::None);
    TestEqual(TEXT("Completing boot enables the Ready presentation"), State.State, EConstructStudioDockState::Ready);

    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 81;
    Dock->OnDeployPrompt.Broadcast(Prompt);
    Prompt.PromptId = 82;
    Dock->OnDeployPrompt.Broadcast(Prompt);
    FConstructStudioPanelTestAccess::AllowRun(*Rig.Panel);
    FConstructStudioPanelTestAccess::Answer(*Rig.Panel, 81, true);
    if (TestTrue(TEXT("The old Run click leaves the replacement alone"), State.Prompt.IsSet()))
    {
        TestEqual(TEXT("The replacement still has its own identity"), State.Prompt->PromptId, int64(82));
    }
    FConstructStudioPanelTestAccess::Answer(*Rig.Panel, 82, false);
    TestFalse(TEXT("The current Not now dismisses its prompt"), State.HasNativePrompt());
    TestTrue(TEXT("Gameplay resumes"), Rig.Gate->IsGameplayInputAllowed());

    Dock->OnStateChanged.Broadcast(EConstructStudioDockState::Failed, EConstructStudioDockReason::NoModel);
    TestFalse(TEXT("A boot failure hides the frame"), State.IsOpen());
    TestFalse(TEXT("A boot failure has a reason toast"), FConstructStudioPanelTestAccess::Toast(*Rig.Panel).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioPanelFailureTest, "Construct.StudioPanel.Lifetime.FailureAndShutdownRestoreLayoutAndInput", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioPanelFailureTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    const float OriginalWidth = Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX;
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::Inset(*Rig.Panel, Rig.Viewport.Get(), 0.48f);
    FConstructStudioPanelTestAccess::Focus(*Rig.Panel, true);
    TestTrue(TEXT("Inset changed native player region"), Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX < OriginalWidth);
    FConstructStudioPanelTestAccess::Failed(*Rig.Panel);
    TestFalse(TEXT("Failed frame is closed"), FConstructStudioPanelTestAccess::State(*Rig.Panel).IsOpen());
    TestEqual(TEXT("Core is closed for reopening"), Rig.Panel->GetDock()->GetState(), EConstructStudioDockState::Closed);
    TestFalse(TEXT("Failure reason appears as toast"), FConstructStudioPanelTestAccess::Toast(*Rig.Panel).IsEmpty());
    TestEqual(TEXT("Original viewport rectangle restored"), Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, OriginalWidth);
    TestTrue(TEXT("Failure releases this modal"), Rig.Gate->IsGameplayInputAllowed());
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::Focus(*Rig.Panel, true);
    Rig.Gate->SetModal(TEXT("Chat"), true);
    Rig.Panel->Shutdown();
    TestFalse(TEXT("Shutdown preserves chat modal"), Rig.Gate->IsGameplayInputAllowed());
    Rig.Gate->SetModal(TEXT("Chat"), false);
    TestTrue(TEXT("Shutdown left no dock input owner"), Rig.Gate->IsGameplayInputAllowed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioInsetRestoreTest, "Construct.StudioPanel.Layout.InsetPreservesSplitLayoutAndExternalChanges", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioInsetRestoreTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameViewportClient> Viewport(NewObject<UGameViewportClient>(GEngine));
    const TArray<FSplitscreenData> Original = Viewport->SplitscreenInfo;
    FConstructStudioViewportInset Inset;
    Inset.Apply(Viewport.Get(), 0.48f);
    for (int32 Mode = 0; Mode < Original.Num(); ++Mode)
    {
        for (int32 Player = 0; Player < Original[Mode].PlayerData.Num(); ++Player)
        {
            const FPerPlayerSplitscreenData& Before = Original[Mode].PlayerData[Player];
            const FPerPlayerSplitscreenData& After = Viewport->SplitscreenInfo[Mode].PlayerData[Player];
            TestEqual(TEXT("Each split Xorigin scales within game area"), After.OriginX, Before.OriginX * 0.48f);
            TestEqual(TEXT("Each split Xsize scales within game area"), After.SizeX, Before.SizeX * 0.48f);
            TestEqual(TEXT("Ysize unchanged"), After.SizeY, Before.SizeY);
        }
    }
    Inset.Restore();
    TestEqual(TEXT("Close restores native default"), Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, Original[0].PlayerData[0].SizeX);
    Inset.Apply(Viewport.Get(), 0.48f);
    Viewport->SplitscreenInfo[0].PlayerData[0].SizeX = 0.37f;
    Inset.Restore();
    TestEqual(TEXT("Unrelated later layout edit is preserved"), Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, 0.37f);
    Inset.Apply(Viewport.Get(), 0.48f);
    FPerPlayerSplitscreenData& SecondPlayer = Viewport->SplitscreenInfo[ESplitScreenType::TwoPlayer_Vertical].PlayerData[1];
    const float DefaultOrigin = Original[ESplitScreenType::TwoPlayer_Vertical].PlayerData[1].OriginX;
    SecondPlayer.SizeX = 0.41f;
    SecondPlayer.OriginY = 0.16f;
    Inset.Apply(Viewport.Get(), 0.60f);
    const FPerPlayerSplitscreenData& Resized = Viewport->SplitscreenInfo[ESplitScreenType::TwoPlayer_Vertical].PlayerData[1];
    TestEqual(TEXT("Resizing uses the live host width"), Resized.SizeX, 0.41f * 0.60f);
    TestEqual(TEXT("Resizing preserves the live vertical layout"), Resized.OriginY, 0.16f);
    Inset.Restore();
    const FPerPlayerSplitscreenData& Restored = Viewport->SplitscreenInfo[ESplitScreenType::TwoPlayer_Vertical].PlayerData[1];
    TestEqual(TEXT("Close restores the owned origin independently"), Restored.OriginX, DefaultOrigin);
    TestEqual(TEXT("Close restores the revised host width"), Restored.SizeX, 0.41f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioNavigationLayoutTest, "Construct.StudioPanel.Layout.ForeignModalResizeRestoresInset", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioNavigationLayoutTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    TestEqual(TEXT("The isolated viewport uses fallback geometry"), Rig.SceneViewport->GetSizeXY(), FIntPoint::ZeroValue);
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    TestTrue(TEXT("Open Studio insets the scene"), Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX < 1.0f);
    Rig.Gate->SetModal(TEXT("PlayerName"), true);
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1400);
    TestEqual(TEXT("Resize while another menu owns navigation restores the full scene"), Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, 1.0f);
    Rig.Gate->SetModal(TEXT("PlayerName"), false);
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1400);
    TestTrue(TEXT("Dismissal reapplies the inset without another resize"), FMath::IsNearlyEqual(Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, 0.48f));
    Rig.Panel->CloseStudio();
    TestEqual(TEXT("Close restores the original scene rectangle"), Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, 1.0f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioOwnershipRefusalTest, "Construct.StudioPanel.Claims.UnresolvedOwnershipDoesNotOpen", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioOwnershipRefusalTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    int64 Grid = 123;
    FText Label = FText::FromString(TEXT("old"));
    TestFalse(TEXT("No pawn or confirmed lookup never grants an opening"), FConstructStudioPanelTestAccess::Context(*Rig.Panel, Grid, Label));
    TestEqual(TEXT("Refusal outputs no grid"), Grid, int64(0));
    TestTrue(TEXT("Refusal outputs no misleading land name"), Label.IsEmpty());
    Rig.Panel->OpenFromOwnedChunk();
    TestEqual(TEXT("Core remains Closed"), Rig.Panel->GetDock()->GetState(), EConstructStudioDockState::Closed);
    TestFalse(TEXT("Ownership refusal is visible"), FConstructStudioPanelTestAccess::Toast(*Rig.Panel).IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioCurrentClaimTest, "Construct.StudioPanel.Claims.OnlyCurrentSuccessfulOwnSelfClaimProvidesContext", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioCurrentClaimTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Game world rig initialized"), Rig.Initialize(EWorldType::Game)))
    {
        return false;
    }
    APawn* Pawn = Rig.World->SpawnActor<APawn>();
    Rig.Controller->SetPawn(Pawn);
    UConstructClaimsSubsystem* Claims = Rig.World->GetSubsystem<UConstructClaimsSubsystem>();
    if (!TestNotNull(TEXT("Real C7 world subsystem"), Claims))
    {
        return false;
    }
    const FInt64Vector Chunk = FInt64Vector::ZeroValue;
    int64 Grid = 0;
    FText Label;
    const uint64 First = Claims->BeginChunkLookup(Chunk);
    TestFalse(TEXT("Pending lookup cannot open"), FConstructStudioPanelTestAccess::Context(*Rig.Panel, Grid, Label));
    FCrowdyClaimSearch Reply;
    Reply.Result = ECrowdyClaimResult::Success;
    FCrowdyChunkClaimInfo Claim;
    Claim.GridId = 900000000000000001LL;
    Claim.OwnerUserId = 7;
    Claim.bOwnedByMe = true;
    Reply.Claims.Add(Claim);
    Claims->FinishChunkLookup(Chunk, First, Reply);
    TestTrue(TEXT("Current successful own self-claim supplies context"), FConstructStudioPanelTestAccess::Context(*Rig.Panel, Grid, Label));
    TestEqual(TEXT("Grid keeps its full64bit identity"), Grid, Claim.GridId);
    TestTrue(TEXT("Land label identifies its chunk"), Label.ToString().Contains(TEXT("Chunk")));
    const uint64 Second = Claims->BeginChunkLookup(Chunk);
    Claims->FinishChunkLookup(Chunk, First, Reply);
    TestFalse(TEXT("Late old reply cannot satisfy newer pending lookup"), FConstructStudioPanelTestAccess::Context(*Rig.Panel, Grid, Label));
    Reply.Claims[0].bOwnedByMe = false;
    Claims->FinishChunkLookup(Chunk, Second, Reply);
    TestFalse(TEXT("Another player's claim cannot open"), FConstructStudioPanelTestAccess::Context(*Rig.Panel, Grid, Label));
    const uint64 Third = Claims->BeginChunkLookup(Chunk);
    Reply.Result = ECrowdyClaimResult::Failed;
    Reply.Claims[0].bOwnedByMe = true;
    Claims->FinishChunkLookup(Chunk, Third, Reply);
    TestFalse(TEXT("Failed lookup does not preserve old authorization"), FConstructStudioPanelTestAccess::Context(*Rig.Panel, Grid, Label));
    const uint64 Fourth = Claims->BeginChunkLookup(Chunk);
    Reply.Result = ECrowdyClaimResult::Success;
    Reply.Claims.Reset();
    Claims->FinishChunkLookup(Chunk, Fourth, Reply);
    TestFalse(TEXT("Confirmed absent claim cannot open"), FConstructStudioPanelTestAccess::Context(*Rig.Panel, Grid, Label));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioFreshLookupTest, "Construct.StudioPanel.Claims.FreshLookupOfAppliedChunkKeepsDockOpen", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioFreshLookupTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Game world rig initialized"), Rig.Initialize(EWorldType::Game)))
    {
        return false;
    }
    Rig.Controller->SetPawn(Rig.World->SpawnActor<APawn>());
    UConstructClaimsSubsystem* Claims = Rig.World->GetSubsystem<UConstructClaimsSubsystem>();
    if (!TestNotNull(TEXT("Real C7 world subsystem"), Claims))
    {
        return false;
    }
    const FInt64Vector Chunk = FInt64Vector::ZeroValue;
    const FConstructStudioPresentation& State = FConstructStudioPanelTestAccess::State(*Rig.Panel);
    FCrowdyClaimSearch Reply;
    Reply.Result = ECrowdyClaimResult::Success;
    FCrowdyChunkClaimInfo Claim;
    Claim.GridId = 900000000000000001LL;
    Claim.bOwnedByMe = true;
    Reply.Claims.Add(Claim);

    const uint64 First = Claims->BeginChunkLookup(Chunk);
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    Rig.Panel->RefreshOwnedChunk();
    TestFalse(TEXT("A chunk with no successful lookup yet closes the dock"), State.IsOpen());

    Claims->FinishChunkLookup(Chunk, First, Reply);
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    Rig.Panel->RefreshOwnedChunk();
    TestTrue(TEXT("The confirmed own chunk keeps the dock open"), State.IsOpen());

    const uint64 Second = Claims->BeginChunkLookup(Chunk);
    Rig.Panel->RefreshOwnedChunk();
    TestTrue(TEXT("A fresh lookup of the same chunk keeps the dock open"), State.IsOpen());

    Reply.Claims[0].bOwnedByMe = false;
    Claims->FinishChunkLookup(Chunk, Second, Reply);
    Rig.Panel->RefreshOwnedChunk();
    TestFalse(TEXT("The lookup answering another owner closes it"), State.IsOpen());
    TestFalse(TEXT("with a reason"), FConstructStudioPanelTestAccess::Toast(*Rig.Panel).IsEmpty());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioSlateFocusTest, "Construct.StudioPanel.Input.BrowserModalPreservesActualSlateFocus", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioSlateFocusTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::PreviewWidth(*Rig.Panel, 1280);
    const TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(FVector2D(1280, 720));
    Window->SetContent(Rig.Slate.ToSharedRef());
    Window->SetIsFocusable(true);
    FSlateApplication::Get().RegisterVirtualWindow(Window);
    Window->SlatePrepass();
    const TSharedRef<SWidget> Browser = Rig.Panel->GetDock()->TakeWidget();
    FSlateApplication::Get().SetUserFocus(0, Browser, EFocusCause::SetDirectly);
    const bool bInitiallyFocused = FSlateApplication::Get().GetUserFocusedWidget(0) == Browser;
    TestTrue(TEXT("Real browser placeholder acquired Slate focus"), bInitiallyFocused);
    FConstructStudioPanelTestAccess::Focus(*Rig.Panel, true);
    TestTrue(TEXT("Registering browser modal retains actual browser focus"), FSlateApplication::Get().GetUserFocusedWidget(0) == Browser);
    TestFalse(TEXT("Browser modal blocks movement"), Rig.Gate->IsGameplayInputAllowed());
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 73;
    FConstructStudioPanelTestAccess::Prompt(*Rig.Panel, Prompt);
    FConstructStudioPanelTestAccess::Focus(*Rig.Panel, false);
    TestFalse(TEXT("Native prompt keeps movement blocked after focus transition"), Rig.Gate->IsGameplayInputAllowed());
    Rig.Panel->CloseStudio();
    FSlateApplication::Get().UnregisterVirtualWindow(Window);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioForeignModalFocusTest, "Construct.StudioPanel.Input.DelayedPromptWaitsForForeignModal", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioForeignModalFocusTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    TStrongObjectPtr<UConstructConnectionStatus> Foreign(CreateWidget<UConstructConnectionStatus>(Rig.Controller));
    if (!TestNotNull(TEXT("Focusable menu fixture"), Foreign.Get()))
    {
        return false;
    }
    Foreign->SetIsFocusable(true);
    const TSharedRef<SWidget> ForeignSlate = Foreign->TakeWidget();
    const TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(FVector2D(1280, 720));
    Window->SetContent(SNew(SOverlay) + SOverlay::Slot()[Rig.Slate.ToSharedRef()] + SOverlay::Slot()[ForeignSlate]);
    FSlateApplication::Get().RegisterVirtualWindow(Window);
    Window->SlatePrepass();

    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    Rig.Gate->SetModal(TEXT("Mods"), true, Foreign.Get());
    FSlateApplication::Get().SetUserFocus(0, ForeignSlate, EFocusCause::SetDirectly);
    TestTrue(TEXT("The foreign menu initially holds real Slate focus"), FSlateApplication::Get().GetUserFocusedWidget(0) == ForeignSlate);
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 83;
    FConstructStudioPanelTestAccess::Prompt(*Rig.Panel, Prompt);
    TestTrue(TEXT("A hidden Studio prompt does not take over Mods navigation"), Rig.Gate->CanUseNavigationInput(TEXT("Mods")));
    FWidgetPath FocusPath;
    FSlateApplication& Slate = FSlateApplication::Get();
    Slate.GeneratePathToWidgetUnchecked(ForeignSlate, FocusPath);
    Slate.ProcessExternalReply(FocusPath, Rig.Local->GetSlateOperations(), 0);
    Rig.Local->GetSlateOperations() = FReply::Unhandled();
    TestTrue(TEXT("The foreign menu keeps its Slate focus after processing pending input"), Slate.GetUserFocusedWidget(0) == ForeignSlate);
    const FConstructStudioPresentation& State = FConstructStudioPanelTestAccess::State(*Rig.Panel);
    TestTrue(TEXT("The waiting prompt is retained"), State.HasNativePrompt());
    FPlatformProcess::Sleep(1.1f);
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    TestFalse(TEXT("A prompt that waited over a second behind Mods is still not runnable"), State.CanRun(FPlatformTime::Seconds()));

    Rig.Gate->SetModal(TEXT("Mods"), false);
    const double ShownAt = FPlatformTime::Seconds();
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    TestFalse(TEXT("Run stays disabled for the first second the prompt is visible"), State.CanRun(ShownAt + 0.9));
    FConstructStudioPanelTestAccess::Answer(*Rig.Panel, 83, true);
    TestTrue(TEXT("An immediate Run click does not answer the prompt"), State.Prompt.IsSet());
    TestTrue(TEXT("Run is available one second after the prompt became visible"), State.CanRun(FPlatformTime::Seconds() + 1.0));
    TestFalse(TEXT("The visible Studio prompt now pauses gameplay"), Rig.Gate->IsGameplayInputAllowed());
    TestTrue(TEXT("Studio now owns navigation"), Rig.Gate->CanUseNavigationInput(TEXT("StudioDock")));
    Rig.Panel->CloseStudio();
    TestTrue(TEXT("Closing Studio restores gameplay"), Rig.Gate->IsGameplayInputAllowed());

    FSlateApplication::Get().UnregisterVirtualWindow(Window);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioInactiveAppTest, "Construct.StudioPanel.Input.InactiveApplicationKeepsDockAndRestartsRunGuard", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioInactiveAppTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    const FConstructStudioPresentation& State = FConstructStudioPanelTestAccess::State(*Rig.Panel);
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    FConstructStudioPanelTestAccess::Focus(*Rig.Panel, true);
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 85;
    FConstructStudioPanelTestAccess::Prompt(*Rig.Panel, Prompt);
    FConstructStudioPanelTestAccess::AllowRun(*Rig.Panel);
    TestTrue(TEXT("A prompt visible long enough can run"), State.CanRun(FPlatformTime::Seconds()));
    const float InsetWidth = Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX;
    TestTrue(TEXT("The open dock insets the scene"), InsetWidth < 1.0f);

    bool bReactivated = false;
    ON_SCOPE_EXIT
    {
        if (!bReactivated)
        {
            FCoreDelegates::ApplicationHasReactivatedDelegate.Broadcast();
        }
    };
    FCoreDelegates::ApplicationWillDeactivateDelegate.Broadcast();
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    TestTrue(TEXT("Switching away keeps the frame"), FConstructStudioPanelTestAccess::Shown(*Rig.Panel));
    TestTrue(TEXT("and the scene inset"), FMath::IsNearlyEqual(Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, InsetWidth));
    TestTrue(TEXT("and the Studio modal"), FConstructStudioPanelTestAccess::HoldsModal(*Rig.Panel));
    TestFalse(TEXT("A prompt is not runnable while the window is inactive"), State.CanRun(FPlatformTime::Seconds() + 5.0));

    const double ActiveAt = FPlatformTime::Seconds();
    FCoreDelegates::ApplicationHasReactivatedDelegate.Broadcast();
    bReactivated = true;
    FConstructStudioPanelTestAccess::Tick(*Rig.Panel, 1280);
    TestFalse(TEXT("Returning keeps movement paused"), Rig.Gate->IsGameplayInputAllowed());
    TestTrue(TEXT("Returning keeps the scene inset"), FMath::IsNearlyEqual(Rig.Viewport->SplitscreenInfo[0].PlayerData[0].SizeX, InsetWidth));
    TestFalse(TEXT("The click that reactivates the window cannot run"), State.CanRun(ActiveAt + 0.9));
    FConstructStudioPanelTestAccess::Answer(*Rig.Panel, 85, true);
    TestTrue(TEXT("so an immediate Run click leaves the prompt"), State.Prompt.IsSet());
    TestTrue(TEXT("Run is available again one second later"), State.CanRun(FPlatformTime::Seconds() + 1.0));
    Rig.Panel->CloseStudio();
    TestTrue(TEXT("Closing Studio restores gameplay"), Rig.Gate->IsGameplayInputAllowed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioAgentWarningTest, "Construct.StudioPanel.Prompt.AgentWarningHiddenWhilePromptShows", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioAgentWarningTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig.Initialize()))
    {
        return false;
    }
    FConstructStudioPanelTestAccess::Bind(*Rig.Panel);
    UConstructStudioDock* Dock = Rig.Panel->GetDock();
    const FConstructStudioPresentation& State = FConstructStudioPanelTestAccess::State(*Rig.Panel);
    Dock->OnStateChanged.Broadcast(EConstructStudioDockState::Ready, EConstructStudioDockReason::None);
    Dock->OnWarning.Broadcast(FText::FromString(TEXT("Disk is full")));
    TestTrue(TEXT("A warning shows beside the dock"), State.ShowsWarning());
    TestEqual(TEXT("as the agent's"), State.Warning.ToString(), FString(TEXT("Agent: Disk is full")));

    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 86;
    Dock->OnDeployPrompt.Broadcast(Prompt);
    TestTrue(TEXT("A new prompt clears the earlier warning"), State.Warning.IsEmpty());
    Dock->OnWarning.Broadcast(FText::FromString(TEXT("Run it now")));
    TestFalse(TEXT("A warning raised during the prompt stays hidden"), State.ShowsWarning());
    FConstructStudioPanelTestAccess::Answer(*Rig.Panel, 86, false);
    TestTrue(TEXT("and shows once the prompt is answered"), State.ShowsWarning());
    Rig.Panel->CloseStudio();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioHiddenCaptureTest, "Construct.StudioPanel.Capture.RefusedWhileDockIsHidden", ConstructStudioPanelTests::PanelTestFlags)
bool FConstructStudioHiddenCaptureTest::RunTest(const FString&)
{
    const TSharedRef<ConstructStudioPanelTests::FPanelRig> Rig = MakeShared<ConstructStudioPanelTests::FPanelRig>();
    if (!TestTrue(TEXT("Native local widget rig initialized"), Rig->Initialize()))
    {
        return false;
    }
    FConstructStudioPanelTestAccess::Ready(*Rig->Panel);
    Rig->Gate->SetModal(TEXT("Mods"), true);
    TestFalse(TEXT("Another menu hides the dock"), FConstructStudioPanelTestAccess::Shown(*Rig->Panel));
    const TStrongObjectPtr<UConstructStudioCaptureTestListener> Listener(NewObject<UConstructStudioCaptureTestListener>());
    FOnConstructStudioCaptured Answer;
    Answer.BindDynamic(Listener.Get(), &UConstructStudioCaptureTestListener::Captured);
    Rig->Panel->CaptureView_Implementation(TEXT("Agent view"), Answer);
    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Rig, Listener, Started]()
    {
        if (Listener->Answers == 0 && FPlatformTime::Seconds() - Started < 5.0)
        {
            return false;
        }
        if (TestEqual(TEXT("A hidden dock answers the capture once"), Listener->Answers, 1))
        {
            TestFalse(TEXT("with no screenshot"), Listener->Capture.bCaptured);
            TestTrue(TEXT("and a reason the agent hears"), Listener->Capture.Refusal.Contains(TEXT("not showing")));
        }
        Rig->Gate->SetModal(TEXT("Mods"), false);
        Rig->Panel->CloseStudio();
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioPanelRenderedLayoutTest, "Construct.StudioPanel.Layout.RenderedNativePrompts", ConstructStudioPanelTests::PanelTestFlags | EAutomationTestFlags::NonNullRHI)
bool FConstructStudioPanelRenderedLayoutTest::RunTest(const FString&)
{
    ConstructStudioPanelTests::FPanelRig Rig;
    if (!TestTrue(TEXT("Real panel preview initialized"), Rig.Initialize()))
    {
        return false;
    }
    FConstructStudioPanelTestAccess::ProviderPreview(*Rig.Panel, FText::FromString(TEXT("Local test notice. Project source, messages and scene screenshots reach the model provider. Requests are charged to the payer shown by the platform. Live webcam images are excluded.")));
    FWidgetRenderer Renderer(true);
    const float Scale = UWidgetLayoutLibrary::GetViewportScale(Rig.Panel.Get());
    const TSharedRef<SWidget> Scaled = SNew(SDPIScaler).DPIScale(Scale)[Rig.Slate.ToSharedRef()];
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("StudioPanel/Previews");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const auto Render = [&](const TCHAR* Name, FVector2D Size)
    {
        TStrongObjectPtr<UTextureRenderTarget2D> Warmup(Renderer.DrawWidget(Scaled, Size));
        FlushRenderingCommands();
        TStrongObjectPtr<UTextureRenderTarget2D> Target(Renderer.DrawWidget(Scaled, Size));
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
        ReadFlags.SetLinearToGamma(false);
        if (!Target.IsValid() || !Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags))
        {
            AddError(TEXT("Real panel framebuffer could not be read."));
            return;
        }
        TArray64<uint8> Png;
        const FImageView Image(Pixels.GetData(), Target->SizeX, Target->SizeY);
        TestTrue(TEXT("Native widget preview PNG encoded"), FImageUtils::CompressImage(Png, TEXT("png"), Image));
        TestTrue(TEXT("Synthetic widget preview saved"), FFileHelper::SaveArrayToFile(Png, *(Directory / Name)));
    };
    const FSlateFontInfo BodyFont = FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Construct.Text.Body")).Font;
    const float BodyLine = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(BodyFont);
    const auto CheckPrompt = [&](const TCHAR* Name, FVector2D Size)
    {
        const TSharedPtr<SWidget> Frame = FConstructStudioPanelTestAccess::Frame(*Rig.Panel);
        const TSharedPtr<SWidget> Prompt = FConstructStudioPanelTestAccess::PromptArea(*Rig.Panel);
        if (!Frame.IsValid() || !Prompt.IsValid())
        {
            AddError(TEXT("The panel has no frame or prompt area to measure."));
            return;
        }
        const FSlateRect FrameRect = Frame->GetPaintSpaceGeometry().GetLayoutBoundingRect().ExtendBy(FMargin(0.5f));
        const FSlateRect WindowRect = FSlateRect(0.0f, 0.0f, static_cast<float>(Size.X), static_cast<float>(Size.Y)).ExtendBy(FMargin(0.5f));
        TArray<TSharedRef<SWidget>> Buttons;
        ConstructStudioPanelTests::CollectStudioWidgets(Prompt.ToSharedRef(), TEXT("SButton"), Buttons);
        TestEqual(FString::Printf(TEXT("%s shows both answer buttons"), Name), Buttons.Num(), 2);
        for (const TSharedRef<SWidget>& Button : Buttons)
        {
            const FSlateRect ButtonRect = Button->GetPaintSpaceGeometry().GetLayoutBoundingRect();
            TestTrue(FString::Printf(TEXT("%s has a drawn answer button"), Name), ButtonRect.GetSize().X > 1.0f && ButtonRect.GetSize().Y > 1.0f);
            TestTrue(FString::Printf(TEXT("%s keeps the answer button inside the frame"), Name), FSlateRect::IsRectangleContained(FrameRect, ButtonRect));
            TestTrue(FString::Printf(TEXT("%s keeps the answer button inside the window"), Name), FSlateRect::IsRectangleContained(WindowRect, ButtonRect));
        }
        TArray<TSharedRef<SWidget>> Texts;
        ConstructStudioPanelTests::CollectStudioWidgets(Prompt.ToSharedRef(), TEXT("STextBlock"), Texts);
        const TSharedRef<SWidget>* ProjectLine = Texts.FindByPredicate([](const TSharedRef<SWidget>& Text)
        {
            return StaticCastSharedRef<STextBlock>(Text)->GetText().ToString().StartsWith(TEXT("Project: "), ESearchCase::CaseSensitive);
        });
        if (TestNotNull(FString::Printf(TEXT("%s shows the project line"), Name), ProjectLine))
        {
            TestTrue(FString::Printf(TEXT("%s keeps the project line one line high"), Name), (*ProjectLine)->GetPaintSpaceGeometry().GetLocalSize().Y <= BodyLine * 1.5f);
        }
    };
    Render(TEXT("Consent-1920.png"), FVector2D(1920, 1080));
    FConstructStudioPanelTestAccess::Ready(*Rig.Panel);
    Rig.Panel->GetDock()->OnWarning.Broadcast(FText::FromString(TEXT("Local UI test data. No model request or deployment.")));
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 84;
    Prompt.GridId = 900000000000000001LL;
    Prompt.GridLabel = FText::FromString(TEXT("Chunk (0, 0, 0)"));
    Prompt.ProjectId = TEXT("example-project");
    Prompt.ProjectName = TEXT("Spinning platform");
    Prompt.Revision = TEXT("saved-revision-4");
    Prompt.bReachesVisitors = true;
    Prompt.ChangedFiles = {TEXT("server/src/logic.rs"), TEXT("client/main.ts")};
    Rig.Panel->GetDock()->OnDeployPrompt.Broadcast(Prompt);
    Render(TEXT("Deploy-1920.png"), FVector2D(1920, 1080));
    CheckPrompt(TEXT("Deploy-1920"), FVector2D(1920, 1080));
    Render(TEXT("Deploy-1280.png"), FVector2D(1280, 720));
    CheckPrompt(TEXT("Deploy-1280"), FVector2D(1280, 720));
    Render(TEXT("Deploy-Fullscreen-900.png"), FVector2D(900, 720));
    CheckPrompt(TEXT("Deploy-Fullscreen-900"), FVector2D(900, 720));
    Prompt.ProjectName = FString(TEXT("Example project\n")) + FString::ChrN(160, TEXT('T'));
    for (int32 Index = 0; Index < 40; ++Index)
    {
        Prompt.ChangedFiles.Add(FString::Printf(TEXT("client/artifact-%d.ts"), Index));
    }
    Rig.Panel->GetDock()->OnDeployPrompt.Broadcast(Prompt);
    FConstructStudioPanelTestAccess::PreviewWidth(*Rig.Panel, 900);
    Render(TEXT("Deploy-Short-900.png"), FVector2D(900, 400));
    CheckPrompt(TEXT("Deploy-Short-900"), FVector2D(900, 400));
    Rig.Panel->CloseStudio();
    return true;
}

#endif

