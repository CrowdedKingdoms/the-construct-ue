#pragma once

#include "CoreMinimal.h"
#include "ConstructPresenceGameMode.h"
#include "ConstructPresencePawn.h"
#include "Containers/Ticker.h"
#include "Subsystem/CrowdyModScriptHost.h"
#include "Subsystem/CrowdyModScriptTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Object.h"
#include "ConstructLayer10Check.generated.h"

class UCrowdyModScripts;

/** The layer 10 check's pawn: the presence pawn with no local movement of its own, so the check places it. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer10CheckPawn : public AConstructPresencePawn
{
	GENERATED_BODY()

protected:
	// The check sets its transform itself, so Character Movement must never write it too.
	virtual bool ShouldUseLocalCharacterMovement() const override;
};

/** The presence GameMode with the check pawn. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer10CheckGameMode : public AConstructPresenceGameMode
{
	GENERATED_BODY()

public:
	AConstructLayer10CheckGameMode();
};

/** Which of the check's three runs this process is. */
enum class EConstructLayer10Run : uint8
{
	Control,
	Declined,
	Accepted
};

/** What the check counted while it ran. */
struct FConstructLayer10Counts
{
	int32 Started = 0;
	int32 Stopped = 0;
	int32 HudMarker = 0;
	int32 HudOther = 0;
	int32 Overlays = 0;
	int32 Clears = 0;
	int32 Errors = 0;
	int32 Prompts = 0;
};

/** The longest wall time between consecutive samples, in milliseconds. */
struct FConstructLayer10FrameTracker
{
	void Reset();
	void Add(double NowSeconds);
	double GetLongestMs() const { return LongestMs; }

private:
	double LastSeconds = -1.0;
	double LongestMs = 0.0;
};

/** The check's Mod Script host: it offers the scripts nothing and counts what reaches it, never keeping the HUD text. */
UCLASS(Transient)
class CONSTRUCTHARNESS_API UConstructLayer10CheckHost : public UObject, public ICrowdyModScriptHost
{
	GENERATED_BODY()

public:
	FString Marker;
	int32 HudMarker = 0;
	int32 HudOther = 0;
	int32 Overlays = 0;
	int32 Clears = 0;

	virtual void DrainClicks_Implementation(FCrowdyModScriptClicks& Clicks) override;
	virtual bool ReadChunk_Implementation(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FCrowdyModScriptChunk& Chunk) override;
	virtual void ListPlayers_Implementation(const FCrowdyModScriptGrid& Box, TArray<FCrowdyModScriptPlayer>& Players) override;
	virtual bool SetVoxel_Implementation(const FCrowdyModScript& Script, const FCrowdyVoxelWrite& Write, FString& Refusal) override;
	virtual void ShowHud_Implementation(const FCrowdyModScript& Script, const FString& Text) override;
	virtual void DrawOverlay_Implementation(const FCrowdyModScript& Script, const TArray<FCrowdyModScriptShape>& Shapes) override;
	virtual void ClearScript_Implementation(const FCrowdyModScript& Script) override;
};

/** On L_Layer10Check: enters grid M with the check's own host, answers the grid's prompt as the run says, and measures 20 s. */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer10CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Chunk M, the check's grid: one chunk the author claims for the check. */
	static constexpr int64 CheckChunkX = 39;
	static constexpr int64 CheckChunkY = -64;
	static constexpr int64 CheckChunkZ = 0;
	static constexpr double MeasureSeconds = 20.0;

	static bool ParseRun(const FString& Text, EConstructLayer10Run& OutRun);
	static const TCHAR* RunName(EConstructLayer10Run Run);

	/** Twelve lowercase hex digits. */
	static bool IsValidRunId(const FString& RunId);

	/** The text the test Mod Script sets on the HUD for this run id. */
	static FString MarkerFor(const FString& RunId);

	/** Whether Text is exactly the marker, case and all. */
	static bool IsMarkerText(const FString& Text, const FString& Marker);

	static bool GridContainsCheckChunk(const FCrowdyModScriptGrid& Grid);

	/** The centre of chunk M. */
	static FVector SpotFor(double ChunkSize);

	static FString StopReasonName(ECrowdyModScriptStop Reason);
	static FString FormatPromptLine(const FCrowdyModScriptPrompt& Prompt);
	static FString FormatSummaryLine(double LongestFrameMs, const FConstructLayer10Counts& Counts);

private:
	enum class EStage : uint8
	{
		WaitEntry,
		WaitGrid,
		Settle,
		WaitPrompt,
		Measure,
		Done,
		Failed
	};

	bool Tick(float DeltaTime);
	void TickEntry(double Now);
	void TickGrid(double Now);
	void TickSettle(double Now);
	void TickWaitPrompt(double Now);
	void TickMeasure(double Now);
	void BeginMeasure(double Now, const TCHAR* Stimulus);
	bool MoveToCheckChunk();
	AConstructLayer10CheckPawn* FindLocalPawn() const;
	bool IsLogging() const;
	bool IsSignedInAndConnected() const;
	void Fail(const FString& Why);

	UFUNCTION()
	void HandlePrompt(const FCrowdyModScriptPrompt& Prompt);
	UFUNCTION()
	void HandlePromptWithdrawn(int32 PromptId);
	UFUNCTION()
	void HandleStarted(const FCrowdyModScript& Script);
	UFUNCTION()
	void HandleStopped(const FCrowdyModScript& Script, ECrowdyModScriptStop Reason);
	UFUNCTION()
	void HandleError(const FCrowdyModScriptError& Error);

	UPROPERTY(Transient)
	TObjectPtr<UConstructLayer10CheckHost> Host;

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<UCrowdyModScripts> ModScripts;
	FConstructLayer10FrameTracker Frames;
	FConstructLayer10Counts Counts;
	FCrowdyModScriptPrompt PendingPrompt;
	EStage Stage = EStage::WaitEntry;
	EConstructLayer10Run Run = EConstructLayer10Run::Control;
	bool bHasPendingPrompt = false;
	double StartedAt = 0.0;
	double GridAt = 0.0;
	double EnterGridAt = 0.0;
	double MeasureStartedAt = 0.0;
	double TimeoutSeconds = 780.0;
};
