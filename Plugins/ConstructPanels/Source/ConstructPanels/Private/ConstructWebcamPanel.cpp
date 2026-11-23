#include "ConstructWebcamPanel.h"

#include "ConstructPresencePawn.h"
#include "ConstructWebcamFeedWidget.h"
#include "ConstructWebcamSafetyRequest.h"
#include "ConstructUIInputSubsystem.h"
#include "ConstructDeveloperSettings.h"
#include "ConstructWebcamSubsystem.h"
#include "ConstructWebcamFeeds.h"
#include "Components/InputComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Subsystem/CrowdyGameSession.h"
#include "TimerManager.h"
#include "Widgets/Input/SEditableTextBox.h"

namespace
{
const FName WebcamModal(TEXT("Construct.Webcam"));

FText ScopeName(ECrowdyCommunicationScope Scope)
{
    switch (Scope)
    {
    case ECrowdyCommunicationScope::Off: return FText::FromString(TEXT("Off"));
    case ECrowdyCommunicationScope::Contacts: return FText::FromString(TEXT("Contacts"));
    case ECrowdyCommunicationScope::Everyone: return FText::FromString(TEXT("Everyone"));
    default: return FText::GetEmpty();
    }
}
}

void UConstructWebcamPanel::BindSources(UConstructWebcamSubsystem* CameraSource, UConstructWebcamFeeds* FeedSource)
{
    Shutdown();
    if (!CameraSource || !FeedSource || !GetOwningPlayer() || !GetOwningLocalPlayer() || !GetWorld())
    {
        return;
    }
    Camera = CameraSource;
    Feeds = FeedSource;
    bEnabledForWorld = GetDefault<UConstructDeveloperSettings>()->bEnableWebcam;
    BoundWorld = GetWorld();
    InputOwner = GetOwningLocalPlayer();
    Session = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyGameSession>() : nullptr;
    SessionUser = Session.IsValid() ? Session->GetUserID() : 0;
    SessionTokenId = Session.IsValid() ? Session->GetSessionGameTokenID() : 0;
    ++Generation;
    bActive = true;
    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Camera->OnCameraStateChanged.AddUniqueDynamic(this, &ThisClass::CameraChanged);
    Feeds->OnFeedChanged.AddUniqueDynamic(this, &ThisClass::FeedChanged);
    if (UCrowdySafety* Controls = Safety())
    {
        Controls->OnRecordingStateChanged.AddUniqueDynamic(this, &ThisClass::RecordingChanged);
        const FCrowdyAppRecording Recording = Controls->GetRecordingState();
        bSafetyRecorded = Recording.bKnown && Recording.bVideo;
    }
    CameraInput = NewObject<UInputComponent>(GetOwningPlayer());
    CameraInput->Priority = 1;
    CameraInput->RegisterComponent();
    CameraInput->BindKey(EKeys::C, IE_Pressed, this, &ThisClass::ToggleCamera).bConsumeInput = false;
    CameraInput->BindKey(EKeys::F6, IE_Pressed, this, &ThisClass::ToggleVideo).bConsumeInput = false;
    GetOwningPlayer()->PushInputComponent(CameraInput);
    BoundWorld->GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::Refresh, 0.1f, true);
    SetNotice();
    Refresh();
}

bool UConstructWebcamPanel::IsCurrent(uint64 ExpectedGeneration) const
{
    APlayerController* Controller = GetOwningPlayer();
    UCrowdyGameSession* Current = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyGameSession>() : nullptr;
    return bActive && Generation == ExpectedGeneration && Camera && Feeds && BoundWorld.IsValid() &&
        IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->GetWorld() == BoundWorld.Get() &&
        Controller->GetLocalPlayer() == InputOwner.Get() && InputOwner.IsValid() && Current == Session.Get() &&
        (!Current || (Current->GetUserID() == SessionUser && Current->GetSessionGameTokenID() == SessionTokenId && !Current->HasAccessEnded()));
}

UCrowdySafety* UConstructWebcamPanel::Safety() const
{
    return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdySafety>() : nullptr;
}

bool UConstructWebcamPanel::IsAvailable() const
{
    return bActive && bEnabledForWorld && Camera && Feeds && Camera->GetBlockedReason() != EConstructCameraBlockedReason::WebcamsDisabled;
}

bool UConstructWebcamPanel::CanInteract() const
{
    const ULocalPlayer* Player = InputOwner.Get();
    const UConstructUIInputSubsystem* Gate = Player ? Player->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
    return IsCurrent(Generation) && IsAvailable() && Gate && Gate->CanUseNavigationInput(WebcamModal);
}

bool UConstructWebcamPanel::IsCameraOn() const
{
    return Camera && Camera->GetCameraState() == EConstructCameraState::On;
}

FText UConstructWebcamPanel::GetCameraCaption() const
{
    if (!Camera)
    {
        return FText::FromString(TEXT("Camera unavailable"));
    }
    switch (Camera->GetCameraState())
    {
    case EConstructCameraState::On: return FText::FromString(TEXT("Camera on  [C]"));
    case EConstructCameraState::Starting: return FText::FromString(TEXT("Starting camera  [C]"));
    case EConstructCameraState::Unavailable: return FText::FromString(TEXT("Camera unavailable"));
    default: return FText::FromString(TEXT("Camera off  [C]"));
    }
}

FText UConstructWebcamPanel::ReasonText(EConstructCameraBlockedReason Reason)
{
    switch (Reason)
    {
    case EConstructCameraBlockedReason::None: return FText::GetEmpty();
    case EConstructCameraBlockedReason::WebcamsDisabled: return FText::FromString(TEXT("Webcams are disabled for this project."));
    case EConstructCameraBlockedReason::NoCamera: return FText::FromString(TEXT("No camera found."));
    case EConstructCameraBlockedReason::NotSignedIn: return FText::FromString(TEXT("Sign in to use your camera."));
    case EConstructCameraBlockedReason::CheckingRecording: return FText::FromString(TEXT("Checking whether this app records video."));
    case EConstructCameraBlockedReason::SendingOff: return FText::FromString(TEXT("Video sending is off in your settings."));
    case EConstructCameraBlockedReason::NeedsParentConsent: return FText::FromString(TEXT("Needs a parent's consent."));
    case EConstructCameraBlockedReason::ConsentUnavailable: return FText::FromString(TEXT("Parental consent is not available yet."));
    case EConstructCameraBlockedReason::TryAgainLater: return FText::FromString(TEXT("Try again later."));
    case EConstructCameraBlockedReason::AppDoesNotPermit: return FText::FromString(TEXT("The app does not permit video."));
    case EConstructCameraBlockedReason::CaptureFailed: return FText::FromString(TEXT("Camera capture failed."));
    default: return FText::FromString(TEXT("Camera unavailable."));
    }
}

FText UConstructWebcamPanel::GetReasonText() const
{
    return Camera ? ReasonText(Camera->GetBlockedReason()) : FText::GetEmpty();
}

FText UConstructWebcamPanel::GetRecordingText() const
{
    return bNoticeVisible ? RecordingNotice : FText::GetEmpty();
}

void UConstructWebcamPanel::SetNotice()
{
    const bool bRecorded = bSafetyRecorded || (Camera && Camera->IsBeingRecorded());
    bNoticeVisible = bRecorded;
    RecordingNotice = bRecorded
        ? FText::FromString(TEXT("This app records video while your camera is on."))
        : FText::GetEmpty();
}

void UConstructWebcamPanel::ToggleCamera()
{
    if (!CanInteract() || !Camera)
    {
        return;
    }
    const EConstructCameraState State = Camera->GetCameraState();
    // Publish the recording notice before asking the source to open capture.
    SetNotice();
    Camera->SetCameraOn(State != EConstructCameraState::On && State != EConstructCameraState::Starting);
    Refresh();
}

void UConstructWebcamPanel::ToggleVideo()
{
    if (!CanInteract())
    {
        return;
    }
    bShowVideo = !bShowVideo;
    if (bSettingsKnown && Settings.VideoReceive == ECrowdyCommunicationScope::Off && bShowVideo)
    {
        bShowVideo = false;
        Message = FText::FromString(TEXT("Video receiving is off in your settings."));
    }
    Feeds->SetShowVideo(bShowVideo);
    if (!bShowVideo)
    {
        ReleaseRemotes();
    }
    NextDiscovery = 0.0;
    Refresh();
}

void UConstructWebcamPanel::CameraChanged(EConstructCameraState, EConstructCameraBlockedReason)
{
    if (!bActive)
    {
        return;
    }
    SetNotice();
    if (!IsAvailable())
    {
        CloseDialog();
        bShowVideo = false;
        Feeds->SetShowVideo(false);
        ReleaseRemotes();
    }
    Refresh();
}

void UConstructWebcamPanel::RecordingChanged(const FCrowdyAppRecording& Recording)
{
    bSafetyRecorded = Recording.bKnown && Recording.bVideo;
    SetNotice();
}

void UConstructWebcamPanel::Refresh()
{
    if (!IsCurrent(Generation))
    {
        Shutdown();
        return;
    }
    PreviewBrush.ImageSize = FVector2D(128, 96);
    PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
    PreviewBrush.SetResourceObject(IsAvailable() ? Camera->GetLocalPreview() : nullptr);
    if (!IsAvailable() || !bShowVideo)
    {
        ReleaseRemotes();
        return;
    }
    const double Now = BoundWorld->GetTimeSeconds();
    if (Now < NextDiscovery)
    {
        return;
    }
    NextDiscovery = Now + 0.25;
    for (int32 Index = Remotes.Num() - 1; Index >= 0; --Index)
    {
        AConstructPresencePawn* Pawn = Remotes[Index].Pawn.Get();
        const UCrowdyEntityComponent* Entity = Pawn ? Pawn->FindComponentByClass<UCrowdyEntityComponent>() : nullptr;
        if (!Pawn || Pawn->IsHidden() || Pawn->IsActorBeingDestroyed() || !Entity || Entity->GetNetID() != Remotes[Index].EntityId)
        {
            RemoveRemote(Index);
        }
    }
    for (TActorIterator<AConstructPresencePawn> It(BoundWorld.Get()); It; ++It)
    {
        AConstructPresencePawn* Pawn = *It;
        if (Pawn != GetOwningPlayer()->GetPawn() && !Pawn->IsHidden() && !Pawn->IsActorBeingDestroyed())
        {
            RefreshRemote(Pawn);
        }
    }
}

void UConstructWebcamPanel::FeedChanged(APawn* Pawn)
{
    if (IsCurrent(Generation) && bShowVideo && IsAvailable())
    {
        RefreshRemote(Cast<AConstructPresencePawn>(Pawn));
    }
}

void UConstructWebcamPanel::RefreshRemote(AConstructPresencePawn* Pawn)
{
    if (!Pawn)
    {
        return;
    }
    if (Pawn == GetOwningPlayer()->GetPawn() || Pawn->GetWorld() != BoundWorld.Get() || Pawn->IsHidden() || Pawn->IsActorBeingDestroyed())
    {
        PresentFeed(Pawn, FConstructWebcamFeed());
        return;
    }
    PresentFeed(Pawn, Feeds->GetFeedInfo(Pawn));
}

void UConstructWebcamPanel::PresentFeed(AConstructPresencePawn* Pawn, const FConstructWebcamFeed& Info)
{
    int32 Index = Remotes.IndexOfByPredicate([Pawn](const FConstructWebcamRemoteUI& Entry) { return Entry.Pawn == Pawn; });
    if (!Info.Texture || Info.SenderId.IsEmpty())
    {
        if (Index != INDEX_NONE)
        {
            RemoveRemote(Index);
        }
        return;
    }
    Pawn->GetConfirmedUserId();
    if (Index == INDEX_NONE)
    {
        UConstructWebcamFeedWidget* Widget = CreateWidget<UConstructWebcamFeedWidget>(GetOwningPlayer());
        if (!Widget)
        {
            return;
        }
        UWidgetComponent* Component = NewObject<UWidgetComponent>(Pawn);
        Component->SetWidgetSpace(EWidgetSpace::Screen);
        Component->SetOwnerPlayer(InputOwner.Get());
        Component->SetDrawSize(FVector2D(160, 150));
        Component->SetPivot(FVector2D(0.5, 1.0));
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetWidget(Widget);
        Component->SetupAttachment(Pawn->GetRootComponent());
        Component->SetRelativeLocation(FVector(0, 0, 210));
        Pawn->AddInstanceComponent(Component);
        Component->RegisterComponent();
        Widget->BindFeed(this, Pawn);
        FConstructWebcamRemoteUI Entry;
        Entry.Pawn = Pawn;
        Entry.Widget = Widget;
        Entry.Component = Component;
        if (const UCrowdyEntityComponent* Entity = Pawn->FindComponentByClass<UCrowdyEntityComponent>())
        {
            Entry.EntityId = Entity->GetNetID();
        }
        Index = Remotes.Add(Entry);
    }
    Remotes[Index].Widget->SetFeed(Info);
}

void UConstructWebcamPanel::RemoveRemote(int32 Index)
{
    FConstructWebcamRemoteUI Entry = Remotes[Index];
    Remotes.RemoveAt(Index);
    if (Entry.Widget)
    {
        Entry.Widget->SetFeed(FConstructWebcamFeed());
        Entry.Widget->RemoveFromParent();
    }
    if (Entry.Component)
    {
        Entry.Component->SetWidget(nullptr);
        Entry.Component->DestroyComponent();
    }
}

void UConstructWebcamPanel::ReleaseRemotes()
{
    while (!Remotes.IsEmpty())
    {
        RemoveRemote(Remotes.Num() - 1);
    }
}

bool UConstructWebcamPanel::CanModerate(AConstructPresencePawn* Pawn) const
{
    if (!CanInteract() || !Pawn || !bShowVideo || !Camera)
    {
        return false;
    }
    const FConstructWebcamFeed Info = Feeds->GetFeedInfo(Pawn);
    return Info.Texture && !Info.SenderId.IsEmpty() && Pawn->GetConfirmedUserId() > 0;
}

void UConstructWebcamPanel::SetDialog(bool bSettings, bool bReport)
{
    bSettingsOpen = bSettings;
    bReportOpen = bReport;
    if (ULocalPlayer* Player = InputOwner.Get())
    {
        if (UConstructUIInputSubsystem* Gate = Player->GetSubsystem<UConstructUIInputSubsystem>())
        {
            Gate->SetModal(WebcamModal, bSettings || bReport, bSettings || bReport ? this : nullptr);
        }
    }
}

void UConstructWebcamPanel::ToggleSettings()
{
    if (!CanInteract())
    {
        return;
    }
    if (bSettingsOpen || bReportOpen)
    {
        CloseDialog();
        return;
    }
    SetDialog(true, false);
    ReadSettings();
}

void UConstructWebcamPanel::CloseDialog()
{
    SetDialog(false, false);
    ReportTarget = FConstructWebcamReportTarget();
    if (ParentEmail)
    {
        ParentEmail->SetText(FText::GetEmpty());
    }
    if (ReportDetails)
    {
        ReportDetails->SetText(FText::GetEmpty());
    }
}

FText UConstructWebcamPanel::GetReportHeading() const
{
    return FText::Format(FText::FromString(TEXT("Report {0}")), FText::FromString(ReportTarget.DisplayName));
}

FText UConstructWebcamPanel::GetScopeText(ECrowdyCommunicationSetting Setting) const
{
    if (!bSettingsKnown)
    {
        return FText::FromString(TEXT("Not loaded"));
    }
    const bool bSend = Setting == ECrowdyCommunicationSetting::VideoSend;
    const ECrowdyCommunicationScope Choice = bSend ? Settings.Choices.VideoSend : Settings.Choices.VideoReceive;
    const ECrowdyCommunicationScope Applied = bSend ? Settings.VideoSend : Settings.VideoReceive;
    return FText::Format(FText::FromString(TEXT("Chosen: {0}   Applied: {1}")), ScopeName(Choice), ScopeName(Applied));
}

UConstructWebcamSafetyRequest* UConstructWebcamPanel::BeginRequest(uint8 Kind)
{
    if (!CanInteract() || IsBusy() || !Safety())
    {
        if (!Safety())
        {
            Message = FText::FromString(TEXT("Sign in to use player safety settings."));
        }
        return nullptr;
    }
    UConstructWebcamSafetyRequest* Request = NewObject<UConstructWebcamSafetyRequest>(this);
    Request->Panel = this;
    Request->Generation = Generation;
    Request->Action = static_cast<EConstructWebcamSafetyAction>(Kind);
    Requests.Add(Request);
    Message = FText::FromString(TEXT("Working..."));
    return Request;
}

bool UConstructWebcamPanel::Consume(UConstructWebcamSafetyRequest* Request)
{
    const bool bCurrent = Request && IsCurrent(Request->Generation) && Requests.Contains(Request);
    Requests.Remove(Request);
    return bCurrent;
}

void UConstructWebcamPanel::ReadSettings()
{
    UConstructWebcamSafetyRequest* Request = BeginRequest(static_cast<uint8>(EConstructWebcamSafetyAction::ReadSettings));
    if (!Request)
    {
        return;
    }
    FOnCommunicationSettings Done;
    Done.BindDynamic(Request, &UConstructWebcamSafetyRequest::SettingsReceived);
    FOnSafetyError Failed;
    Failed.BindDynamic(Request, &UConstructWebcamSafetyRequest::Failed);
    Safety()->GetCommunicationSettings(Done, Failed);
}

void UConstructWebcamPanel::SetScope(ECrowdyCommunicationSetting Setting, ECrowdyCommunicationScope Scope)
{
    if (!bSettingsOpen || !bSettingsKnown || (Setting != ECrowdyCommunicationSetting::VideoSend && Setting != ECrowdyCommunicationSetting::VideoReceive))
    {
        return;
    }
    UConstructWebcamSafetyRequest* Request = BeginRequest(static_cast<uint8>(EConstructWebcamSafetyAction::ChangeScope));
    if (!Request)
    {
        return;
    }
    FOnCommunicationSettings Done;
    Done.BindDynamic(Request, &UConstructWebcamSafetyRequest::SettingsReceived);
    FOnSafetyError Failed;
    Failed.BindDynamic(Request, &UConstructWebcamSafetyRequest::Failed);
    Safety()->SetCommunicationSetting(Setting, Scope, Done, Failed);
}

void UConstructWebcamPanel::AskParent(const FString& Email)
{
    if (!bSettingsOpen)
    {
        return;
    }
    UConstructWebcamSafetyRequest* Request = BeginRequest(static_cast<uint8>(EConstructWebcamSafetyAction::Consent));
    if (!Request)
    {
        return;
    }
    FOnConsentRequested Done;
    Done.BindDynamic(Request, &UConstructWebcamSafetyRequest::ConsentReceived);
    FOnSafetyError Failed;
    Failed.BindDynamic(Request, &UConstructWebcamSafetyRequest::Failed);
    Safety()->RequestParentalConsent(ECrowdyConsentFeature::Video, Email, Done, Failed);
    if (ParentEmail)
    {
        ParentEmail->SetText(FText::GetEmpty());
    }
}

void UConstructWebcamPanel::BlockPlayer(AConstructPresencePawn* Pawn)
{
    if (!CanModerate(Pawn))
    {
        return;
    }
    const int64 UserId = Pawn->GetConfirmedUserId();
    UConstructWebcamSafetyRequest* Request = BeginRequest(static_cast<uint8>(EConstructWebcamSafetyAction::Block));
    if (!Request)
    {
        return;
    }
    FOnSafetyDone Done;
    Done.BindDynamic(Request, &UConstructWebcamSafetyRequest::Done);
    FOnSafetyError Failed;
    Failed.BindDynamic(Request, &UConstructWebcamSafetyRequest::Failed);
    Safety()->BlockPlayer(UserId, Done, Failed);
}

void UConstructWebcamPanel::OpenReport(AConstructPresencePawn* Pawn)
{
    if (!CanModerate(Pawn) || IsBusy())
    {
        return;
    }
    const FConstructWebcamFeed Info = Feeds->GetFeedInfo(Pawn);
    ReportTarget.UserId = Pawn->GetConfirmedUserId();
    ReportTarget.SenderId = Info.SenderId;
    ReportTarget.Generation = Info.Generation;
    ReportTarget.OccurredAt = FDateTime::UtcNow();
    const FString DisplayName = Pawn->GetPlayerDisplayName();
    ReportTarget.DisplayName = DisplayName.IsEmpty() ? TEXT("player") : DisplayName;
    SetDialog(false, true);
}

void UConstructWebcamPanel::SubmitReport(ECrowdySafetyReportReason Reason, const FString& Details)
{
    if (!bReportOpen || ReportTarget.UserId <= 0 || ReportTarget.SenderId.IsEmpty())
    {
        return;
    }
    UConstructWebcamSafetyRequest* Request = BeginRequest(static_cast<uint8>(EConstructWebcamSafetyAction::Report));
    if (!Request)
    {
        return;
    }
    FOnSafetyDone Done;
    Done.BindDynamic(Request, &UConstructWebcamSafetyRequest::Done);
    FOnSafetyError Failed;
    Failed.BindDynamic(Request, &UConstructWebcamSafetyRequest::Failed);
    Safety()->FileReport(ReportTarget.UserId, Reason, Details, ReportTarget.SenderId, ReportTarget.OccurredAt, Done, Failed);
}

void UConstructWebcamPanel::StopInput()
{
    if (CameraInput)
    {
        if (APlayerController* Controller = Cast<APlayerController>(CameraInput->GetOwner()))
        {
            Controller->PopInputComponent(CameraInput);
        }
        CameraInput->DestroyComponent();
        CameraInput = nullptr;
    }
}

void UConstructWebcamPanel::Shutdown()
{
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    const bool bWasActive = bActive;
    bActive = false;
    ++Generation;
    if (BoundWorld.IsValid())
    {
        BoundWorld->GetTimerManager().ClearTimer(RefreshTimer);
    }
    CloseDialog();
    StopInput();
    Requests.Reset();
    ReleaseRemotes();
    if (Camera)
    {
        Camera->OnCameraStateChanged.RemoveDynamic(this, &ThisClass::CameraChanged);
        if (bWasActive)
        {
            Camera->SetCameraOn(false);
        }
    }
    if (Feeds)
    {
        Feeds->OnFeedChanged.RemoveDynamic(this, &ThisClass::FeedChanged);
        if (bWasActive)
        {
            Feeds->SetShowVideo(false);
        }
    }
    if (UCrowdySafety* Controls = Safety())
    {
        Controls->OnRecordingStateChanged.RemoveDynamic(this, &ThisClass::RecordingChanged);
    }
    Camera = nullptr;
    Feeds = nullptr;
    bEnabledForWorld = false;
    PreviewBrush.SetResourceObject(nullptr);
    bShowVideo = false;
    bSettingsKnown = false;
    bSafetyRecorded = false;
    bNoticeVisible = false;
    Message = FText::GetEmpty();
    InputOwner.Reset();
    BoundWorld.Reset();
    Session.Reset();
    bShuttingDown = false;
}

void UConstructWebcamPanel::NativeDestruct()
{
    Shutdown();
    Super::NativeDestruct();
}
