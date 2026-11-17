#pragma once

#include "CoreMinimal.h"
#include "ConstructPresenceGameMode.h"
#include "ConstructPresencePawn.h"
#include "ConstructStudioDockHost.h"
#include "ConstructStudioDockTypes.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Object.h"
#include "ConstructLayer11Check.generated.h"

class UConstructStudioDock;

/** The layer 11 check's pawn: the presence pawn with no local movement of its own, so the check places it. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer11CheckPawn : public AConstructPresencePawn
{
	GENERATED_BODY()

protected:
	// The check sets its transform itself, so Character Movement must never write it too.
	virtual bool ShouldUseLocalCharacterMovement() const override;
};

/** The presence GameMode with the check pawn. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer11CheckGameMode : public AConstructPresenceGameMode
{
	GENERATED_BODY()

public:
	AConstructLayer11CheckGameMode();
};

/** What the dock's state asks of the check next. */
enum class EConstructLayer11Step : uint8
{
	Wait,
	Consent,
	Ready,
	Failed,
	NotRun
};

/** One instruction from the check's driver, read from the signal file: send a prompt turn, or close and exit. */
struct FConstructLayer11Signal
{
	enum class EKind : uint8
	{
		Turn,
		Draft,
		Close
	};

	int64 Seq = 0;
	EKind Kind = EKind::Close;
	FString Text;
};

/** What the run reached, for the result file. It holds no prompt text, model output, label or token. */
struct FConstructLayer11Result
{
	struct FState
	{
		FString State;
		FString Reason;
		double At = 0.0;
	};

	struct FPrompt
	{
		int64 Seq = 0;
		FString Kind;
		bool bSent = false;
		double At = 0.0;
	};

	struct FDeploy
	{
		FString Kind;
		FString Answer;
		double At = 0.0;
		int32 ChangedFiles = 0;
		bool bReachesVisitors = false;
		bool bGridMatches = false;
		bool bProjectMatches = false;
		/** A run was in flight at some tick of the watch after this Not now. */
		bool bRunInFlight = false;
		/** The watch after this Not now lasted its full time. */
		bool bWatched = false;
	};

	TArray<FState> States;
	TArray<FPrompt> Prompts;
	TArray<FDeploy> DeployPrompts;
	FString Outcome = TEXT("running");
	FString Problem;
	double OpenedAt = -1.0;
	double ReadyAt = -1.0;
	int32 Withdrawn = 0;
	int32 Captures = 0;
	bool bConsentGiven = false;
};

/** The check's dock host: every capture is the same small solid image, answered on a later tick. */
UCLASS(Transient)
class CONSTRUCTHARNESS_API UConstructLayer11CheckHost : public UObject, public IConstructStudioDockHost
{
	GENERATED_BODY()

public:
	static constexpr int32 ImageWidth = 64;
	static constexpr int32 ImageHeight = 36;

	int32 Captures = 0;

	virtual void CaptureView_Implementation(const FString& Label, const FOnConstructStudioCaptured& OnDone) override;

	/** The solid image as a capture with Label as its caption, or a refusal when it cannot be encoded. */
	static FConstructStudioCapture SolidCapture(const FString& Label);
};

/**
 * On L_Layer11Check: once player A is signed in and standing in the check's chunk, puts the Studio dock on screen, opens it
 * on A's grid and the check's project, sends each prompt the driver writes, answers every deploy prompt Not now, and keeps a
 * result file beside the log.
 */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer11CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Chunk S, the check's grid: one chunk player A self-claims for the check. */
	static constexpr int64 CheckChunkX = 40;
	static constexpr int64 CheckChunkY = -64;
	static constexpr int64 CheckChunkZ = 0;

	/** Seconds the dock may take from Open to Ready, consent included, before the harness gives up on it. */
	static constexpr double DockWaitSeconds = 150.0;
	static constexpr int32 MaxPromptChars = 4000;

	/** Seconds after each Not now the harness watches the dock for a run that should not have started. */
	static constexpr double RunWatchSeconds = 30.0;

	/** Notes on a deploy answered at Entry.At whether a run is in flight at Now; true when Entry changed. A finished watch never changes. */
	static bool WatchDeploy(FConstructLayer11Result::FDeploy& Entry, double Now, bool bRunInFlight);

	static bool IsValidGridId(const FString& Text, int64& OutGridId);

	/** One to sixty-four letters, digits and dashes, as the platform's project ids are. */
	static bool IsValidProjectId(const FString& Id);

	static FText GridLabelFor(int64 ChunkX, int64 ChunkY, int64 ChunkZ);

	/** The centre of chunk S. */
	static FVector SpotFor(double ChunkSize);

	static EConstructLayer11Step StepFor(EConstructStudioDockState State, EConstructStudioDockReason Reason, bool bInMayConsent, bool bInConsentGiven);

	/** Why the check cannot run for a NotRun step. */
	static FString NotRunReason(EConstructStudioDockState State, EConstructStudioDockReason Reason);

	/** A signal newer than LastSeq; anything malformed, stale or unknown is not one. */
	static bool ParseSignal(const FString& Json, int64 InLastSeq, FConstructLayer11Signal& OutSignal);

	static const TCHAR* SignalKindName(FConstructLayer11Signal::EKind Kind);
	static FString StateName(EConstructStudioDockState State);
	static FString ReasonName(EConstructStudioDockReason Reason);
	static FString DeployKindName(EConstructStudioDeployKind Kind);
	static FString ResultToJson(const FConstructLayer11Result& Result);

private:
	enum class EStage : uint8
	{
		WaitEntry,
		WaitDock,
		Ready,
		Done,
		Failed
	};

	bool Tick(float DeltaTime);
	void TickEntry(double Now);
	void TickDock(double Now);
	void TickReady(double Now);
	void OpenDock(double Now);
	void ReadSignal(double Now);
	void AnswerPendingPrompt(double Now);
	void WatchDeploys(double Now);
	void Finish();
	bool MoveToCheckChunk();
	AConstructLayer11CheckPawn* FindLocalPawn() const;
	bool IsLogging() const;
	void Fail(const FString& Why);
	void NotRun(const FString& Why);
	void End(const TCHAR* Outcome, const TCHAR* Line, const FString& Why, int32 ExitCode);
	void ReleaseDock();
	void WriteResult();
	double Elapsed(double Now) const;

	UFUNCTION()
	void HandleStateChanged(EConstructStudioDockState State, EConstructStudioDockReason Reason);
	UFUNCTION()
	void HandleDeployPrompt(const FConstructStudioDeployPrompt& Prompt);
	UFUNCTION()
	void HandleDeployPromptWithdrawn(int64 PromptId);

	UPROPERTY(Transient)
	TObjectPtr<UConstructStudioDock> Dock;

	UPROPERTY(Transient)
	TObjectPtr<UConstructLayer11CheckHost> Host;

	FTSTicker::FDelegateHandle TickerHandle;
	FConstructLayer11Result Result;
	FConstructStudioDeployPrompt PendingPrompt;
	FString ProjectId;
	FString ResultPath;
	FString SignalPath;
	EStage Stage = EStage::WaitEntry;
	int64 GridId = 0;
	int64 LastSeq = 0;
	double StartedAt = 0.0;
	double OpenedAt = 0.0;
	double LastSignalRead = 0.0;
	double TimeoutSeconds = 2400.0;
	bool bMayConsent = false;
	bool bHasPendingPrompt = false;
	bool bClosing = false;
};
