#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystem/CrowdyChannels.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer1Check.generated.h"

/** On L_Layer1Check: sign-in, a token refresh, a late Game API call and the expected ids, then quits 0 (pass) or 1. */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer1CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	enum class EStage : uint8
	{
		WaitEntry,
		WaitRefresh,
		WaitLateCall,
		CallingLate,
		Done
	};

	bool Tick(float DeltaTime);
	void MakeLateCall();
	void Finish(bool bPass, const FString& Why);

	UFUNCTION()
	void HandleRefreshed();
	UFUNCTION()
	void HandleLateCallSucceeded(const TArray<FCrowdyChannelMembership>& Memberships);
	UFUNCTION()
	void HandleLateCallFailed(FCrowdyChannelError Error, FString Message);

	FTSTicker::FDelegateHandle TickerHandle;
	EStage Stage = EStage::WaitEntry;
	int64 ExpectedAppId = 0;
	int64 ExpectedUserId = 0;
	double StartedAt = 0.0;
	double FirstRefreshAt = 0.0;
	double LateCallDelaySeconds = 180.0;
	double TimeoutSeconds = 2700.0;
};
