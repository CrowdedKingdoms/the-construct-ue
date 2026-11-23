#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "InputCoreTypes.h"
#include "Engine/TimerHandle.h"
#include "Core/Audio/VoiceChat/CrowdyVoiceMicState.h"
#include "ConstructVoiceComponent.generated.h"

class APlayerController;
class UGameInstance;
struct FCrowdySendRefusal;
struct FConstructVoiceRuntimeTestAccess;
class UConstructUIInputSubsystem;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;

/** Intent latch shared by Enhanced Input callbacks and explicit press/release callers. */
struct CONSTRUCTVOICE_API FConstructVoiceTalkKeyState
{
	void Press(bool bRepeat)
	{
		// A repeat first observed after registration/focus change is not a new opt-in press.
		if (bRepeat && !bDown)
		{
			bReleaseRequired = true;
		}
		bDown = true;
	}
	void Release()
	{
		bDown = false;
		bReleaseRequired = false;
	}
	void BlockIfHeld()
	{
		bReleaseRequired = bReleaseRequired || bDown;
	}
	bool IsDown() const
	{
		return bDown;
	}
	bool IsReleaseRequired() const
	{
		return bReleaseRequired;
	}
	bool WantsCapture() const
	{
		return bDown && !bReleaseRequired;
	}

private:
	bool bDown = false;
	bool bReleaseRequired = false;
};

/** Owns local UI/input intent only. The SDK owns capture, codec, transport and playback. */
UCLASS(ClassGroup = (Construct), BlueprintType, meta = (BlueprintSpawnableComponent))
class CONSTRUCTVOICE_API UConstructVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UConstructVoiceComponent();
	/** Complete activation after a local player is assigned; repeated calls are safe. */
	void InitializeLocalRuntime();
	/** Release only this component's UI, input and voice requests. */
	void ShutdownLocalRuntime();
	virtual void Activate(bool bReset = false) override;
	virtual void Deactivate() override;
	virtual void SetComponentTickEnabled(bool bEnabled) override;
	UFUNCTION(BlueprintPure, Category = "Construct|Voice")
	bool IsVoiceAvailable() const
	{
		return bRuntimeActive;
	}

	virtual void TickComponent(float DeltaTime,
	                           ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
	UFUNCTION(BlueprintCallable, Category = "Construct|Voice")
	void SetPushToTalkEnabled(bool bEnabled);
	/** Explicit press/release intent for another input source. Keyboard acquisition uses Enhanced Input. */
	UFUNCTION(BlueprintCallable, Category = "Construct|Voice")
	void SetPushToTalkPressed(bool bPressed);
	UFUNCTION(BlueprintCallable, Category = "Construct|Voice")
	void StopTalking();
	UFUNCTION(BlueprintCallable, Category = "Construct|Voice")
	void SetListeningEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "Construct|Voice")
	bool IsPushToTalkEnabled() const
	{
		return bPushToTalkEnabled;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Voice")
	bool IsListeningEnabled() const
	{
		return bListeningEnabled;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Voice")
	bool IsCaptureActive() const;
	UFUNCTION(BlueprintPure, Category = "Construct|Voice")
	FText GetStatusText() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Construct|Voice")
	FKey PushToTalkKey = EKeys::V;
	static bool CanRequestCapture(
	    bool bEnabled, bool bLocalController, bool bSignedIn, bool bGameplayAllowed, bool bApplicationActive);

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FConstructVoiceAccessLossTest;
	friend struct FConstructVoiceRuntimeTestAccess;
	void EnsurePanel();
	void RefreshSessionIdentity();
	void ResetPreferences();
	UFUNCTION()
	void SignedIn(bool bSuccess, FString Message);
	UFUNCTION()
	void MicStateChanged(ECrowdyVoiceMicState State);
	UFUNCTION()
	void SendRefused(const FCrowdySendRefusal& Refusal);
	UPROPERTY(Transient)
	TObjectPtr<class UConstructVoicePanel> Panel;
	bool bRuntimeActive = false;
	bool bShuttingDown = false;
	int64 SessionUserId = 0;
	int64 SessionAppId = 0;
	ECrowdyVoiceMicState MicState = ECrowdyVoiceMicState::Off;

	APlayerController* LocalController() const;
	bool IsSignedIn() const;
	bool IsGameplayInputAllowed() const;
	bool IsOwnViewportFocused() const;
	void BindInputGate();
	void RefreshEnhancedInput();
	void ReleaseEnhancedInput();
	void EnhancedTalkStarted();
	void EnhancedTalkReleased();
	void ReconcileCapture(bool bUserEdge = false);
	void StopCapture();
	void ApplicationActivationChanged(bool bActive);
	UFUNCTION()
	void GameplayInputChanged(bool bAllowed);
	UFUNCTION()
	void LoggedOut(bool bSuccess);
	UFUNCTION()
	void AccessEnded(const FString& Reason, const FString& BlockedUntil);
	TWeakObjectPtr<UConstructUIInputSubsystem> InputGate;
	TWeakObjectPtr<UGameInstance> RuntimeInstance;
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> TalkInput;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TalkAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> TalkContext;
	TWeakObjectPtr<APlayerController> InputController;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> EnhancedSubsystem;
	bool bContextInstalled = false;
	bool bEnhancedInputAllowed = false;
	FDelegateHandle ActivationHandle;
	FConstructVoiceTalkKeyState TalkKeyState;
	bool bPushToTalkEnabled = false;
	bool bListeningEnabled = false;
	bool bCaptureRequested = false;
	// A default-off component must not initialize the SDK audio service merely to mute it.
	bool bVoiceServiceActivated = false;
	bool bApplicationActive = true;
	bool bKeyboardIntent = false;
	FText LastStatus;
};

UCLASS(BlueprintType, Blueprintable)
class CONSTRUCTVOICE_API UConstructVoicePanel : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

private:
	UConstructVoiceComponent* FindVoiceComponent() const;
	void RequestGameplayFocusRestore();
	FTimerHandle GameplayFocusTimer;
};
