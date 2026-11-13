#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Subsystem/CrowdyModScripts.h"
#include "Subsystems/WorldSubsystem.h"
#include "StandInC10.generated.h"

/**
 * Stands in for UCrowdyModScripts until its runtime lands, under the same function and event names. It replays a recorded
 * timeline from Content/StandIn/C10/<Name>.json against the host set with SetHost, with no network call and no decision
 * of its own. A timeline is a JSON array of steps, each one of:
 *   { "atMs": n, "call": "<host method>", "args": { "<parameter>": value } }
 *   { "atMs": n, "event": "<OnModScript event>", "args": { "<parameter>": value } }
 *   { "waitFor": "AnswerPrompt" | "Revoke" | "ForgetAuthor" | "LeaveGrid", "branches": { "<input>": [ steps ] } }
 * atMs counts from the start or the last checkpoint. A checkpoint waits for the player's call, then replays the branch
 * for it in place of the rest of its list: "Run" or "NotNow" for AnswerPrompt, the mod id or author id for Revoke and
 * ForgetAuthor, else "Any". A call with no branch is ignored.
 * EnterGrid makes a grid of GridReach chunks around the entered chunk; chunk coordinates and shape locations in a
 * timeline are offsets from that chunk. Leaving the grid withdraws open prompts and stops what runs with LeftGrid.
 */
UCLASS()
class CONSTRUCTHOST_API UStandInC10 : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Chunks the stand-in's grid reaches on every side of the entered chunk. */
	static constexpr int64 GridReach = 2;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Loads Content/StandIn/C10/<Timeline>.json for the next EnterGrid; false, with nothing selected, when it is not valid. */
	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	bool SelectTimelineForStandIn(FName Timeline);

	UFUNCTION(BlueprintPure, Category="Construct|StandIn")
	bool IsAvailable() const;

	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	void EnterGrid(int64 ChunkX, int64 ChunkY, int64 ChunkZ);

	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	void LeaveGrid();

	UFUNCTION(BlueprintPure, Category="Construct|StandIn")
	FCrowdyModScriptGrid GetGrid() const;

	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	void SetHost(TScriptInterface<ICrowdyModScriptHost> Host);

	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	void ClearHost(TScriptInterface<ICrowdyModScriptHost> Host);

	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	void AnswerPrompt(int32 PromptId, bool bRun);

	UFUNCTION(BlueprintPure, Category="Construct|StandIn")
	TArray<FCrowdyModScript> GetRunning() const;

	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	void Revoke(const FString& ModId);

	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	void ForgetAuthor(int64 AuthorId);

	UPROPERTY(BlueprintAssignable, Category="Construct|StandIn")
	FOnCrowdyModScriptPrompt OnModScriptPrompt;

	UPROPERTY(BlueprintAssignable, Category="Construct|StandIn")
	FOnCrowdyModScriptPromptWithdrawn OnModScriptPromptWithdrawn;

	UPROPERTY(BlueprintAssignable, Category="Construct|StandIn")
	FOnCrowdyModScriptStarted OnModScriptStarted;

	UPROPERTY(BlueprintAssignable, Category="Construct|StandIn")
	FOnCrowdyModScriptStopped OnModScriptStopped;

	UPROPERTY(BlueprintAssignable, Category="Construct|StandIn")
	FOnCrowdyModScriptError OnModScriptError;

	/** Loads a timeline from JSON text instead of a file, for tests. */
	bool SelectTimelineTextForStandIn(const FString& Json);

private:
	void ReplayDue();
	bool EnterCheckpoint(const FJsonObject& Step);
	void RunStep(const FJsonObject& Step);
	bool ReadScript(const FJsonObject& Args, FCrowdyModScript& Script) const;
	bool CallHost(UObject* Host, const FString& Method, const FJsonObject& Args) const;
	bool FireEvent(const FString& Event, const FJsonObject& Args);
	void TakeBranch(const FString& Input);
	void EndVisit();

	TArray<TSharedPtr<FJsonValue>> Timeline;
	TArray<TSharedPtr<FJsonValue>> Steps;
	TSharedPtr<FJsonObject> Branches;
	int32 NextStep = 0;
	double SegmentMs = 0.0;
	bool bReplaying = false;
	FString WaitingFor;

	FCrowdyModScriptGrid Grid;
	FInt64Vector EntryChunk = FInt64Vector::ZeroValue;
	int32 Generation = 0;
	TArray<int32> OpenPrompts;
	TArray<FCrowdyModScript> Running;
	TWeakObjectPtr<UObject> HostObject;
};
