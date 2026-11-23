#pragma once

#include "CoreMinimal.h"
#include "ConstructPresenceGameMode.h"
#include "ConstructPresencePawn.h"
#include "Containers/Ticker.h"
#include "Replication/Subsystems/CrowdyActorTracker.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer2Check.generated.h"

/** The layer 2 check's pawn: a counter the walker sends and the watcher logs, and no local movement of its own. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer2CheckPawn : public AConstructPresencePawn
{
	GENERATED_BODY()

public:
	void SendNextCounter();
	int32 GetCheckCounter() const { return CheckCounter; }
	bool IsRemoteCopy() const;

protected:
	// The walker sets its transform itself, so Character Movement must never write it too.
	virtual bool ShouldUseLocalCharacterMovement() const override;
	virtual void OnRep_PlayerName() override;

	UPROPERTY(meta = (CrowdyState, CrowdyOnRep = "OnRep_CheckCounter"))
	int32 CheckCounter = 0;

	UFUNCTION()
	void OnRep_CheckCounter();
};

/** The presence GameMode with the check pawn. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer2CheckGameMode : public AConstructPresenceGameMode
{
	GENERATED_BODY()

public:
	AConstructLayer2CheckGameMode();
};

/** On L_Layer2Check: the walker walks a square and counts; the watcher logs what arrives and exits when it leaves. */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer2CheckSubsystem : public UWorldSubsystem
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
		WaitPeer,
		Hold,
		Walk,
		Idle,
		WaitLeave,
		Done
	};

	bool Tick(float DeltaTime);
	void TickWalk(AConstructLayer2CheckPawn& Pawn, double Now);
	AConstructLayer2CheckPawn* FindLocalPawn() const;
	FGuid FindVisiblePeer() const;
	FVector PathPoint(double Distance) const;
	void Finish(bool bOk, const FString& Why);

	UFUNCTION()
	void HandlePeerLeft(const FCrowdyActorLeft& ActorLeft, int32 ActorCount);
	UFUNCTION()
	void HandlePeerTimedOut(FGuid UUID, int32 ActorCount);

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<UCrowdyActorTracker> Tracker;
	EStage Stage = EStage::WaitEntry;
	FGuid PeerId;
	bool bWalker = false;
	bool bPeerLeft = false;
	FVector Centre = FVector::ZeroVector;
	double StartedAt = 0.0;
	double StageStartedAt = 0.0;
	double TimeoutSeconds = 900.0;
	int32 Sent = 0;
};
