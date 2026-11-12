#pragma once

#include "CoreMinimal.h"
#include "ConstructWebcamTypes.h"
#include "Containers/Ticker.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StandInC9.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FStandInC9CameraStateChanged, EConstructCameraState, State,
                                             EConstructCameraBlockedReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStandInC9FeedChanged, APawn*, Pawn);
// The real call answers FCrowdyActorOwner with the same three results; Unknown means ask again later.
UENUM(BlueprintType)
enum class EStandInC9OwnerResult : uint8
{
	Confirmed,
	Unknown,
	Failed
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FStandInC9OwnerResolved, EStandInC9OwnerResult, Result, int64, UserId);

/** Temporary fixture-backed camera, feed and owner lookup. Makes no network call. */
UCLASS()
class CONSTRUCTWEBCAM_API UStandInC9 : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void SetCameraOn(bool bOn);

	UFUNCTION(BlueprintPure, Category = "Construct|StandIn")
	EConstructCameraState GetCameraState() const { return CameraState; }

	UFUNCTION(BlueprintPure, Category = "Construct|StandIn")
	EConstructCameraBlockedReason GetBlockedReason() const { return BlockedReason; }

	UFUNCTION(BlueprintPure, Category = "Construct|StandIn")
	UTexture2D* GetLocalPreview() const;

	UFUNCTION(BlueprintPure, Category = "Construct|StandIn")
	bool IsBeingRecorded() const { return bRecording; }

	UPROPERTY(BlueprintAssignable, Category = "Construct|StandIn")
	FStandInC9CameraStateChanged OnCameraStateChanged;

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void SetShowVideo(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Construct|StandIn")
	FConstructWebcamFeed GetFeedInfo(APawn* Pawn) const;

	UPROPERTY(BlueprintAssignable, Category = "Construct|StandIn")
	FStandInC9FeedChanged OnFeedChanged;

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void ResolveActorOwner(FGuid ActorId, FStandInC9OwnerResolved OnResolved);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void AddFixtureFeed(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void RemoveFixtureFeed(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void SetFixtureCameraBlock(EConstructCameraBlockedReason Reason);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void SetFixtureRecording(bool bRecorded);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void SetFixtureOwner(EStandInC9OwnerResult Result, int64 UserId);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void ExpireFixtureFeed(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void RestoreFixtureFeed(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "Construct|StandIn")
	void ChangeFixtureFeedGeneration(APawn* Pawn);

	void InitializeForTest() { LoadFixtures(); }
	void AdvanceFixtureFrameForTest() { AdvanceFixtureFrame(); }

private:
	struct FFixtureFeed
	{
		TWeakObjectPtr<APawn> Pawn;
		FString SenderId;
		int32 Generation = 0;
		bool bExpired = false;
	};

	void LoadFixtures();
	void AdvanceFixtureFrame();
	void UpdateLoop();
	void SetCameraState(EConstructCameraState State, EConstructCameraBlockedReason Reason);
	void PruneFeeds();
	FFixtureFeed* FindFeed(const APawn* Pawn);
	const FFixtureFeed* FindFeed(const APawn* Pawn) const;
	bool IsVisible(const FFixtureFeed& Feed) const { return bShowVideo && !Feed.bExpired; }

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Frames;

	TArray<FFixtureFeed> Feeds;
	FTSTicker::FDelegateHandle LoopHandle;
	EConstructCameraState CameraState = EConstructCameraState::Off;
	EConstructCameraBlockedReason BlockedReason = EConstructCameraBlockedReason::None;
	EConstructCameraBlockedReason FixtureBlock = EConstructCameraBlockedReason::None;
	int64 FixtureUserId = 1000000001;
	int32 FrameIndex = 0;
	TSet<FGuid> AskedActors;
	EStandInC9OwnerResult ForcedOwnerResult = EStandInC9OwnerResult::Confirmed;
	int32 NextGeneration = 1;
	bool bOwnerForced = false;
	bool bDeinitialized = false;
	bool bRecording = false;
	bool bShowVideo = false;
};
