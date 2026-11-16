#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Subsystems/WorldSubsystem.h"
#include "StandInC11.generated.h"

class UConstructStudioDock;

/**
 * Stands in for the Studio dock's agent page and platform calls until they land. The next UConstructStudioDock that
 * opens in this world replays a timeline from Content/StandIn/C11/<Name>.json, with no network call and no decision of
 * its own: the dock applies its own rules to every step. A timeline is a JSON array of steps, each one of:
 *   { "atMs": n, "event": "<event>", "args": { "<field>": value } }
 *   { "waitFor": "GiveProviderConsent" | "AnswerDeployPrompt" | "Close" | "SetGrid", "branches": { "<input>": [ steps ] } }
 * atMs counts from the dock's Open or the last checkpoint. A checkpoint waits for the player's call, then replays the
 * branch for it in place of the rest of its list: "Run" or "NotNow" for AnswerDeployPrompt (a prompt that declines
 * itself counts as NotNow), else "Any". A call with no branch is ignored. studio.projectOpen's ProjectId is a JSON
 * string, as the platform's project ids are ("504"); a numeric one is refused.
 */
UCLASS()
class CONSTRUCTSTUDIODOCKCORE_API UStandInC11 : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void BeginDestroy() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Loads Content/StandIn/C11/<Timeline>.json for the next Open; false, with nothing selected, when it is not valid. */
	UFUNCTION(BlueprintCallable, Category="Construct|StandIn")
	bool SelectTimelineForStandIn(FName Timeline);

	/** Loads a timeline from JSON text instead of a file, for tests. */
	bool SelectTimelineTextForStandIn(const FString& Json);

private:
	void Heard(UConstructStudioDock& Dock, FName Message, const FString& Detail);
	void Listen();
	void StopListening();
	void ReplayDue();
	bool EnterCheckpoint(const FJsonObject& Step);
	void RunStep(const FJsonObject& Step);
	bool Replay(UConstructStudioDock& Dock, const FString& Event, const FJsonObject& Args) const;
	void TakeBranch(const FString& Input);

	TArray<TSharedPtr<FJsonValue>> Timeline;
	TArray<TSharedPtr<FJsonValue>> Steps;
	TSharedPtr<FJsonObject> Branches;
	int32 NextStep = 0;
	double SegmentMs = 0.0;
	bool bReplaying = false;
	FString WaitingFor;

	TWeakObjectPtr<UConstructStudioDock> Target;
	int64 Generation = 0;
	FDelegateHandle Listening;
};
