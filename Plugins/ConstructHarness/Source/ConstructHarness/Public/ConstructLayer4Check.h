#pragma once

#include "CoreMinimal.h"
#include "ConstructHubTypes.h"
#include "Containers/Ticker.h"
#include "CrowdyServerObjectTypes.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Actor.h"
#include "StructUtils/PropertyBag.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer4Check.generated.h"

class UCrowdyServerObject;
class UCrowdyServerObjectDefinition;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnConstructLayer4ProbeAnswer, int32 /*Step*/, const FString& /*AnswerJson*/);

/**
 * The layer 4 check's Blueprint path. A Blueprint subclass implements each Run event with the hub's typed nodes and
 * passes the answer to the matching Report function, so the check can compare it with the C++ and Node paths.
 */
UCLASS(Blueprintable)
class CONSTRUCTHARNESS_API AConstructLayer4CheckProbe : public AActor
{
	GENERATED_BODY()

public:
	/** Fires once per Report call, with the answer as one line of JSON. */
	FOnConstructLayer4ProbeAnswer OnAnswer;

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Construct|Layer4Check")
	void RunReadWorld(int32 Step);

	/**
	 * InstanceId is a user id in digits: the caller's own, or another player's for the check's refused read. A read has no
	 * Outcome pin, so check the object's status first, then bind On Status Changed: Ready goes to ReportProgress, Failed to
	 * ReportReadRefused with Get Failure Reason. A Get node alone answers defaults on a refused read.
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Construct|Layer4Check")
	void RunReadProgress(int32 Step, const FString& InstanceId);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Construct|Layer4Check")
	void RunListPrograms(int32 Step);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Construct|Layer4Check")
	void RunListClaims(int32 Step, bool bOnlyChunk, const FConstructHubChunk& Chunk, bool bOnlyMine, int32 Limit);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Construct|Layer4Check")
	void RunRecordClaim(int32 Step, int64 GridId, const FConstructHubChunk& Chunk);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Construct|Layer4Check")
	void RunReleaseClaim(int32 Step, int64 GridId);

	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Construct|Layer4Check")
	void RunForgetClaim(int32 Step, int64 GridId);

	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check")
	void ReportWorld(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, int64 Pulses, int64 LastPulseAt);

	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check")
	void ReportProgress(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, int64 Xp, int64 Level, int64 SkillPoints);

	/** A read the server refused. It has no reply, and its Reason is the SDK's failure text, not the server's. */
	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check")
	void ReportReadRefused(int32 Step, const FString& FailureReason);

	// On a call's Failed branch, leave the reply pins unwired: a typed node's outputs still hold its last success there.
	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check", meta = (AutoCreateRefTerm = "Programs"))
	void ReportPrograms(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TArray<FConstructHubProgram>& Programs);

	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check", meta = (AutoCreateRefTerm = "Claims"))
	void ReportClaims(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TArray<FConstructHubClaim>& Claims, int32 Total);

	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check", meta = (AutoCreateRefTerm = "Claim"))
	void ReportRecord(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, bool bRecorded, const FConstructHubClaim& Claim);

	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check")
	void ReportRelease(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, bool bReleased);

	UFUNCTION(BlueprintCallable, Category = "Construct|Layer4Check")
	void ReportForget(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, bool bForgotten);

	/** A claim as a JSON object. Each int64 is a string: a JSON number loses exactness above 2^53, and the check compares ids exactly. */
	static TSharedRef<FJsonObject> ClaimToJson(const FConstructHubClaim& Claim);

	/** One answer as one line of JSON: the outcome's name, the reason, whether it is retryable, and the reply's fields, which only a Success carries. */
	static FString AnswerToJson(ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TSharedRef<FJsonObject>& Reply);

private:
	void Emit(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TSharedRef<FJsonObject>& Reply);
};

/**
 * On L_Layer4Check: runs Tools/Layer4Check's step plan through C++ and the placed Blueprint probe, one answer at a time,
 * and writes each answer to the results file for Node to compare. Answers never reach the log: they hold user ids.
 */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer4CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	enum class EStage : uint8 { WaitEntry, WaitHub, WaitGo, RunStep, WaitAnswer, WaitNode, Done, Failed };

	struct FCheckOp
	{
		FString Id;
		FString Kind;
		int64 GridId = 0;
		FConstructHubChunk Chunk;
		bool bOnlyChunk = false;
		bool bOnlyMine = false;
		int32 Limit = 0;
		FString InstanceId;
		bool bQuick = false;
	};

	struct FCheckStep
	{
		int32 Number = 0;
		TArray<FCheckOp> Native;
		TArray<FCheckOp> Blueprint;
	};

	bool Tick(float DeltaTime);
	bool LoadPlan();
	bool TickEntry();
	void TickStep(double Now);
	void RunNative(const FCheckOp& Op);
	void RunBlueprint(const FCheckOp& Op);
	void CallHub(UCrowdyServerObject* Object, FName Function, const FInstancedPropertyBag& Params, TFunction<void(const FCrowdyServerCallResult&, const FInstancedPropertyBag&)> OnReply);
	void ReadProgress(const FString& InstanceId);
	void FinishProgressRead();
	void HandleAnswer(int32 OpIndex, const FString& AnswerJson);
	bool ReadControl(const FString& Expected) const;
	void Fail(const FString& Why);

	UPROPERTY()
	TObjectPtr<UCrowdyServerObjectDefinition> WorldDefinition;

	UPROPERTY()
	TObjectPtr<UCrowdyServerObjectDefinition> ClaimsDefinition;

	UPROPERTY()
	TObjectPtr<UCrowdyServerObjectDefinition> ProgressDefinition;

	UPROPERTY()
	TObjectPtr<UCrowdyServerObject> World;

	UPROPERTY()
	TObjectPtr<UCrowdyServerObject> Claims;

	UPROPERTY()
	TObjectPtr<UCrowdyServerObject> Progress;

	/** The object a progress read is waiting on: the player's own, or another's for the refused read. */
	UPROPERTY()
	TObjectPtr<UCrowdyServerObject> ReadingProgress;

	/** Reports the C++ path's answers, so they take the same form as the Blueprint path's. */
	UPROPERTY()
	TObjectPtr<AConstructLayer4CheckProbe> NativeReporter;

	UPROPERTY()
	TObjectPtr<AConstructLayer4CheckProbe> BlueprintProbe;

	EStage Stage = EStage::WaitEntry;
	TArray<FCheckStep> Steps;
	int32 StepIndex = 0;
	bool bBlueprintTurn = false;
	int32 OpIndex = 0;
	bool bAnswerWaiting = false;
	FString SelfId;
	FString ControlPath;
	FString ResultsPath;
	FString PlanPath;
	double StartedAt = 0.0;
	double LastPollAt = 0.0;
	double NextOpAt = 0.0;
	double AnswerDeadline = 0.0;
	FTSTicker::FDelegateHandle TickerHandle;
};
