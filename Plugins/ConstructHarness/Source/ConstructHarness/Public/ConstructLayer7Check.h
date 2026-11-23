#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "CrowdyServerObjectTypes.h"
#include "Subsystem/CrowdyClaims.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer7Check.generated.h"

class AConstructClaimPad;
class UCrowdyServerObject;
class UCrowdyServerObjectDefinition;

/** What a claim instruction does with what the pad shows. */
enum class EConstructLayer7ClaimPath : uint8
{
	/** A free chunk: the pad's own Claim, as a player accepting its offer. */
	Pad,
	/** Another player's claim: the pad offers nothing, so the SDK's Claim Chunk, then the hub's RecordClaim. */
	Direct,
	/** Unknown, or this player's own claim: nothing to claim. */
	None
};

/**
 * On L_Layer7Check: once signed in, connected, with the other player's pawn in view and the placed claim pad's chunk
 * shown, follows Tools/Layer7Check's control file. "claim" claims the pad's chunk, "release" gives it back and "look"
 * refreshes it, each through the pad's own Claim, Release and Refresh. Owner ids and names go to the results file,
 * never the log.
 */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer7CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	static EConstructLayer7ClaimPath ChooseClaimPath(bool bKnown, int64 GridId, bool bOwnedByMe);

	/** The control file's instructions from index FirstNew on. A last line with no newline yet is left for a later read. */
	static TArray<FString> CompleteLines(const FString& Text, int32 FirstNew);

	/** Results-file lines. Ids are strings, so a JSON reader keeps them exact. */
	static FString ShownJson(int64 GridId, int64 OwnerUserId, bool bOwnedByMe);
	static FString NamedJson(int64 OwnerUserId, const FString& Name);
	static FString RecordJson(int64 GridId, ECrowdyServerCallOutcome Outcome, const FString& Reason);

private:
	enum class EStage : uint8
	{
		WaitEntry,
		Running,
		Failed
	};

	// The instruction in hand; Idle reads the next one.
	enum class EStep : uint8
	{
		Idle,
		Claiming,
		Releasing,
		AwaitShown,
		Looking,
		AwaitNamed,
		SdkClaiming,
		Recording
	};

	bool Tick(float DeltaTime);
	bool TickEntry();
	bool HasVisiblePeer() const;
	void PollControl();
	void Act(const FString& Line);
	void StartClaim();
	void HandleSdkClaim(const FCrowdyChunkClaim& Reply);
	void CallRecord();
	void HandleRecord(const FCrowdyServerCallResult& Result);
	void ReleaseHub();
	void Begin(EStep Next, double Seconds);
	void Finish();
	void WriteResult(const FString& Line);
	void Fail(const FString& Why);

	UFUNCTION()
	void HandleOwnerShown(int64 GridId, int64 OwnerUserId, bool bOwnedByMe);

	UFUNCTION()
	void HandleOwnerNamed(int64 OwnerUserId, const FString& Name);

	UFUNCTION()
	void HandleClaimDone(ECrowdyClaimResult Result, int64 GridId, bool bListed, const FString& Message);

	UFUNCTION()
	void HandleReleaseDone(ECrowdyClaimResult Result, int64 GridId, bool bUnlisted, const FString& Message);

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<AConstructClaimPad> Pad;
	UPROPERTY(Transient)
	TObjectPtr<UCrowdyServerObjectDefinition> ClaimsDefinition;
	UPROPERTY(Transient)
	TObjectPtr<UCrowdyServerObject> Hub;
	FString ControlPath;
	FString ResultsPath;
	FInt64Vector Chunk = FInt64Vector::ZeroValue;
	EStage Stage = EStage::WaitEntry;
	EStep Step = EStep::Idle;
	int32 LinesDone = 0;
	int64 TargetGrid = 0;
	double StartedAt = 0.0;
	double LastPollAt = 0.0;
	double StepDeadline = 0.0;
	uint64 StepSerial = 0;
	bool bRecordCalled = false;
};
