#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructWebcamPanel.h"
#include "ConstructDeveloperSettings.h"
#include "ConstructWebcamFeedWidget.h"
#include "ConstructWebcamSafetyRequest.h"
#include "ConstructPresencePawn.h"
#include "ConstructUIInputSubsystem.h"
#include "ConstructWebcamSubsystem.h"
#include "ConstructWebcamFeeds.h"
#include "Components/InputComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Slate/SceneViewport.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SViewport.h"

struct FConstructWebcamUITestAccess
{
    static UInputComponent* Input(const UConstructWebcamPanel& Panel) { return Panel.CameraInput; }
    static void Enable(UConstructWebcamPanel& Panel, bool bEnabled = true) { Panel.bEnabledForWorld = bEnabled; }
    static bool HasTimer(const UConstructWebcamPanel& Panel) { return Panel.BoundWorld.IsValid() && Panel.BoundWorld->GetTimerManager().IsTimerActive(Panel.RefreshTimer); }
    static int32 RemoteCount(const UConstructWebcamPanel& Panel) { return Panel.Remotes.Num(); }
    static UConstructWebcamFeedWidget* Widget(const UConstructWebcamPanel& Panel) { return Panel.Remotes.IsEmpty() ? nullptr : Panel.Remotes[0].Widget.Get(); }
    static UWidgetComponent* Component(const UConstructWebcamPanel& Panel) { return Panel.Remotes.IsEmpty() ? nullptr : Panel.Remotes[0].Component.Get(); }
    static void Present(UConstructWebcamPanel& Panel, AConstructPresencePawn* Pawn, const FConstructWebcamFeed& Feed) { Panel.PresentFeed(Pawn, Feed); }
    static void Record(UConstructWebcamPanel& Panel, bool bVideo)
    {
        FCrowdyAppRecording Recording;
        Recording.bKnown = true;
        Recording.bVideo = bVideo;
        Panel.RecordingChanged(Recording);
    }
    static FReply Escape(UConstructWebcamPanel& Panel) { return Panel.NativeOnPreviewKeyDown(FGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)); }
    // Only Construct-owned receivers are driven here; no SDK transport or capture is substituted.
    static UConstructWebcamSafetyRequest* Reply(UConstructWebcamPanel& Panel, EConstructWebcamSafetyAction Action)
    {
        UConstructWebcamSafetyRequest* Request = NewObject<UConstructWebcamSafetyRequest>(&Panel);
        Request->Panel = &Panel;
        Request->Generation = Panel.Generation;
        Request->Action = Action;
        Panel.Requests.Add(Request);
        return Request;
    }
    static void Retain(UConstructWebcamPanel& Panel, UConstructWebcamSafetyRequest* Request) { Panel.Requests.Add(Request); }
    static void PrimeReport(UConstructWebcamPanel& Panel)
    {
        Panel.ReportTarget.UserId = 41;
        Panel.ReportTarget.SenderId = TEXT("original-sender");
        Panel.ReportTarget.Generation = 1;
        Panel.ReportTarget.DisplayName = TEXT("Original player");
        Panel.ReportTarget.OccurredAt = FDateTime(2026, 1, 2, 3, 4, 5);
        Panel.SetDialog(false, true);
    }
    static FConstructWebcamReportTarget Report(const UConstructWebcamPanel& Panel) { return Panel.ReportTarget; }
};

namespace
{
struct FWebcamUIFixture
{
    UWorld* World = nullptr;
    TStrongObjectPtr<UGameViewportClient> Viewport;
    TSharedPtr<SViewport> ViewportWidget;
    TSharedPtr<FSceneViewport> SceneViewport;
    TStrongObjectPtr<ULocalPlayer> Local;
    TStrongObjectPtr<UGameInstance> SourceOuter;
    TStrongObjectPtr<UConstructWebcamSubsystem> Camera;
    TStrongObjectPtr<UConstructWebcamFeeds> Feeds;
    TStrongObjectPtr<UConstructWebcamPanel> Panel;
    TSharedPtr<SWidget> Slate;
    APlayerController* Controller = nullptr;
    UConstructUIInputSubsystem* Gate = nullptr;

    bool Initialize()
    {
        if (!GEngine)
        {
            return false;
        }
        UWorld::InitializationValues Init;
        Init.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
        if (!World)
        {
            return false;
        }
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
        Context.SetCurrentWorld(World);
        Viewport.Reset(NewObject<UGameViewportClient>(GEngine));
        ViewportWidget = SNew(SViewport);
        SceneViewport = Viewport->CreateViewport(ViewportWidget.ToSharedRef());
        Context.GameViewport = Viewport.Get();
        World->InitializeActorsForPlay(FURL());
        Controller = World->SpawnActor<APlayerController>();
        if (!Controller)
        {
            return false;
        }
        Local.Reset(NewObject<ULocalPlayer>(GEngine));
        Local->PlayerAdded(Viewport.Get(), int32(0));
        Controller->SetPlayer(Local.Get());
        Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
        Panel.Reset(CreateWidget<UConstructWebcamPanel>(Controller));
        if (!Gate || !Panel.IsValid())
        {
            return false;
        }
        Panel->SetOwningPlayer(Controller);
        Slate = Panel->TakeWidget();
        // No game instance is installed on the world and no SDK collection or capture device is initialized.
        SourceOuter.Reset(NewObject<UGameInstance>());
        Camera.Reset(NewObject<UConstructWebcamSubsystem>(SourceOuter.Get()));
        Feeds.Reset(NewObject<UConstructWebcamFeeds>(World));
        Panel->BindSources(Camera.Get(), Feeds.Get());
        FConstructWebcamUITestAccess::Enable(*Panel);
        Gate->SetSessionReady(true);
        return Panel->GetOwningLocalPlayer() == Local.Get() && Panel->CanInteract();
    }
    void PressCameraKey()
    {
        for (FInputKeyBinding& Binding : FConstructWebcamUITestAccess::Input(*Panel)->KeyBindings)
        {
            if (Binding.Chord.Key == EKeys::C && Binding.KeyEvent == IE_Pressed)
            {
                Binding.KeyDelegate.Execute(EKeys::C);
                return;
            }
        }
    }
    ~FWebcamUIFixture()
    {
        if (Panel.IsValid())
        {
            Panel->Shutdown();
        }
        if (Local.IsValid())
        {
            Local->PlayerRemoved();
        }
        Slate.Reset();
        Panel.Reset();
        Camera.Reset();
        Feeds.Reset();
        SourceOuter.Reset();
        SceneViewport.Reset();
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }
};

TSharedPtr<SWidget> FindOverlay(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetType() == FName(TEXT("SOverlay")))
    {
        return Widget;
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
    {
        if (TSharedPtr<SWidget> Found = FindOverlay(Children->GetChildAt(Index)))
        {
            return Found;
        }
    }
    return nullptr;
}

TSharedPtr<STextBlock> FindFirstText(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetType() == FName(TEXT("STextBlock")))
    {
        return StaticCastSharedRef<STextBlock>(Widget);
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
    {
        if (TSharedPtr<STextBlock> Found = FindFirstText(Children->GetChildAt(Index)))
        {
            return Found;
        }
    }
    return nullptr;
}

FCrowdyCommunicationSettings AllowedSettings()
{
    FCrowdyCommunicationSettings Settings;
    Settings.VideoSend = ECrowdyCommunicationScope::Contacts;
    Settings.VideoReceive = ECrowdyCommunicationScope::Everyone;
    Settings.Choices.VideoSend = ECrowdyCommunicationScope::Everyone;
    Settings.Choices.VideoReceive = ECrowdyCommunicationScope::Everyone;
    return Settings;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUICameraKeyTest, "Construct.Webcam.UI.DefaultOffAndBoundKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUICameraKeyTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!TestTrue(TEXT("Real widgets and separate sources initialized"), F.Initialize()))
    {
        return false;
    }
    TestEqual(TEXT("Camera defaults off"), F.Camera->GetCameraState(), EConstructCameraState::Off);
    TestFalse(TEXT("Viewing defaults off"), F.Panel->IsShowingVideo());
    TestNull(TEXT("No preview while off"), F.Panel->GetPreviewBrush()->GetResourceObject());
    const auto& Keys = FConstructWebcamUITestAccess::Input(*F.Panel)->KeyBindings;
    TestTrue(TEXT("C binding exists"), Keys.ContainsByPredicate([](const FInputKeyBinding& B) { return B.Chord.Key == EKeys::C; }));
    TestTrue(TEXT("F6 binding exists"), Keys.ContainsByPredicate([](const FInputKeyBinding& B) { return B.Chord.Key == EKeys::F6; }));
    F.Gate->SetModal(TEXT("OtherScreen"), true);
    F.PressCameraKey();
    TestEqual(TEXT("Other screen blocks C"), F.Camera->GetCameraState(), EConstructCameraState::Off);
    F.Gate->SetModal(TEXT("OtherScreen"), false);
    // The real source refuses outside a game world before enumerating or opening any camera.
    F.PressCameraKey();
    TestEqual(TEXT("Real source refusal reaches panel"), F.Panel->GetReasonText().ToString(), FString(TEXT("Webcams are disabled for this project.")));
    TestFalse(TEXT("Refused source never shows CAMERA ON"), F.Panel->IsCameraOn());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIReasonsTest, "Construct.Webcam.UI.ReasonsAndProjectDisabledCard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIReasonsTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!TestTrue(TEXT("Player fixture initialized"), F.Initialize()))
    {
        return false;
    }
    const TCHAR* Expected[] = { TEXT(""), TEXT("Webcams are disabled for this project."), TEXT("No camera found."),
        TEXT("Sign in to use your camera."), TEXT("Checking whether this app records video."),
        TEXT("Video sending is off in your settings."), TEXT("Needs a parent's consent."),
        TEXT("Parental consent is not available yet."), TEXT("Try again later."),
        TEXT("The app does not permit video."), TEXT("Camera capture failed.") };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Expected); ++Index)
    {
        TestEqual(TEXT("Reason wording"), UConstructWebcamPanel::ReasonText(static_cast<EConstructCameraBlockedReason>(Index)).ToString(), FString(Expected[Index]));
    }
    const TSharedPtr<SWidget> Card = FindOverlay(F.Slate.ToSharedRef());
    if (!TestTrue(TEXT("Actual overlay exists"), Card.IsValid()))
    {
        return false;
    }
    TGuardValue<bool> DisabledProject(GetMutableDefault<UConstructDeveloperSettings>()->bEnableWebcam, false);
    F.Panel->BindSources(F.Camera.Get(), F.Feeds.Get());
    Card->UpdateAllAttributes();
    TestFalse(TEXT("Disabled project makes controls unavailable"), F.Panel->IsAvailable());
    TestTrue(TEXT("Actual card collapses"), Card->GetVisibility() == EVisibility::Collapsed);
    TGuardValue<bool> EnabledProject(GetMutableDefault<UConstructDeveloperSettings>()->bEnableWebcam, true);
    F.Panel->BindSources(F.Camera.Get(), F.Feeds.Get());
    Card->UpdateAllAttributes();
    TestTrue(TEXT("Enabled card restores visibility"), Card->GetVisibility().IsVisible());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIRecordingTest, "Construct.Webcam.UI.RecordingNoticeBeforeCapture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIRecordingTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!F.Initialize())
    {
        return false;
    }
    TestTrue(TEXT("No fabricated notice"), F.Panel->GetRecordingText().IsEmpty());
    FConstructWebcamUITestAccess::Record(*F.Panel, true);
    TestFalse(TEXT("Recording event displays notice before capture"), F.Panel->GetRecordingText().IsEmpty());
    TestEqual(TEXT("Camera has not started"), F.Camera->GetCameraState(), EConstructCameraState::Off);
    FConstructWebcamUITestAccess::Record(*F.Panel, false);
    TestTrue(TEXT("Recording ended clears notice"), F.Panel->GetRecordingText().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIRemoteFeedTest, "Construct.Webcam.UI.RemotePresentationReplacementAndClear", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIRemoteFeedTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!F.Initialize())
    {
        return false;
    }
    AConstructPresencePawn* Pawn = F.World->SpawnActor<AConstructPresencePawn>();
    FString ForgedName;
    for (int32 Index = 0; Index < 200; ++Index)
    {
        ForgedName += TEXT("X\n");
    }
    FStrProperty* NameField = FindFProperty<FStrProperty>(Pawn->GetClass(), TEXT("PlayerName"));
    if (!TestNotNull(TEXT("Construct-owned view-state name field exists"), NameField))
    {
        return false;
    }
    NameField->SetPropertyValue_InContainer(Pawn, ForgedName);
    FConstructWebcamFeed Feed;
    Feed.Texture = NewObject<UTexture2D>();
    Feed.SenderId = TEXT("presentation-only");
    Feed.Generation = 1;
    FConstructWebcamUITestAccess::Present(*F.Panel, Pawn, Feed);
    TStrongObjectPtr<UConstructWebcamFeedWidget> Widget(FConstructWebcamUITestAccess::Widget(*F.Panel));
    TestTrue(TEXT("Texture displayed"), Widget.IsValid() && Widget->HasPicture());
    const TSharedPtr<STextBlock> NameLabel = FindFirstText(Widget->TakeWidget());
    if (!TestTrue(TEXT("Actual feed name label exists"), NameLabel.IsValid()))
    {
        return false;
    }
    NameLabel->UpdateAllAttributes();
    TestEqual(TEXT("Forged multiline name cannot expand feed layout"), NameLabel->GetText().ToString(), FString(TEXT("X")));
    TestEqual(TEXT("Display formatting never rewrites raw replicated name"), Pawn->GetPlayerName(), ForgedName);
    F.Panel->ToggleVideo();
    FConstructWebcamUITestAccess::Present(*F.Panel, Pawn, Feed);
    TStrongObjectPtr<UConstructWebcamFeedWidget> HiddenWidget(FConstructWebcamUITestAccess::Widget(*F.Panel));
    Pawn->SetActorHiddenInGame(true);
    F.Feeds->OnFeedChanged.Broadcast(Pawn);
    TestEqual(TEXT("Hidden pawn feed event clears immediately"), FConstructWebcamUITestAccess::RemoteCount(*F.Panel), 0);
    TestFalse(TEXT("Hidden pawn drops retained image immediately"), HiddenWidget->HasPicture());
    Pawn->SetActorHiddenInGame(false);
    FConstructWebcamUITestAccess::Present(*F.Panel, Pawn, Feed);
    Widget.Reset(FConstructWebcamUITestAccess::Widget(*F.Panel));
    Feed.Generation = 2;
    FConstructWebcamUITestAccess::Present(*F.Panel, Pawn, Feed);
    TestEqual(TEXT("Replacement reuses component"), FConstructWebcamUITestAccess::RemoteCount(*F.Panel), 1);
    TestTrue(TEXT("Replacement reuses widget"), FConstructWebcamUITestAccess::Widget(*F.Panel) == Widget.Get());
    FConstructWebcamUITestAccess::Present(*F.Panel, Pawn, FConstructWebcamFeed());
    TestEqual(TEXT("Empty feed removes component immediately"), FConstructWebcamUITestAccess::RemoteCount(*F.Panel), 0);
    TestFalse(TEXT("Retained widget drops texture"), Widget->HasPicture());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIModalTest, "Construct.Webcam.UI.EscapePreservesOtherModal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIModalTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!F.Initialize())
    {
        return false;
    }
    F.Panel->ToggleSettings();
    TestTrue(TEXT("Settings opens"), F.Panel->IsSettingsOpen());
    TestFalse(TEXT("Dialog blocks gameplay"), F.Gate->IsGameplayInputAllowed());
    F.Gate->SetModal(TEXT("OtherScreen"), true);
    TestTrue(TEXT("Escape consumes close"), FConstructWebcamUITestAccess::Escape(*F.Panel).IsEventHandled());
    TestFalse(TEXT("Settings closes"), F.Panel->IsSettingsOpen());
    TestFalse(TEXT("Other modal keeps gameplay blocked"), F.Gate->IsGameplayInputAllowed());
    F.Gate->SetModal(TEXT("OtherScreen"), false);
    TestTrue(TEXT("Final release restores gameplay"), F.Gate->IsGameplayInputAllowed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIShutdownTest, "Construct.Webcam.UI.ShutdownReleasesBothSourcesAndInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIShutdownTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!F.Initialize())
    {
        return false;
    }
    AConstructPresencePawn* Pawn = F.World->SpawnActor<AConstructPresencePawn>();
    FConstructWebcamFeed Feed;
    Feed.Texture = NewObject<UTexture2D>();
    Feed.SenderId = TEXT("presentation-only");
    FConstructWebcamUITestAccess::Present(*F.Panel, Pawn, Feed);
    F.Panel->ToggleSettings();
    TStrongObjectPtr<UInputComponent> Input(FConstructWebcamUITestAccess::Input(*F.Panel));
    TStrongObjectPtr<UConstructWebcamFeedWidget> Widget(FConstructWebcamUITestAccess::Widget(*F.Panel));
    TStrongObjectPtr<UWidgetComponent> Component(FConstructWebcamUITestAccess::Component(*F.Panel));
    TestTrue(TEXT("Camera subscribed"), F.Camera->OnCameraStateChanged.IsBound());
    TestTrue(TEXT("Feeds subscribed"), F.Feeds->OnFeedChanged.IsBound());
    TestTrue(TEXT("Timer active"), FConstructWebcamUITestAccess::HasTimer(*F.Panel));
    F.Panel->Shutdown();
    TestFalse(TEXT("Camera unsubscribed"), F.Camera->OnCameraStateChanged.IsBound());
    TestFalse(TEXT("Feeds unsubscribed"), F.Feeds->OnFeedChanged.IsBound());
    TestTrue(TEXT("Input unregistered"), Input.IsValid() && !Input->IsRegistered());
    TestTrue(TEXT("Remote unregistered"), Component.IsValid() && !Component->IsRegistered());
    TestTrue(TEXT("Old image cleared"), Widget.IsValid() && !Widget->HasPicture());
    TestFalse(TEXT("Timer cleared"), FConstructWebcamUITestAccess::HasTimer(*F.Panel));
    TestTrue(TEXT("Gameplay restored"), F.Gate->IsGameplayInputAllowed());
    F.Camera->OnCameraStateChanged.Broadcast(EConstructCameraState::On, EConstructCameraBlockedReason::None);
    F.Feeds->OnFeedChanged.Broadcast(Pawn);
    TestFalse(TEXT("Late events cannot revive panel"), F.Panel->CanInteract());
    F.Panel->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIReplyGuardTest, "Construct.Webcam.UI.SingleUseAndLateGenerationGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIReplyGuardTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!F.Initialize())
    {
        return false;
    }
    TStrongObjectPtr<UConstructWebcamSafetyRequest> Reply(FConstructWebcamUITestAccess::Reply(*F.Panel, EConstructWebcamSafetyAction::ReadSettings));
    Reply->SettingsReceived(AllowedSettings());
    TestTrue(TEXT("First reply confirms settings"), F.Panel->AreSettingsKnown());
    const FString Scope = F.Panel->GetScopeText(ECrowdyCommunicationSetting::VideoSend).ToString();
    Reply->SettingsReceived(FCrowdyCommunicationSettings());
    TestEqual(TEXT("Duplicate cannot overwrite settings"), F.Panel->GetScopeText(ECrowdyCommunicationSetting::VideoSend).ToString(), Scope);
    TStrongObjectPtr<UConstructWebcamSafetyRequest> Late(FConstructWebcamUITestAccess::Reply(*F.Panel, EConstructWebcamSafetyAction::Report));
    F.Panel->BindSources(F.Camera.Get(), F.Feeds.Get());
    FConstructWebcamUITestAccess::Enable(*F.Panel);
    F.Panel->ToggleSettings();
    const FString Message = F.Panel->GetMessage().ToString();
    FConstructWebcamUITestAccess::Retain(*F.Panel, Late.Get());
    Late->Done();
    Late->SettingsReceived(AllowedSettings());
    TestTrue(TEXT("Old completion cannot close current dialog"), F.Panel->IsSettingsOpen());
    TestFalse(TEXT("Old completion cannot confirm new settings"), F.Panel->AreSettingsKnown());
    TestEqual(TEXT("Old completion cannot replace message"), F.Panel->GetMessage().ToString(), Message);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIFailureTest, "Construct.Webcam.UI.SettingsFailuresPreserveConfirmation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIFailureTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!F.Initialize())
    {
        return false;
    }
    FConstructWebcamUITestAccess::Reply(*F.Panel, EConstructWebcamSafetyAction::ReadSettings)->SettingsReceived(AllowedSettings());
    const FString Scope = F.Panel->GetScopeText(ECrowdyCommunicationSetting::VideoSend).ToString();
    TestTrue(TEXT("Choice and applied policy distinct"), Scope.Contains(TEXT("Everyone")) && Scope.Contains(TEXT("Contacts")));
    const ECrowdySafetyErrorCode Codes[] = { ECrowdySafetyErrorCode::RateLimited, ECrowdySafetyErrorCode::ConsentUnavailable, ECrowdySafetyErrorCode::NetworkError };
    const TCHAR* Messages[] = { TEXT("Try again later."), TEXT("Parental consent is not available yet."), TEXT("Connection interrupted") };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Codes); ++Index)
    {
        FCrowdySafetyError Error;
        Error.Code = Codes[Index];
        Error.Message = TEXT("Connection interrupted");
        FConstructWebcamUITestAccess::Reply(*F.Panel, EConstructWebcamSafetyAction::ChangeScope)->Failed(Error);
        TestFalse(TEXT("Failure releases request"), F.Panel->IsBusy());
        TestTrue(TEXT("Confirmation preserved"), F.Panel->AreSettingsKnown());
        TestEqual(TEXT("Confirmed values preserved"), F.Panel->GetScopeText(ECrowdyCommunicationSetting::VideoSend).ToString(), Scope);
        TestEqual(TEXT("Actionable failure"), F.Panel->GetMessage().ToString(), FString(Messages[Index]));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIReportTest, "Construct.Webcam.UI.ReportSnapshotSurvivesDeparture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIReportTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!F.Initialize())
    {
        return false;
    }
    // Snapshot retention only, not platform owner confirmation or OpenReport.
    FConstructWebcamUITestAccess::PrimeReport(*F.Panel);
    const FConstructWebcamReportTarget Before = FConstructWebcamUITestAccess::Report(*F.Panel);
    AConstructPresencePawn* Pawn = F.World->SpawnActor<AConstructPresencePawn>();
    F.Feeds->OnFeedChanged.Broadcast(Pawn);
    Pawn->Destroy();
    const FConstructWebcamReportTarget After = FConstructWebcamUITestAccess::Report(*F.Panel);
    TestTrue(TEXT("Report remains open"), F.Panel->IsReportOpen());
    TestEqual(TEXT("User retained"), After.UserId, Before.UserId);
    TestEqual(TEXT("Sender retained"), After.SenderId, Before.SenderId);
    TestEqual(TEXT("Generation retained"), After.Generation, Before.Generation);
    TestTrue(TEXT("Time retained"), After.OccurredAt == Before.OccurredAt);
    TestEqual(TEXT("Original name retained"), F.Panel->GetReportHeading().ToString(), FString(TEXT("Report Original player")));
    F.Panel->CloseDialog();
    TestEqual(TEXT("Close drops identity"), FConstructWebcamUITestAccess::Report(*F.Panel).UserId, int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamUIAppliedOffTest, "Construct.Webcam.UI.SuccessfulSettingChangeClearsAppliedOff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructWebcamUIAppliedOffTest::RunTest(const FString&)
{
    FWebcamUIFixture F;
    if (!TestTrue(TEXT("Real source bindings initialized"), F.Initialize()))
    {
        return false;
    }
    F.Panel->ToggleVideo();
    TestTrue(TEXT("Viewing requested"), F.Panel->IsShowingVideo());
    AConstructPresencePawn* Pawn = F.World->SpawnActor<AConstructPresencePawn>();
    FConstructWebcamFeed Feed;
    Feed.Texture = NewObject<UTexture2D>();
    Feed.SenderId = TEXT("presentation-only");
    FConstructWebcamUITestAccess::Present(*F.Panel, Pawn, Feed);
    TStrongObjectPtr<UConstructWebcamFeedWidget> Widget(FConstructWebcamUITestAccess::Widget(*F.Panel));
    TestTrue(TEXT("Image initially retained"), Widget.IsValid() && Widget->HasPicture());
    // No SDK collection exists, so refreshing these real subsystems makes no network call.
    FConstructWebcamUITestAccess::Reply(*F.Panel, EConstructWebcamSafetyAction::ChangeScope)->SettingsReceived(FCrowdyCommunicationSettings());
    TestTrue(TEXT("Successful change confirms settings"), F.Panel->AreSettingsKnown());
    TestFalse(TEXT("Applied Off ends viewing choice"), F.Panel->IsShowingVideo());
    TestEqual(TEXT("Applied Off removes remote component"), FConstructWebcamUITestAccess::RemoteCount(*F.Panel), 0);
    TestFalse(TEXT("Applied Off clears retained image"), Widget->HasPicture());
    TestEqual(TEXT("Settings change never starts camera"), F.Camera->GetCameraState(), EConstructCameraState::Off);
    return true;
}

#endif
