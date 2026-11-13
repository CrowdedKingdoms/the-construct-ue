#pragma once

#include "CoreMinimal.h"
#include "ConstructWebcamTypes.h"
#include "Containers/Ticker.h"
#include "Subsystem/CrowdySafety.h"
#include "Subsystem/CrowdyVideoSender.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ConstructWebcamSubsystem.generated.h"

class APlayerController;
class IImageWrapperModule;
class UConstructWebcamSettingsReply;
class UMediaPlayer;
class UTexture2D;
class UWorld;
enum class EMediaEvent;
struct FWorldContext;
struct FConstructWebcamSamples;
struct FConstructWebcamTestAccess;
struct FMediaCaptureDeviceInfo;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FConstructCameraStateChanged, EConstructCameraState, State,
                                             EConstructCameraBlockedReason, Reason);

/** Everything a camera start depends on, read fresh at every step. */
struct FConstructWebcamConditions
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<APlayerController> Controller;
	int64 UserId = 0;
	bool bEnabled = false;
	bool bSignedIn = false;
	bool bAppActive = false;
	bool bGameplayAllowed = false;
	bool bHasLocalPlayer = false;
	bool bRecordingKnown = false;
	bool bRecordingVideo = false;
};

/** The local player's camera: capture, the 128x96 JPEG encode and the nearby send. */
UCLASS()
class CONSTRUCTWEBCAM_API UConstructWebcamSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
	void SetCameraOn(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Construct|Webcam")
	EConstructCameraState GetCameraState() const
	{
		return CameraState;
	}

	UFUNCTION(BlueprintPure, Category = "Construct|Webcam")
	EConstructCameraBlockedReason GetBlockedReason() const
	{
		return BlockedReason;
	}

	/** The last captured frame, 128x96; null unless the camera is On. */
	UFUNCTION(BlueprintPure, Category = "Construct|Webcam")
	UTexture2D* GetLocalPreview() const;

	/** Whether this app records video. Known before the camera opens, so the notice can be shown first. */
	UFUNCTION(BlueprintPure, Category = "Construct|Webcam")
	bool IsBeingRecorded() const
	{
		return bRecordingKnown && bRecordingVideo;
	}

	/** Fires on every state or reason change, and when the recording state changes. */
	UPROPERTY(BlueprintAssignable, Category = "Construct|Webcam")
	FConstructCameraStateChanged OnCameraStateChanged;

	/** Reads the player's settings again; a camera whose Video Send is now off stops with that reason. */
	UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
	void RefreshSettings();

#if !UE_BUILD_SHIPPING
	/**
	 * Replaces the camera with Source, which fills one 128x96 frame or returns false when it has none, and reports
	 * every send to OnSent. Only the end-to-end check calls this.
	 */
	void SetFrameSourceForCheck(TFunction<bool(TArray<FColor>&)> Source,
	                            bool bInIgnoreNearbyGate,
	                            TFunction<void(const FCrowdyVideoFrameSend&)> OnSent);
	void ClearFrameSourceForCheck();
#endif

private:
	friend struct FConstructWebcamTestAccess;

	enum class EStep : uint8
	{
		Idle,
		ReadingSettings,
		CheckingRecording,
		Notice,
		Opening,
		Capturing
	};

	bool TickCamera(float DeltaTime);
	FConstructWebcamConditions ReadConditions();
	void Reconcile(const FConstructWebcamConditions& Conditions);
	void Advance(const FConstructWebcamConditions& Conditions);
	bool IsStartStillValid(const FConstructWebcamConditions& Conditions) const;
	void BeginStart(const FConstructWebcamConditions& Conditions);
	void EndStart();
	void ClearChoice(EConstructCameraState State, EConstructCameraBlockedReason Reason);
	void SetState(EConstructCameraState State, EConstructCameraBlockedReason Reason);
	void UpdateRecording(bool bKnown, bool bVideo);
	void RequestSettings(int32 Token, bool bAfterRefusal);
	void SettingsAnswered(int32 Token,
	                      bool bAfterRefusal,
	                      const FCrowdyCommunicationSettings* Settings,
	                      const FCrowdySafetyError* Error);
	void ApplyRefreshedSettings(const FCrowdyCommunicationSettings* Settings);
	void RequestRecordingState();
	bool FindCamera(FString& OutUrl) const;
	void OpenCamera();
	void HandleMediaEvent(EMediaEvent Event);
	void HandleRetiringMediaEvent(EMediaEvent Event, TWeakObjectPtr<UMediaPlayer> Retiring);
	void StartSampling();
	void PumpFrame();
	void ApplyFrame(int32 Token, TArray<FColor> Pixels, TArray<uint8> Jpeg, int32 NextQuality, bool bConverted);
	bool IsOtherPlayerNearby() const;
	FCrowdyVideoFrameSend Transmit(const TArray<uint8>& Jpeg);
	void Dispatch(TUniqueFunction<void()> Job);
	void EnsureTicker();
	void StopTicker();

	UFUNCTION()
	void HandleLogout(bool bSuccess);
	UFUNCTION()
	void HandleAccessEnded(const FString& Reason, const FString& BlockedUntil);
	UFUNCTION()
	void HandleAppPaused(bool bPaused, const FString& Reason);
	UFUNCTION()
	void HandleRecordingChanged(const FCrowdyAppRecording& Recording);
	UFUNCTION()
	void HandleSendRefused(const FCrowdySendRefusal& Refusal);
	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void HandlePostLoadMap(UWorld* World);

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> Player;

	/** Players stopped while still opening; each is closed once its open completes, since closing earlier is lost. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMediaPlayer>> RetiringPlayers;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Preview;

	UPROPERTY(Transient)
	TObjectPtr<UConstructWebcamSettingsReply> SettingsReply;

	TSharedPtr<FConstructWebcamSamples, ESPMode::ThreadSafe> Samples;
	TFunction<bool(TArray<FColor>&)> CheckSource;
	TFunction<void(const FCrowdyVideoFrameSend&)> CheckSent;
	IImageWrapperModule* ImageWrapper = nullptr;
	FString CameraUrl;
	TWeakObjectPtr<UWorld> EnabledWorld;
	TWeakObjectPtr<UWorld> StartWorld;
	TWeakObjectPtr<APlayerController> StartController;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	int64 StartUserId = 0;
	int32 TokenCounter = 0;
	int32 ActiveGeneration = 0;
	int32 RefusalToken = 0;
	int32 RefreshToken = 0;
	int32 Quality = 50;
	EConstructCameraState CameraState = EConstructCameraState::Off;
	EConstructCameraBlockedReason BlockedReason = EConstructCameraBlockedReason::None;
	EStep Step = EStep::Idle;
	bool bWanted = false;
	bool bEnabledForWorld = false;
	bool bRecordingKnown = false;
	bool bRecordingVideo = false;
	bool bRecordingRequested = false;
	bool bFrameInFlight = false;
	float FrameClock = 0.f;
	bool bIgnoreNearbyGate = false;

#if WITH_DEV_AUTOMATION_TESTS
	struct FTestHooks
	{
		TFunction<FConstructWebcamConditions()> Conditions;
		TFunction<void(int32 Token, bool bAfterRefusal)> ReadSettings;
		TFunction<void(TArray<FMediaCaptureDeviceInfo>&)> EnumerateDevices;
		TFunction<bool()> Nearby;
		TFunction<FCrowdyVideoFrameSend(const TArray<uint8>&)> Send;
		TFunction<void(TUniqueFunction<void()>)> RunWork;
	};
	FTestHooks Hooks;
#endif
};
