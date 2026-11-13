#pragma once

#include "CoreMinimal.h"
#include "ConstructWebcamTypes.h"
#include "Containers/Ticker.h"
#include "GameFramework/Pawn.h"
#include "Replication/Subsystems/CrowdyActorTracker.h"
#include "Replication/Subsystems/CrowdyVideoFrameReceiver.h"
#include "Serialization/CrowdyActorId.h"
#include "Subsystem/CrowdySafety.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructWebcamFeeds.generated.h"

class IImageWrapperModule;
class UConstructWebcamSettingsReply;
class UCrowdyEntitySubsystem;
struct FConstructWebcamFeedsTestAccess;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FConstructWebcamFeedChanged, APawn*, Pawn);

/** Other players' camera frames, decoded into one texture per sender over that sender's presence pawn. */
UCLASS()
class CONSTRUCTWEBCAM_API UConstructWebcamFeeds : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/** Shows other players' cameras. Off by default and local to this player. */
	UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
	void SetShowVideo(bool bShow);

	/** Empty while that player sends nothing, nothing arrived for 1 s, or Show Video or Video Receive is off. */
	UFUNCTION(BlueprintPure, Category = "Construct|Webcam")
	FConstructWebcamFeed GetFeedInfo(APawn* Pawn) const;

	/** Fires on the game thread whenever GetFeedInfo's answer for Pawn changes. */
	UPROPERTY(BlueprintAssignable, Category = "Construct|Webcam")
	FConstructWebcamFeedChanged OnFeedChanged;

	/** Frames that were WebP or not a JPEG of at most 128x96. */
	int64 GetUndecodableFrameCount() const
	{
		return UndecodableFrames;
	}

	/** Frames whose sender had no drawn presence pawn with exactly that id. */
	int64 GetDroppedFrameCount() const
	{
		return DroppedFrames;
	}

	/** Whether feeds are shown: webcams enabled, Show Video on, and Video Receive allowing it as last read. */
	bool IsReceiving() const
	{
		return bEnabled && bShowVideo && bReceiveAllowed;
	}

	/** Reads the player's Video Receive setting again; it is also read every 10 s while Show Video is on. */
	UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
	void RefreshSettings();

#if !UE_BUILD_SHIPPING
	/** Reports every decoded frame with its pixels. Only the end-to-end check calls this. */
	void SetDecodedFrameObserverForCheck(TFunction<void(APawn*, const TArray<FColor>&, FIntPoint)> Observer)
	{
		DecodedObserver = MoveTemp(Observer);
	}
#endif

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend struct FConstructWebcamFeedsTestAccess;

	struct FFeed
	{
		FCrowdyActorId PawnId;
		TObjectPtr<UTexture2D> Texture;
		TWeakObjectPtr<APawn> Pawn;
		FString SenderId;
		TArray<uint8> Pending;
		FGuid UUID;
		double LastShownAt = 0.0;
		TArray<double, TInlineAllocator<10>> RecentDecodes;
		int32 Generation = 0;
		bool bDecoding = false;
		bool bShown = false;
	};

	void BindFrames();
	void UnbindFrames();
	void RequestReceiveSetting();
	void ReceiveSettingAnswered(int32 Token, const FCrowdyCommunicationSettings* Settings);
	void ClearFeeds(bool bNotify);
	FFeed& BindFeed(const FCrowdyVideoFrame& Frame, APawn& Pawn, const FCrowdyActorId& PawnId);
	FFeed* FindFeed(const FCrowdyActorId& PawnId);
	void ReleaseFeedAt(int32 Index);
	void ReleaseFeedsFor(const FGuid& UUID);
	bool IsFeedPawnCurrent(const FFeed& Feed) const;
	void StartDecode(FFeed& Feed);
	void ApplyDecode(const FCrowdyActorId& PawnId,
	                 TWeakObjectPtr<APawn> Pawn,
	                 int32 FeedGeneration,
	                 int32 Receive,
	                 bool bDecoded,
	                 TArray<FColor> Pixels);
	void FlushNotify();
	bool TickFeeds(float DeltaTime);
	bool TickRefresh(float DeltaTime);
	void EnsureTicker();
	void StopRefreshTicker();
	AActor* FindSenderActor(const FGuid& UUID) const;
	bool FindPawnId(const AActor& Actor, FCrowdyActorId& OutId) const;
	double Now() const;
	void Dispatch(TUniqueFunction<void()> Job);

	UFUNCTION()
	void HandleFrame(const FCrowdyVideoFrame& Frame);
	UFUNCTION()
	void HandleActorLeft(const FCrowdyActorLeft& ActorLeft, int32 ActorCount);
	UFUNCTION()
	void HandleActorTimedOut(FGuid UUID, int32 ActorCount);
	UFUNCTION()
	void HandleEntityUnregistered(const FGuid& NetID);
	UFUNCTION()
	void HandleLogin(bool bSuccess, FString Message);
	UFUNCTION()
	void HandleLogout(bool bSuccess);

	UPROPERTY(Transient)
	TObjectPtr<UConstructWebcamSettingsReply> SettingsReply;

	TArray<FFeed> Feeds;
	TArray<TWeakObjectPtr<APawn>> PendingNotify;
	TFunction<void(APawn*, const TArray<FColor>&, FIntPoint)> DecodedObserver;
	IImageWrapperModule* ImageWrapper = nullptr;
	TWeakObjectPtr<UCrowdyVideoFrameReceiver> Receiver;
	TWeakObjectPtr<UCrowdyActorTracker> Tracker;
	TWeakObjectPtr<UCrowdyEntitySubsystem> Entities;
	TWeakObjectPtr<UGameInstance> ObservedInstance;
	FTSTicker::FDelegateHandle TickHandle;
	FTSTicker::FDelegateHandle RefreshHandle;
	int64 UndecodableFrames = 0;
	int64 DroppedFrames = 0;
	int32 ReceiveGeneration = 0;
	int32 FeedGenerationCounter = 0;
	bool bEnabled = false;
	bool bShowVideo = false;
	bool bReceiveAllowed = false;
	bool bFramesBound = false;

#if WITH_DEV_AUTOMATION_TESTS
	struct FTestHooks
	{
		TFunction<AActor*(const FGuid&)> FindActor;
		TFunction<bool(const AActor&, FCrowdyActorId&)> FindPawnId;
		TFunction<void(int32 Token)> ReadSettings;
		TFunction<double()> Now;
		TFunction<void(TUniqueFunction<void()>)> RunWork;
	};
	FTestHooks Hooks;
#endif
};
