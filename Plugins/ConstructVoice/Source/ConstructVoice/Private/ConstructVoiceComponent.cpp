#include "ConstructVoiceComponent.h"

#include "ConstructUIInputSubsystem.h"
#include "ConstructVoiceSubsystem.h"
#include "Network/CrowdyCpp/CrowdyCppReplicationSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "ConstructVoice"

UConstructVoiceComponent::UConstructVoiceComponent()
{
	bAutoActivate = false;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.02f;
	LastStatus = LOCTEXT("OptIn", "Microphone and listening are off. Enable only the voice controls you want to use.");
}

void UConstructVoiceComponent::OnRegister()
{
	if (!bRuntimeActive)
	{
		bAutoActivate = false;
		PrimaryComponentTick.bStartWithTickEnabled = false;
		Super::SetComponentTickEnabled(false);
	}
	Super::OnRegister();
}

void UConstructVoiceComponent::Activate(bool bReset)
{
	if (!bRuntimeActive)
	{
		InitializeLocalRuntime();
		if (!bRuntimeActive)
		{
			Super::SetComponentTickEnabled(false);
		}
		return;
	}
	Super::Activate(bReset);
}

void UConstructVoiceComponent::Deactivate()
{
	ShutdownLocalRuntime();
	Super::Deactivate();
}

void UConstructVoiceComponent::SetComponentTickEnabled(bool bEnabled)
{
	Super::SetComponentTickEnabled(bEnabled && bRuntimeActive);
}

APlayerController* UConstructVoiceComponent::LocalController() const
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	}
	return PC && PC->IsLocalController() && PC->GetLocalPlayer() ? PC : nullptr;
}

bool UConstructVoiceComponent::IsSignedIn() const
{
	const UGameInstance* Instance = RuntimeInstance.Get();
	const UCrowdyAuthentication* Auth = Instance ? Instance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	return Auth && Auth->IsSignedIn() && !Auth->IsLegalAcceptancePending() && Session && !Session->HasAccessEnded() &&
	       !Session->IsAppPaused();
}

void UConstructVoiceComponent::BindInputGate()
{
	APlayerController* PC = LocalController();
	ULocalPlayer* Local = PC ? PC->GetLocalPlayer() : nullptr;
	UConstructUIInputSubsystem* Gate = Local ? Local->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
	if (InputGate.Get() == Gate)
	{
		return;
	}
	if (InputGate.IsValid())
	{
		InputGate->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::GameplayInputChanged);
	}
	InputGate = Gate;
	if (Gate)
	{
		Gate->OnGameplayInputChanged.AddUniqueDynamic(this, &ThisClass::GameplayInputChanged);
	}
}

bool UConstructVoiceComponent::IsGameplayInputAllowed() const
{
	return InputGate.IsValid() && InputGate->IsGameplayInputAllowed();
}

bool UConstructVoiceComponent::IsOwnViewportFocused() const
{
	const UWorld* World = GetWorld();
	const UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
	return LocalController() && Client && Client->Viewport && Client->Viewport->HasFocus();
}

void UConstructVoiceComponent::RefreshEnhancedInput()
{
	if (!bRuntimeActive)
	{
		return;
	}
	APlayerController* PC = LocalController();
	ULocalPlayer* Local = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem =
	    Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (InputController.Get() != PC || EnhancedSubsystem.Get() != Subsystem)
	{
		ReleaseEnhancedInput();
	}
	if (!PC || !Subsystem || !Cast<UEnhancedPlayerInput>(PC->PlayerInput))
	{
		return;
	}
	if (!TalkInput)
	{
		InputController = PC;
		EnhancedSubsystem = Subsystem;
		TalkAction = NewObject<UInputAction>(this, TEXT("ConstructTalkAction"));
		TalkAction->ValueType = EInputActionValueType::Boolean;
		TalkAction->bConsumeInput = true;
		TalkContext = NewObject<UInputMappingContext>(this, TEXT("ConstructTalkContext"));
		TalkContext->MapKey(TalkAction, PushToTalkKey);
		TalkInput = NewObject<UEnhancedInputComponent>(PC);
		TalkInput->bBlockInput = false;
		TalkInput->Priority = 50;
		TalkInput->RegisterComponent();
		TalkInput->BindAction(TalkAction, ETriggerEvent::Started, this, &ThisClass::EnhancedTalkStarted);
		TalkInput->BindAction(TalkAction, ETriggerEvent::Completed, this, &ThisClass::EnhancedTalkReleased);
		TalkInput->BindAction(TalkAction, ETriggerEvent::Canceled, this, &ThisClass::EnhancedTalkReleased);
		PC->PushInputComponent(TalkInput);
	}
	const bool bAllowed = CanRequestCapture(
	    bPushToTalkEnabled, true, IsSignedIn(), IsGameplayInputAllowed(), bApplicationActive && IsOwnViewportFocused());
	const bool bReactivated = bAllowed && !bEnhancedInputAllowed;
	bEnhancedInputAllowed = bAllowed;
	if (!bAllowed)
	{
		TalkKeyState.BlockIfHeld();
		StopCapture();
	}
	if (!bPushToTalkEnabled)
	{
		if (bContextInstalled)
		{
			bContextInstalled = false;
			Subsystem->RemoveMappingContext(TalkContext);
			TalkKeyState.Release();
		}
		return;
	}
	if (!bContextInstalled || bReactivated)
	{
		// Keep the opted-in mapping during a modal so Engine release/flush suppression
		// survives. Gameplay capture is disabled above, independently of key tracking.
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = true;
		Options.bForceImmediately = true;
		Subsystem->AddMappingContext(TalkContext, 50, Options);
		bContextInstalled = true;
	}
}

void UConstructVoiceComponent::ReleaseEnhancedInput()
{
	bEnhancedInputAllowed = false;
	if (EnhancedSubsystem.IsValid() && bContextInstalled && TalkContext)
	{
		EnhancedSubsystem->RemoveMappingContext(TalkContext);
	}
	bContextInstalled = false;
	if (APlayerController* PC = InputController.Get(); PC && TalkInput)
	{
		PC->PopInputComponent(TalkInput);
	}
	if (TalkInput)
	{
		TalkInput->DestroyComponent();
	}
	TalkInput = nullptr;
	TalkContext = nullptr;
	TalkAction = nullptr;
	InputController.Reset();
	EnhancedSubsystem.Reset();
	TalkKeyState.Release();
	StopCapture();
}

void UConstructVoiceComponent::EnhancedTalkStarted()
{
	bKeyboardIntent = true;
	if (!bEnhancedInputAllowed)
	{
		StopTalking();
		return;
	}
	TalkKeyState.Press(false);
	ReconcileCapture(true);
}

void UConstructVoiceComponent::EnhancedTalkReleased()
{
	TalkKeyState.Release();
	ReconcileCapture();
}

bool UConstructVoiceComponent::CanRequestCapture(
    bool bEnabled, bool bLocalController, bool bSignedIn, bool bGameplayAllowed, bool bApplicationActive)
{
	return bEnabled && bLocalController && bSignedIn && bGameplayAllowed && bApplicationActive;
}

void UConstructVoiceComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeLocalRuntime();
}

void UConstructVoiceComponent::InitializeLocalRuntime()
{
	if (bRuntimeActive || bShuttingDown || !HasBegunPlay())
	{
		return;
	}
	const UConstructVoiceSubsystem* Installer =
	    GetWorld() ? GetWorld()->GetSubsystem<UConstructVoiceSubsystem>() : nullptr;
	APlayerController* PC = LocalController();
	if (!Installer || !Installer->IsVoiceEnabled() || !PC ||
	    PC->FindComponentByClass<UConstructVoiceComponent>() != this)
	{
		return;
	}
	RuntimeInstance = GetWorld()->GetGameInstance();
	bRuntimeActive = true;
	Activate(true);
	if (!bRuntimeActive)
	{
		return;
	}
	BindInputGate();
	RefreshSessionIdentity();
	RefreshEnhancedInput();
	UGameInstance* Instance = RuntimeInstance.Get();
	if (UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr)
	{
		SDK->OnLogin.AddUniqueDynamic(this, &ThisClass::SignedIn);
		SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::LoggedOut);
		SDK->OnVoiceMicStateChanged.AddUniqueDynamic(this, &ThisClass::MicStateChanged);
		MicState = SDK->GetVoiceMicState();
	}
	if (UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr)
	{
		Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
	}
	if (UCrowdyCppReplicationSubsystem* Realtime =
	        Instance ? Instance->GetSubsystem<UCrowdyCppReplicationSubsystem>() : nullptr)
	{
		Realtime->OnSendRefused.AddUniqueDynamic(this, &ThisClass::SendRefused);
	}
	if (FSlateApplication::IsInitialized())
	{
		bApplicationActive = FSlateApplication::Get().IsActive();
		ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
		    this, &ThisClass::ApplicationActivationChanged);
	}
	EnsurePanel();
}

void UConstructVoiceComponent::EnsurePanel()
{
	if (!bRuntimeActive || (Panel && Panel->IsInViewport()))
	{
		return;
	}
	APlayerController* PC = LocalController();
	if (!PC || !GetWorld() || !GetWorld()->GetGameViewport())
	{
		return;
	}
	if (!Panel)
	{
		Panel = CreateWidget<UConstructVoicePanel>(PC, UConstructVoicePanel::StaticClass());
		if (Panel)
		{
			Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	if (Panel && !Panel->IsInViewport())
	{
		Panel->AddToPlayerScreen(2);
	}
}

void UConstructVoiceComponent::RefreshSessionIdentity()
{
	if (!bRuntimeActive)
	{
		return;
	}
	const UGameInstance* Instance = RuntimeInstance.Get();
	const UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	const int64 User = IsSignedIn() && Session ? Session->GetUserID() : 0;
	const int64 App = IsSignedIn() && Session ? Session->GetAppID() : 0;
	if (User != SessionUserId || App != SessionAppId)
	{
		ResetPreferences();
		SessionUserId = User;
		SessionAppId = App;
	}
}

void UConstructVoiceComponent::ResetPreferences()
{
	bPushToTalkEnabled = false;
	TalkKeyState.BlockIfHeld();
	StopCapture();
	if (bRuntimeActive)
	{
		SetListeningEnabled(false);
		RefreshEnhancedInput();
	}
	else
	{
		bListeningEnabled = false;
	}
}

void UConstructVoiceComponent::SignedIn(bool bSuccess, FString Message)
{
	if (bSuccess)
	{
		RefreshSessionIdentity();
	}
}

void UConstructVoiceComponent::MicStateChanged(ECrowdyVoiceMicState State)
{
	if (bRuntimeActive)
	{
		MicState = State;
	}
}

void UConstructVoiceComponent::SendRefused(const FCrowdySendRefusal& Refusal)
{
	if (bRuntimeActive && Refusal.MessageType == 134)
	{
		LastStatus = LOCTEXT("Refused", "Voice is not enabled for this app");
	}
}

void UConstructVoiceComponent::SetPushToTalkEnabled(bool bEnabled)
{
	if (!bRuntimeActive)
	{
		return;
	}

	bPushToTalkEnabled = bEnabled;
	TalkKeyState.BlockIfHeld();
	StopCapture();
	RefreshEnhancedInput();
	LastStatus = bEnabled ? LOCTEXT("Enabled", "Push-to-talk enabled. Hold the talk key during gameplay to speak.")
	                      : LOCTEXT("Disabled", "Push-to-talk is off. The microphone is stopped.");
}

void UConstructVoiceComponent::SetPushToTalkPressed(bool bPressed)
{
	if (!bRuntimeActive)
	{
		return;
	}

	bKeyboardIntent = false;
	if (bPressed)
	{
		TalkKeyState.Press(false);
	}
	else
	{
		TalkKeyState.Release();
	}
	ReconcileCapture(true);
}

void UConstructVoiceComponent::ReconcileCapture(bool bUserEdge)
{
	if (!bRuntimeActive)
	{
		return;
	}

	const bool bFocusAllowed = bApplicationActive && (!bKeyboardIntent || IsOwnViewportFocused());
	if (!CanRequestCapture(
	        bPushToTalkEnabled, LocalController() != nullptr, IsSignedIn(), IsGameplayInputAllowed(), bFocusAllowed))
	{
		TalkKeyState.BlockIfHeld();
		StopCapture();
		if (bUserEdge && TalkKeyState.IsDown())
		{
			LastStatus = LOCTEXT(
			    "Blocked",
			    "Microphone is blocked until push-to-talk is enabled, access is active and gameplay input is available.");
		}
		return;
	}
	if (!TalkKeyState.WantsCapture())
	{
		StopCapture();
		if (bUserEdge && TalkKeyState.IsReleaseRequired())
		{
			LastStatus = LOCTEXT("ReleaseFirst", "Release the talk key before starting again.");
		}
		return;
	}
	if (bCaptureRequested)
	{
		return;
	}
	UGameInstance* Instance = RuntimeInstance.Get();
	UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	if (!SDK)
	{
		LastStatus = LOCTEXT("NoSDK", "Crowdy voice is unavailable in this world.");
		return;
	}
	bCaptureRequested = true;
	bVoiceServiceActivated = true;
	SDK->StartVoiceChat();
	LastStatus = LOCTEXT("Starting",
	                     "Microphone capture requested. Capture status below reflects the SDK's actual device state.");
}

void UConstructVoiceComponent::StopCapture()
{
	if (!bCaptureRequested)
	{
		return;
	}
	bCaptureRequested = false;
	UGameInstance* Instance = RuntimeInstance.Get();
	if (UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	    SDK && bVoiceServiceActivated)
	{
		SDK->StopVoiceChat();
	}
	LastStatus = LOCTEXT("Stopped", "Microphone stopped.");
}

void UConstructVoiceComponent::StopTalking()
{
	TalkKeyState.BlockIfHeld();
	StopCapture();
	LastStatus = TalkKeyState.IsReleaseRequired()
	                 ? LOCTEXT("StoppedRelease", "Microphone stopped. Release the talk key before starting again.")
	                 : LOCTEXT("Stopped", "Microphone stopped.");
}

void UConstructVoiceComponent::SetListeningEnabled(bool bEnabled)
{
	if (!bRuntimeActive)
	{
		bListeningEnabled = false;
		return;
	}

	UGameInstance* Instance = RuntimeInstance.Get();
	UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const bool bWasListening = bListeningEnabled;
	const bool bCanEnable = bEnabled && LocalController() && IsSignedIn() && SDK;
	bListeningEnabled = bCanEnable;
	// Release playback only when this component requested it. This also avoids
	// initializing the SDK audio service before the player opts in.
	if (SDK)
	{
		if (bCanEnable)
		{
			bVoiceServiceActivated = true;
			SDK->PlayVoiceChat();
		}
		else if (bWasListening)
		{
			SDK->MuteVoiceChat();
		}
	}
	if (!bEnabled)
	{
		LastStatus = LOCTEXT("Muted", "Incoming voice playback muted.");
	}
	else if (bCanEnable)
	{
		LastStatus = LOCTEXT("Listening", "Incoming voice playback enabled.");
	}
	else if (!SDK)
	{
		LastStatus = LOCTEXT("NoSDK", "Crowdy voice is unavailable in this world.");
	}
	else
	{
		LastStatus = LOCTEXT("ListenNeedsSession", "Sign in with active app access before enabling voice playback.");
	}
}

bool UConstructVoiceComponent::IsCaptureActive() const
{
	if (!bRuntimeActive || !bCaptureRequested)
	{
		return false;
	}
	const UGameInstance* Instance = RuntimeInstance.Get();
	const UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	return SDK && SDK->GetVoiceMicState() == ECrowdyVoiceMicState::Capturing;
}

FText UConstructVoiceComponent::GetStatusText() const
{
	if (!bRuntimeActive)
	{
		return LOCTEXT("ProjectDisabled", "Voice is disabled for this project or controller.");
	}
	FText Device;
	switch (MicState)
	{
	case ECrowdyVoiceMicState::Starting:
		Device = LOCTEXT("MicStarting", "Starting");
		break;
	case ECrowdyVoiceMicState::Capturing:
		Device = LOCTEXT("MicLive", "Talking");
		break;
	case ECrowdyVoiceMicState::NoDevice:
		Device = LOCTEXT("MicMissing", "No microphone");
		break;
	case ECrowdyVoiceMicState::Error:
		Device = LOCTEXT("MicError", "Microphone error");
		break;
	default:
		Device = LOCTEXT("MicOff", "Off");
		break;
	}
	return FText::Format(LOCTEXT("Status", "{0}\nMicrophone: {1}"), LastStatus, Device);
}

void UConstructVoiceComponent::TickComponent(float DeltaTime,
                                             ELevelTick TickType,
                                             FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bRuntimeActive)
	{
		return;
	}
	EnsurePanel();
	RefreshSessionIdentity();
	BindInputGate();
	RefreshEnhancedInput();
	ReconcileCapture();
}

void UConstructVoiceComponent::ApplicationActivationChanged(bool bActive)
{
	bApplicationActive = bActive;
	RefreshEnhancedInput();
	if (!bActive)
	{
		StopTalking();
		LastStatus = LOCTEXT("FocusLost", "Microphone stopped because this application lost focus.");
	}
}

void UConstructVoiceComponent::GameplayInputChanged(bool bAllowed)
{
	RefreshEnhancedInput();
	if (!bAllowed)
	{
		StopTalking();
		LastStatus = LOCTEXT("Modal", "Microphone is off while gameplay input is blocked.");
	}
	else if (bRuntimeActive)
	{
		if (TalkKeyState.IsReleaseRequired())
		{
			LastStatus = LOCTEXT("ReleaseFirst", "Release the talk key before starting again.");
		}
		else if (bPushToTalkEnabled)
		{
			LastStatus = LOCTEXT("Enabled", "Push-to-talk enabled. Hold the talk key during gameplay to speak.");
		}
		else
		{
			LastStatus =
			    bListeningEnabled
			        ? LOCTEXT("Listening", "Incoming voice playback enabled.")
			        : LOCTEXT("OptIn",
			                  "Microphone and listening are off. Enable only the voice controls you want to use.");
		}
	}
}

void UConstructVoiceComponent::LoggedOut(bool bSuccess)
{
	ResetPreferences();
	SessionUserId = 0;
	SessionAppId = 0;
	LastStatus = LOCTEXT("SignedOut", "Microphone and incoming playback stopped on sign-out.");
}

void UConstructVoiceComponent::AccessEnded(const FString& Reason, const FString& BlockedUntil)
{
	ResetPreferences();
	SessionUserId = 0;
	SessionAppId = 0;
	LastStatus = LOCTEXT("AccessEnded", "Microphone and incoming playback stopped because app access ended.");
}

void UConstructVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownLocalRuntime();
	Super::EndPlay(EndPlayReason);
}

void UConstructVoiceComponent::ShutdownLocalRuntime()
{
	if (!bRuntimeActive)
	{
		return;
	}
	bShuttingDown = true;
	const bool bOwnedPlayback = bListeningEnabled && bVoiceServiceActivated;
	bRuntimeActive = false;
	bPushToTalkEnabled = false;
	bListeningEnabled = false;
	SetComponentTickEnabled(false);
	if (Panel)
	{
		Panel->RemoveFromParent();
		Panel = nullptr;
	}

	ReleaseEnhancedInput();
	TalkKeyState.Release();
	if (InputGate.IsValid())
	{
		InputGate->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::GameplayInputChanged);
	}
	InputGate.Reset();
	if (FSlateApplication::IsInitialized())
	{
		if (ActivationHandle.IsValid())
		{
			FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
		}
	}
	ActivationHandle.Reset();
	UGameInstance* Instance = RuntimeInstance.Get();
	if (Instance)
	{
		if (UCrowdyCppReplicationSubsystem* Realtime = Instance->GetSubsystem<UCrowdyCppReplicationSubsystem>())
		{
			Realtime->OnSendRefused.RemoveDynamic(this, &ThisClass::SendRefused);
		}
		if (UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>())
		{
			Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
		}
		if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
		{
			SDK->OnLogin.RemoveDynamic(this, &ThisClass::SignedIn);
			SDK->OnLogout.RemoveDynamic(this, &ThisClass::LoggedOut);
			SDK->OnVoiceMicStateChanged.RemoveDynamic(this, &ThisClass::MicStateChanged);
			if (bOwnedPlayback)
			{
				SDK->MuteVoiceChat();
			}
		}
	}
	bVoiceServiceActivated = false;
	RuntimeInstance.Reset();
	Super::Deactivate();
	bShuttingDown = false;
}

#undef LOCTEXT_NAMESPACE
