#pragma once

#include "CoreMinimal.h"
#include "ConstructWebcamTypes.h"
#include "StandInC9.h"
#include "GameFramework/Pawn.h"
#include "UObject/Object.h"
#include "StandInC9TestTypes.generated.h"

UCLASS(Transient, Hidden)
class UStandInC9TestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnCameraState(EConstructCameraState State, EConstructCameraBlockedReason Reason)
	{
		States.Add(State);
		Reasons.Add(Reason);
	}

	UFUNCTION()
	void OnFeed(APawn* Pawn) { FeedPawns.Add(Pawn); }

	UFUNCTION()
	void OnOwner(EStandInC9OwnerResult InResult, int64 InUserId)
	{
		++OwnerAnswers;
		Result = InResult;
		UserId = InUserId;
	}

	TArray<EConstructCameraState> States;
	TArray<EConstructCameraBlockedReason> Reasons;
	TArray<TWeakObjectPtr<APawn>> FeedPawns;
	int32 OwnerAnswers = 0;
	int64 UserId = -1;
	EStandInC9OwnerResult Result = EStandInC9OwnerResult::Failed;
};
