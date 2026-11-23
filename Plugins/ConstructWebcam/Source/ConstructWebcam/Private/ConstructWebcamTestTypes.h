#pragma once

#include "CoreMinimal.h"
#include "ConstructWebcamTypes.h"
#include "GameFramework/Pawn.h"
#include "UObject/Object.h"
#include "ConstructWebcamTestTypes.generated.h"

UCLASS(Transient, Hidden)
class UConstructWebcamTestListener : public UObject
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
	void OnFeed(APawn* Pawn)
	{
		FeedPawns.Add(Pawn);
	}

	int32 FeedEventsFor(const APawn* Pawn) const
	{
		return FeedPawns.FilterByPredicate([Pawn](const TWeakObjectPtr<APawn>& Each) { return Each.Get() == Pawn; }).Num();
	}

	TArray<EConstructCameraState> States;
	TArray<EConstructCameraBlockedReason> Reasons;
	TArray<TWeakObjectPtr<APawn>> FeedPawns;
};
