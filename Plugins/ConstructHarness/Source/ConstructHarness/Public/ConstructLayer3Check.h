#pragma once

#include "CoreMinimal.h"
#include "ConstructPresenceGameMode.h"
#include "ConstructPresencePawn.h"
#include "Containers/Ticker.h"
#include "Subsystem/CrowdyProximityChat.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer3Check.generated.h"

class UVoiceChatSubsystem;

/** The layer 3 check's pawn: the presence pawn with no local movement of its own, so the check places it. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer3CheckPawn : public AConstructPresencePawn
{
	GENERATED_BODY()

protected:
	// The check sets its transform itself, so Character Movement must never write it too.
	virtual bool ShouldUseLocalCharacterMovement() const override;
};

/** The presence GameMode with the check pawn. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer3CheckGameMode : public AConstructPresenceGameMode
{
	GENERATED_BODY()

public:
	AConstructLayer3CheckGameMode();
};

/** On L_Layer3Check: the talker sends nearby text and a fed tone; the listener moves away and back and counts what arrives. */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer3CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Line N exactly as the talker sends it and the listener expects it: 240 characters. */
	static FString CheckLine(int32 N);

	/** The line number Text claims, or 0 when it is not a check line. */
	static int32 CheckLineNumber(const FString& Text);

private:
	enum class EStage : uint8
	{
		WaitEntry,
		WaitPeer,
		Settle,
		WaitGo,
		Run,
		Done,
		Failed
	};

	bool Tick(float DeltaTime);
	bool ReadControl(double Now, FString& OutControl);
	void StartRun(double Now);
	void TickEar(double Now);
	void BeginEarPhase(int32 Phase);
	void EndEarPhase();
	void TickTalker(double Elapsed, double Now);
	void TickFeed(double Elapsed, double Now);
	void TickListener(double Elapsed);
	bool MoveTo(const FVector& Spot, const TCHAR* Name);
	AConstructLayer3CheckPawn* FindLocalPawn() const;
	FGuid FindVisiblePeer() const;
	void LogDone();
	void Fail(const FString& Why);

	UFUNCTION()
	void HandleText(const FCrowdyProximityText& Message);
	UFUNCTION()
	void HandleTextRefused(const FCrowdySendRefusal& Refusal);
	UFUNCTION()
	void HandleSendRefused(const FCrowdySendRefusal& Refusal);

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<UCrowdyProximityChat> Chat;
	TWeakObjectPtr<UCrowdyCppReplicationSubsystem> Replication;
	TWeakObjectPtr<UVoiceChatSubsystem> Voice;
	FString ControlPath;
	FGuid PeerId;
	FVector NearSpot = FVector::ZeroVector;
	FVector FarSpot = FVector::ZeroVector;
	EStage Stage = EStage::WaitEntry;
	bool bTalker = false;
	// The by-ear run: phases come from the control file and a person listens; only packets are counted here.
	bool bEar = false;
	bool bFeeding = false;
	bool bWindowOpen = false;
	double StartedAt = 0.0;
	double GoAt = 0.0;
	double LastPollAt = 0.0;
	double SettleStartedAt = 0.0;
	double FeedStartedAt = 0.0;
	double TimeoutSeconds = 900.0;
	int64 FeedSentAtStart = 0;
	int64 FeedSkippedAtStart = 0;
	int64 FeedSample = 0;
	int64 WindowReceivedAtStart = 0;
	int64 EarCountAtStart = 0;
	int32 NextLine = 1;
	int32 FeedIndex = 0;
	int32 FeedFrames = 0;
	int32 WindowIndex = 0;
	int32 MoveIndex = 0;
	int32 EarPhase = -1;
};
