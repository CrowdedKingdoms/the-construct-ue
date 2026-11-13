#include "ConstructWebcamSubsystem.h"

#include "ConstructDeveloperSettings.h"
#include "ConstructPresencePawn.h"
#include "ConstructUIInputSubsystem.h"
#include "ConstructWebcamCore.h"
#include "ConstructWebcamLog.h"
#include "ConstructWebcamSettingsReply.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapperModule.h"
#include "IMediaCaptureSupport.h"
#include "IMediaEventSink.h"
#include "MediaCaptureSupport.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "Modules/ModuleManager.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "Tasks/Task.h"
#include "UObject/UObjectGlobals.h"

namespace
{
bool CanRunCamera(const FConstructWebcamConditions& Conditions)
{
	return Conditions.bAppActive && Conditions.bGameplayAllowed && Conditions.bHasLocalPlayer;
}

EConstructCameraBlockedReason ReasonFromSettings(const FCrowdyCommunicationSettings& Settings)
{
	if (Settings.bIsMinor && !Settings.bVideoConsent)
	{
		return EConstructCameraBlockedReason::NeedsParentConsent;
	}
	if (Settings.VideoSend == ECrowdyCommunicationScope::Off)
	{
		return EConstructCameraBlockedReason::SendingOff;
	}
	return EConstructCameraBlockedReason::None;
}

EConstructCameraBlockedReason ReasonFromError(const FCrowdySafetyError* Error)
{
	if (Error && Error->Code == ECrowdySafetyErrorCode::ConsentUnavailable)
	{
		return EConstructCameraBlockedReason::ConsentUnavailable;
	}
	return EConstructCameraBlockedReason::TryAgainLater;
}

void SelectSmallestWebcamFormat(UMediaPlayer& Player)
{
	const int32 Track = FMath::Max(0, Player.GetSelectedTrack(EMediaPlayerTrack::Video));
	const int32 Formats = Player.GetNumTrackFormats(EMediaPlayerTrack::Video, Track);
	int32 Best = INDEX_NONE;
	int64 BestArea = MAX_int64;
	for (int32 Format = 0; Format < Formats; ++Format)
	{
		const FIntPoint Dim = Player.GetVideoTrackDimensions(Track, Format);
		const int64 Area = static_cast<int64>(Dim.X) * Dim.Y;
		if (Dim.X >= ConstructWebcam::FrameWidth && Dim.Y >= ConstructWebcam::FrameHeight && Area < BestArea)
		{
			Best = Format;
			BestArea = Area;
		}
	}
	if (Best != INDEX_NONE && Best != Player.GetTrackFormat(EMediaPlayerTrack::Video, Track))
	{
		Player.SetTrackFormat(EMediaPlayerTrack::Video, Track, Best);
	}
}
}

void UConstructWebcamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCrowdySDKSubsystem>();
	Collection.InitializeDependency<UCrowdyAuthentication>();
	Collection.InitializeDependency<UCrowdyGameSession>();
	Collection.InitializeDependency<UCrowdySafety>();
	Collection.InitializeDependency<UCrowdyVideoSender>();
	ImageWrapper = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));

	UGameInstance* Instance = GetGameInstance();
	if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
	{
		SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::HandleLogout);
	}
	if (UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>())
	{
		Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::HandleAccessEnded);
		Session->OnAppPausedChanged.AddUniqueDynamic(this, &ThisClass::HandleAppPaused);
	}
	if (UCrowdySafety* Safety = Instance->GetSubsystem<UCrowdySafety>())
	{
		Safety->OnRecordingStateChanged.AddUniqueDynamic(this, &ThisClass::HandleRecordingChanged);
	}
	if (UCrowdyVideoSender* Sender = Instance->GetSubsystem<UCrowdyVideoSender>())
	{
		Sender->OnVideoSendRefused.AddUniqueDynamic(this, &ThisClass::HandleSendRefused);
	}
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
}

void UConstructWebcamSubsystem::Deinitialize()
{
	bWanted = false;
	RefusalToken = 0;
	RefreshToken = 0;
	EndStart();
	StopTicker();
	SettingsReply = nullptr;
	for (UMediaPlayer* Retiring : RetiringPlayers)
	{
		if (Retiring)
		{
			Retiring->OnMediaEvent().RemoveAll(this);
			Retiring->Close();
		}
	}
	RetiringPlayers.Reset();
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PreLoadMapHandle.Reset();
	PostLoadMapHandle.Reset();
	UGameInstance* Instance = GetGameInstance();
	if (UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr)
	{
		SDK->OnLogout.RemoveDynamic(this, &ThisClass::HandleLogout);
	}
	if (UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr)
	{
		Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::HandleAccessEnded);
		Session->OnAppPausedChanged.RemoveDynamic(this, &ThisClass::HandleAppPaused);
	}
	if (UCrowdySafety* Safety = Instance ? Instance->GetSubsystem<UCrowdySafety>() : nullptr)
	{
		Safety->OnRecordingStateChanged.RemoveDynamic(this, &ThisClass::HandleRecordingChanged);
	}
	if (UCrowdyVideoSender* Sender = Instance ? Instance->GetSubsystem<UCrowdyVideoSender>() : nullptr)
	{
		Sender->OnVideoSendRefused.RemoveDynamic(this, &ThisClass::HandleSendRefused);
	}
	Super::Deinitialize();
}

void UConstructWebcamSubsystem::SetCameraOn(bool bOn)
{
	if (!bOn)
	{
		if (CameraState != EConstructCameraState::Unavailable)
		{
			ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
		}
		return;
	}
	const FConstructWebcamConditions Conditions = ReadConditions();
	if (!Conditions.bEnabled)
	{
		ClearChoice(EConstructCameraState::Unavailable, EConstructCameraBlockedReason::WebcamsDisabled);
		return;
	}
	if (bWanted)
	{
		return;
	}
	if (!FindCamera(CameraUrl))
	{
		ClearChoice(EConstructCameraState::Unavailable, EConstructCameraBlockedReason::NoCamera);
		return;
	}
	if (!Conditions.bSignedIn)
	{
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::NotSignedIn);
		return;
	}
	bWanted = true;
	RefusalToken = 0;
	EnsureTicker();
	Reconcile(Conditions);
}

UTexture2D* UConstructWebcamSubsystem::GetLocalPreview() const
{
	return CameraState == EConstructCameraState::On ? Preview.Get() : nullptr;
}

void UConstructWebcamSubsystem::RefreshSettings()
{
	RefreshToken = ++TokenCounter;
	RequestSettings(RefreshToken, false);
}

#if !UE_BUILD_SHIPPING
void UConstructWebcamSubsystem::SetFrameSourceForCheck(TFunction<bool(TArray<FColor>&)> Source,
                                                       bool bInIgnoreNearbyGate,
                                                       TFunction<void(const FCrowdyVideoFrameSend&)> OnSent)
{
	EndStart();
	CheckSource = MoveTemp(Source);
	CheckSent = MoveTemp(OnSent);
	bIgnoreNearbyGate = bInIgnoreNearbyGate;
	Reconcile(ReadConditions());
}

void UConstructWebcamSubsystem::ClearFrameSourceForCheck()
{
	EndStart();
	CheckSource = nullptr;
	CheckSent = nullptr;
	bIgnoreNearbyGate = false;
	FindCamera(CameraUrl);
	Reconcile(ReadConditions());
}
#endif

bool UConstructWebcamSubsystem::TickCamera(float DeltaTime)
{
	// Runs every frame and acts every FrameInterval, carrying the remainder so the frame rate does not drift below 10.
	FrameClock += DeltaTime;
	if (FrameClock < ConstructWebcam::FrameInterval)
	{
		return true;
	}
	FrameClock = FMath::Min(FrameClock - ConstructWebcam::FrameInterval, ConstructWebcam::FrameInterval);
	const FConstructWebcamConditions Conditions = ReadConditions();
	Reconcile(Conditions);
	if (ActiveGeneration != 0)
	{
		Advance(Conditions);
	}
	return true;
}

FConstructWebcamConditions UConstructWebcamSubsystem::ReadConditions()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.Conditions)
	{
		return Hooks.Conditions();
	}
#endif
	FConstructWebcamConditions Conditions;
	UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	if (EnabledWorld.Get() != World)
	{
		EnabledWorld = World;
		bEnabledForWorld = World && World->IsGameWorld() && GetDefault<UConstructDeveloperSettings>()->bEnableWebcam;
	}
	Conditions.World = World;
	Conditions.bEnabled = bEnabledForWorld;

	const UCrowdyAuthentication* Auth = Instance ? Instance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	Conditions.bSignedIn = Auth && Auth->IsSignedIn() && !Auth->IsLegalAcceptancePending() && Session &&
	                       !Session->HasAccessEnded();
	Conditions.bAppActive = Session && !Session->IsAppPaused();
	Conditions.UserId = Session ? Session->GetUserID() : 0;

	ULocalPlayer* Local = Instance ? Instance->GetFirstGamePlayer() : nullptr;
	APlayerController* Controller = Local && World ? Local->GetPlayerController(World) : nullptr;
	const UConstructUIInputSubsystem* Gate = Local ? Local->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
	Conditions.Controller = Controller;
	Conditions.bHasLocalPlayer = Controller && Controller->IsLocalController() && !Controller->IsActorBeingDestroyed();
	Conditions.bGameplayAllowed = Gate && Gate->IsGameplayInputAllowed();

	if (const UCrowdySafety* Safety = Instance ? Instance->GetSubsystem<UCrowdySafety>() : nullptr)
	{
		const FCrowdyAppRecording Recording = Safety->GetRecordingState();
		Conditions.bRecordingKnown = Recording.bKnown;
		Conditions.bRecordingVideo = Recording.bVideo;
	}
	return Conditions;
}

void UConstructWebcamSubsystem::Reconcile(const FConstructWebcamConditions& Conditions)
{
	UpdateRecording(Conditions.bRecordingKnown, Conditions.bRecordingVideo);
	if (!Conditions.bEnabled)
	{
		ClearChoice(EConstructCameraState::Unavailable, EConstructCameraBlockedReason::WebcamsDisabled);
		return;
	}
	if (BlockedReason == EConstructCameraBlockedReason::WebcamsDisabled)
	{
		SetState(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
	}
	if (!bWanted)
	{
		return;
	}
	if (!Conditions.bSignedIn)
	{
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::NotSignedIn);
		return;
	}
	const bool bStarted = ActiveGeneration != 0;
	if (bStarted && (StartWorld != Conditions.World || StartUserId != Conditions.UserId))
	{
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
		return;
	}
	if (bStarted && (!CanRunCamera(Conditions) || StartController != Conditions.Controller))
	{
		EndStart();
	}
	if (!CanRunCamera(Conditions))
	{
		SetState(EConstructCameraState::Starting, EConstructCameraBlockedReason::None);
		return;
	}
	if (ActiveGeneration == 0)
	{
		BeginStart(Conditions);
	}
}

void UConstructWebcamSubsystem::Advance(const FConstructWebcamConditions& Conditions)
{
	switch (Step)
	{
	case EStep::CheckingRecording:
		if (!Conditions.bRecordingKnown)
		{
			RequestRecordingState();
			SetState(EConstructCameraState::Starting, EConstructCameraBlockedReason::CheckingRecording);
			return;
		}
		// The camera opens on the next tick, so a recording notice is on screen before the first frame.
		Step = EStep::Notice;
		if (BlockedReason == EConstructCameraBlockedReason::None && IsBeingRecorded())
		{
			OnCameraStateChanged.Broadcast(CameraState, BlockedReason);
			return;
		}
		SetState(EConstructCameraState::Starting, EConstructCameraBlockedReason::None);
		return;
	case EStep::Notice:
		OpenCamera();
		return;
	case EStep::Capturing:
		PumpFrame();
		return;
	default:
		return;
	}
}

bool UConstructWebcamSubsystem::IsStartStillValid(const FConstructWebcamConditions& Conditions) const
{
	return Conditions.bEnabled && Conditions.bSignedIn && CanRunCamera(Conditions) && StartWorld == Conditions.World &&
	       StartController == Conditions.Controller && StartUserId == Conditions.UserId;
}

void UConstructWebcamSubsystem::BeginStart(const FConstructWebcamConditions& Conditions)
{
	ActiveGeneration = ++TokenCounter;
	StartWorld = Conditions.World;
	StartController = Conditions.Controller;
	StartUserId = Conditions.UserId;
	Step = EStep::ReadingSettings;
	Quality = ConstructWebcam::StartQuality;
	if (!ImageWrapper)
	{
		ImageWrapper = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	}
	const int32 Token = ActiveGeneration;
	SetState(EConstructCameraState::Starting, EConstructCameraBlockedReason::None);
	if (Token == ActiveGeneration)
	{
		RequestSettings(Token, false);
	}
}

void UConstructWebcamSubsystem::EndStart()
{
	// The generation ends before anything is released, so no completion can act on a closing camera.
	ActiveGeneration = 0;
	const bool bOpening = Step == EStep::Opening;
	Step = EStep::Idle;
	bFrameInFlight = false;
	bRecordingRequested = false;
	if (UMediaPlayer* Closing = Player)
	{
		Player = nullptr;
		Closing->OnMediaEvent().RemoveAll(this);
		if (bOpening)
		{
			Closing->OnMediaEvent().AddUObject(this, &ThisClass::HandleRetiringMediaEvent,
			                                   TWeakObjectPtr<UMediaPlayer>(Closing));
			RetiringPlayers.Add(Closing);
		}
		else
		{
			Closing->Close();
		}
	}
	Samples.Reset();
	Preview = nullptr;
}

void UConstructWebcamSubsystem::ClearChoice(EConstructCameraState State, EConstructCameraBlockedReason Reason)
{
	bWanted = false;
	EndStart();
	StopTicker();
	SetState(State, Reason);
}

void UConstructWebcamSubsystem::SetState(EConstructCameraState State, EConstructCameraBlockedReason Reason)
{
	if (CameraState == State && BlockedReason == Reason)
	{
		return;
	}
	CameraState = State;
	BlockedReason = Reason;
	UE_CLOG(IsConstructWebcamTraceOn(), LogConstructWebcam, Log, TEXT("ConstructWebcam: camera %s, %s"),
	        *UEnum::GetValueAsString(State), *UEnum::GetValueAsString(Reason));
	OnCameraStateChanged.Broadcast(State, Reason);
}

void UConstructWebcamSubsystem::UpdateRecording(bool bKnown, bool bVideo)
{
	const bool bWasRecorded = IsBeingRecorded();
	bRecordingKnown = bKnown;
	bRecordingVideo = bVideo;
	if (bWasRecorded != IsBeingRecorded())
	{
		OnCameraStateChanged.Broadcast(CameraState, BlockedReason);
	}
}

void UConstructWebcamSubsystem::RequestSettings(int32 Token, bool bAfterRefusal)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.ReadSettings)
	{
		Hooks.ReadSettings(Token, bAfterRefusal);
		return;
	}
#endif
	UGameInstance* Instance = GetGameInstance();
	UCrowdySafety* Safety = Instance ? Instance->GetSubsystem<UCrowdySafety>() : nullptr;
	if (!Safety)
	{
		SettingsAnswered(Token, bAfterRefusal, nullptr, nullptr);
		return;
	}
	TWeakObjectPtr<UConstructWebcamSubsystem> WeakThis(this);
	SettingsReply = UConstructWebcamSettingsReply::Read(*Safety, this,
	    [WeakThis, Token, bAfterRefusal](const FCrowdyCommunicationSettings* Settings, const FCrowdySafetyError* Error)
	    {
		    if (UConstructWebcamSubsystem* Self = WeakThis.Get())
		    {
			    Self->SettingsAnswered(Token, bAfterRefusal, Settings, Error);
		    }
	    });
}

void UConstructWebcamSubsystem::SettingsAnswered(int32 Token,
                                                 bool bAfterRefusal,
                                                 const FCrowdyCommunicationSettings* Settings,
                                                 const FCrowdySafetyError* Error)
{
	if (RefreshToken != 0 && Token == RefreshToken)
	{
		RefreshToken = 0;
		ApplyRefreshedSettings(Settings);
		return;
	}
	if (bAfterRefusal)
	{
		if (Token != RefusalToken || bWanted)
		{
			return;
		}
		RefusalToken = 0;
		const EConstructCameraBlockedReason Reason =
		    Settings ? ReasonFromSettings(*Settings) : EConstructCameraBlockedReason::None;
		const bool bExplained = Reason != EConstructCameraBlockedReason::None;
		SetState(EConstructCameraState::Off, bExplained ? Reason : EConstructCameraBlockedReason::AppDoesNotPermit);
		return;
	}
	if (Token != ActiveGeneration || Step != EStep::ReadingSettings)
	{
		return;
	}
	const FConstructWebcamConditions Conditions = ReadConditions();
	if (!IsStartStillValid(Conditions))
	{
		Reconcile(Conditions);
		return;
	}
	const EConstructCameraBlockedReason Reason = Settings ? ReasonFromSettings(*Settings) : ReasonFromError(Error);
	if (Reason != EConstructCameraBlockedReason::None)
	{
		ClearChoice(EConstructCameraState::Off, Reason);
		return;
	}
	Step = EStep::CheckingRecording;
}

void UConstructWebcamSubsystem::ApplyRefreshedSettings(const FCrowdyCommunicationSettings* Settings)
{
	if (!Settings)
	{
		return;
	}
	const EConstructCameraBlockedReason Reason = ReasonFromSettings(*Settings);
	if (bWanted && Reason != EConstructCameraBlockedReason::None)
	{
		ClearChoice(EConstructCameraState::Off, Reason);
		return;
	}
	const bool bSettingsBlocked = BlockedReason == EConstructCameraBlockedReason::SendingOff ||
	                              BlockedReason == EConstructCameraBlockedReason::NeedsParentConsent;
	if (!bWanted && bSettingsBlocked && Reason == EConstructCameraBlockedReason::None)
	{
		SetState(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
	}
}

void UConstructWebcamSubsystem::RequestRecordingState()
{
	if (bRecordingRequested)
	{
		return;
	}
	bRecordingRequested = true;
	UGameInstance* Instance = GetGameInstance();
	if (UCrowdySafety* Safety = Instance ? Instance->GetSubsystem<UCrowdySafety>() : nullptr)
	{
		Safety->RefreshRecordingState();
	}
}

bool UConstructWebcamSubsystem::FindCamera(FString& OutUrl) const
{
	if (CheckSource)
	{
		return true;
	}
	TArray<FMediaCaptureDeviceInfo> Devices;
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.EnumerateDevices)
	{
		Hooks.EnumerateDevices(Devices);
		OutUrl = PickWebcamDevice(Devices);
		return !OutUrl.IsEmpty();
	}
#endif
	MediaCaptureSupport::EnumerateVideoCaptureDevices(Devices);
	OutUrl = PickWebcamDevice(Devices);
	return !OutUrl.IsEmpty();
}

void UConstructWebcamSubsystem::OpenCamera()
{
	if (CheckSource)
	{
		Step = EStep::Capturing;
		SetState(EConstructCameraState::On, EConstructCameraBlockedReason::None);
		return;
	}
	if (CameraUrl.IsEmpty())
	{
		ClearChoice(EConstructCameraState::Unavailable, EConstructCameraBlockedReason::NoCamera);
		return;
	}
	UMediaPlayer* Opening = NewObject<UMediaPlayer>(this);
	Opening->PlayOnOpen = true;
	Opening->OnMediaEvent().AddUObject(this, &ThisClass::HandleMediaEvent);
	Player = Opening;
	Step = EStep::Opening;
	if (!Opening->OpenUrl(CameraUrl) && Player == Opening)
	{
		Step = EStep::Idle;
		UE_LOG(LogConstructWebcam, Warning, TEXT("ConstructWebcam: the camera could not be opened"));
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::CaptureFailed);
	}
}

void UConstructWebcamSubsystem::HandleMediaEvent(EMediaEvent Event)
{
	if (!Player || ActiveGeneration == 0)
	{
		return;
	}
	if (Event == EMediaEvent::MediaOpened && Step == EStep::Opening)
	{
		const FConstructWebcamConditions Conditions = ReadConditions();
		if (!IsStartStillValid(Conditions))
		{
			Reconcile(Conditions);
			return;
		}
		StartSampling();
		return;
	}
	const bool bLost = Event == EMediaEvent::MediaOpenFailed || Event == EMediaEvent::MediaClosed ||
	                   Event == EMediaEvent::PlaybackEndReached;
	if (bLost)
	{
		// A failed open has settled, so the player can be closed now rather than retired.
		if (Event == EMediaEvent::MediaOpenFailed)
		{
			Step = EStep::Idle;
		}
		UE_LOG(LogConstructWebcam, Warning, TEXT("ConstructWebcam: the camera stopped delivering frames"));
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::CaptureFailed);
	}
}

void UConstructWebcamSubsystem::HandleRetiringMediaEvent(EMediaEvent Event, TWeakObjectPtr<UMediaPlayer> Retiring)
{
	const bool bOpenSettled = Event == EMediaEvent::MediaOpened || Event == EMediaEvent::TracksChanged ||
	                          Event == EMediaEvent::MediaOpenFailed;
	UMediaPlayer* Settled = Retiring.Get();
	if (!bOpenSettled || !Settled)
	{
		return;
	}
	Settled->OnMediaEvent().RemoveAll(this);
	RetiringPlayers.Remove(Settled);
	Settled->Close();
}

void UConstructWebcamSubsystem::StartSampling()
{
	SelectSmallestWebcamFormat(*Player);
	Samples = MakeShared<FConstructWebcamSamples, ESPMode::ThreadSafe>();
	Player->GetPlayerFacade()->AddVideoSampleSink(Samples->Queue);
	Step = EStep::Capturing;
	UE_CLOG(IsConstructWebcamTraceOn(), LogConstructWebcam, Log, TEXT("ConstructWebcam: source: camera, device opened"));
	SetState(EConstructCameraState::On, EConstructCameraBlockedReason::None);
}

void UConstructWebcamSubsystem::PumpFrame()
{
	if (bFrameInFlight || !ImageWrapper)
	{
		return;
	}
	const int32 Token = ActiveGeneration;
	const int32 FrameQuality = Quality;
	const bool bEncode = bIgnoreNearbyGate || IsOtherPlayerNearby();
	IImageWrapperModule* Wrapper = ImageWrapper;
	TWeakObjectPtr<UConstructWebcamSubsystem> WeakThis(this);

	TArray<FColor> Pixels;
	TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
	if (CheckSource)
	{
		if (!CheckSource(Pixels) || Pixels.Num() != ConstructWebcam::FramePixels)
		{
			return;
		}
		if (!bEncode)
		{
			bFrameInFlight = true;
			ApplyFrame(Token, MoveTemp(Pixels), TArray<uint8>(), FrameQuality, true);
			return;
		}
	}
	else
	{
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Next;
		while (Samples.IsValid() && Samples->Queue->Dequeue(Next))
		{
			Sample = MoveTemp(Next);
		}
		if (!Sample.IsValid())
		{
			return;
		}
	}

	bFrameInFlight = true;
	Dispatch([WeakThis, Token, FrameQuality, bEncode, Wrapper, Pixels = MoveTemp(Pixels), Sample]() mutable
	{
		FConstructWebcamSourceFrame Source;
		const bool bConverted = !Sample.IsValid() ||
		                        (DescribeWebcamSample(*Sample, Source) && ConvertWebcamFrame(Source, Pixels));
		FConstructWebcamEncoded Encoded;
		Encoded.NextQuality = FrameQuality;
		if (bConverted && bEncode)
		{
			Encoded = EncodeWebcamFrame(*Wrapper, Pixels, FrameQuality);
		}
		// The sample goes back to its pool on the game thread, where the player hands it out.
		RunOnWebcamGameThread([WeakThis, Token, bConverted, Pixels = MoveTemp(Pixels), Encoded = MoveTemp(Encoded),
		                       Sample = MoveTemp(Sample)]() mutable
		{
			if (UConstructWebcamSubsystem* Self = WeakThis.Get())
			{
				Self->ApplyFrame(Token, MoveTemp(Pixels), MoveTemp(Encoded.Jpeg), Encoded.NextQuality, bConverted);
			}
		});
	});
}

void UConstructWebcamSubsystem::ApplyFrame(int32 Token,
                                           TArray<FColor> Pixels,
                                           TArray<uint8> Jpeg,
                                           int32 NextQuality,
                                           bool bConverted)
{
	if (Token != ActiveGeneration || Step != EStep::Capturing)
	{
		return;
	}
	bFrameInFlight = false;
	const FConstructWebcamConditions Conditions = ReadConditions();
	if (!IsStartStillValid(Conditions))
	{
		Reconcile(Conditions);
		return;
	}
	if (!bConverted)
	{
		UE_LOG(LogConstructWebcam, Warning, TEXT("ConstructWebcam: the camera delivers a picture format it cannot read"));
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::CaptureFailed);
		return;
	}
	Preview = WriteWebcamTexture(Preview, Pixels, FIntPoint(ConstructWebcam::FrameWidth, ConstructWebcam::FrameHeight));
	Quality = NextQuality;
	if (Jpeg.IsEmpty() || !(bIgnoreNearbyGate || IsOtherPlayerNearby()))
	{
		return;
	}
	const FCrowdyVideoFrameSend Result = Transmit(Jpeg);
	if (CheckSent)
	{
		CheckSent(Result);
	}
}

bool UConstructWebcamSubsystem::IsOtherPlayerNearby() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.Nearby)
	{
		return Hooks.Nearby();
	}
#endif
	const UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	const UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	FInt64Vector LocalChunk;
	if (!World || !Session || !Session->TryGetLocalPlayerChunk(World, LocalChunk))
	{
		return false;
	}
	const double ChunkSize = Session->GetChunkSize();
	const APlayerController* Controller = World->GetFirstPlayerController();
	const APawn* LocalPawn = Controller ? Controller->GetPawn() : nullptr;
	for (TActorIterator<AConstructPresencePawn> It(World); It; ++It)
	{
		const AConstructPresencePawn* Pawn = *It;
		if (Pawn == LocalPawn || Pawn->IsHidden() || Pawn->IsActorBeingDestroyed())
		{
			continue;
		}
		const FInt64Vector Chunk = WebcamChunkAt(Pawn->GetActorLocation(), ChunkSize);
		if (IsWebcamChunkWithin(LocalChunk, Chunk, ConstructWebcam::SendDistance))
		{
			return true;
		}
	}
	return false;
}

FCrowdyVideoFrameSend UConstructWebcamSubsystem::Transmit(const TArray<uint8>& Jpeg)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.Send)
	{
		return Hooks.Send(Jpeg);
	}
#endif
	UGameInstance* Instance = GetGameInstance();
	UCrowdyVideoSender* Sender = Instance ? Instance->GetSubsystem<UCrowdyVideoSender>() : nullptr;
	if (!Sender)
	{
		return FCrowdyVideoFrameSend();
	}
	return Sender->SendVideoFrame(Jpeg, ECrowdyVideoCodec::Jpeg, ConstructWebcam::SendDistance);
}

void UConstructWebcamSubsystem::Dispatch(TUniqueFunction<void()> Job)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.RunWork)
	{
		Hooks.RunWork(MoveTemp(Job));
		return;
	}
#endif
	UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Job));
}

void UConstructWebcamSubsystem::EnsureTicker()
{
	if (TickHandle.IsValid())
	{
		return;
	}
	FrameClock = 0.f;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickCamera));
}

void UConstructWebcamSubsystem::StopTicker()
{
	if (!TickHandle.IsValid())
	{
		return;
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
}

void UConstructWebcamSubsystem::HandleLogout(bool bSuccess)
{
	RefusalToken = 0;
	RefreshToken = 0;
	if (bWanted || ActiveGeneration != 0)
	{
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::NotSignedIn);
	}
}

void UConstructWebcamSubsystem::HandleAccessEnded(const FString& Reason, const FString& BlockedUntil)
{
	RefusalToken = 0;
	RefreshToken = 0;
	if (bWanted || ActiveGeneration != 0)
	{
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::NotSignedIn);
	}
}

void UConstructWebcamSubsystem::HandleAppPaused(bool bPaused, const FString& Reason)
{
	Reconcile(ReadConditions());
}

void UConstructWebcamSubsystem::HandleRecordingChanged(const FCrowdyAppRecording& Recording)
{
	UpdateRecording(Recording.bKnown, Recording.bVideo);
}

void UConstructWebcamSubsystem::HandleSendRefused(const FCrowdySendRefusal& Refusal)
{
	if (ActiveGeneration == 0 && !bWanted)
	{
		return;
	}
	UE_LOG(LogConstructWebcam, Warning, TEXT("ConstructWebcam: the server refused a video frame; the camera stopped"));
	ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::AppDoesNotPermit);
	if (bWanted)
	{
		return;
	}
	RefusalToken = ++TokenCounter;
	RequestSettings(RefusalToken, true);
}

void UConstructWebcamSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance != GetGameInstance())
	{
		return;
	}
	if (bWanted || ActiveGeneration != 0)
	{
		ClearChoice(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
	}
}

void UConstructWebcamSubsystem::HandlePostLoadMap(UWorld* World)
{
	Reconcile(ReadConditions());
}
