#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystem/CrowdyClaims.h"
#include "Subsystem/CrowdyMods.h"
#include "Subsystem/CrowdyVoxels.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer8Check.generated.h"

class AConstructChunkView;
class AConstructClaimPad;
class APawn;
class UConstructModPanel;
class UCrowdyVoxelWorld;
enum class EConstructModPanelState : uint8;
struct FConstructModFileHash;

/** On L_Layer8Check, drives the pad's mod panel from Tools/Layer8Check's control file; ids go to the results file, never the log. */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer8CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** The control file's instructions from index FirstNew on. A last line with no newline yet is left for a later read. */
	static TArray<FString> CompleteLines(const FString& Text, int32 FirstNew);

	/** Whether the panel's state offers what a panel instruction calls; true for instructions that need no panel. */
	static bool IsReadyFor(const FString& Instruction, EConstructModPanelState State);

	/** The starter's post, hub and four arm cells, in that order. */
	static TArray<FIntVector> SpinnerCells();

	/** Log lines. */
	static FString EnabledLine(ECrowdyModResult Result, bool bEnabled, bool bRunning);
	static FString DeployLine(ECrowdyModResult Result, int64 Version, bool bEnabled);
	static FString CubeLine(const FIntVector& Cell, bool bDrawn, int32 Type, const FVector& Centre);

	/** Results-file lines. Ids are strings, so a JSON reader keeps them exact. */
	static FString ClaimJson(ECrowdyClaimResult Result, int64 GridId);
	static FString BuildJson(ECrowdyModResult Result, ECrowdyModBuildStatus Status, const FString& DoneBuildId, const FString& BuildId, const TArray<FConstructModFileHash>& Files);
	static FString ReloadJson(ECrowdyVoxelResult Result, const FString& BeforeSha1, const FString& Sha1, const TArray<FIntVector>& ChangedCells, const TArray<FIntVector>& RedrawnCells);

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
		AwaitOwned,
		AwaitNoMod,
		AwaitReady,
		Building,
		Deploying,
		Enabling,
		Waking,
		Disabling,
		Reloading,
		Logging,
		Deleting,
		Releasing
	};

	bool Tick(float DeltaTime);
	bool TickEntry();
	void TickStep();
	void UpdateFollow();
	void PollControl();
	void Act(const FString& Line);
	void StartClaim();
	void OpenPanel();
	void StartPanelStep();
	void StartReload();
	void HandleReloaded(ECrowdyVoxelResult Result);
	void StartDelete();
	void StartRelease();
	FString PanelStateName() const;
	void Begin(EStep Next, double Seconds, const FString& What);
	void Finish();
	void WriteResult(const FString& Line);
	void Fail(const FString& Why);

	UFUNCTION()
	void HandleClaimDone(ECrowdyClaimResult Result, int64 GridId, bool bListed, const FString& Message);

	UFUNCTION()
	void HandleReleaseDone(ECrowdyClaimResult Result, int64 GridId, bool bUnlisted, const FString& Message);

	UFUNCTION()
	void HandleBuildProgress(ECrowdyModBuildStatus Status);

	UFUNCTION()
	void HandleBuildDone(ECrowdyModResult Result, ECrowdyModBuildStatus Status, const FString& BuildId);

	UFUNCTION()
	void HandleDeployed(ECrowdyModResult Result, int64 Version, bool bEnabled);

	UFUNCTION()
	void HandleEnabledChanged(ECrowdyModResult Result, bool bEnabled, bool bRunning);

	UFUNCTION()
	void HandleWakeDone(ECrowdyModResult Result, const FString& Message);

	UFUNCTION()
	void HandleLogShown(ECrowdyModResult Result, int32 LineCount);

	UFUNCTION()
	void HandleDeleted(ECrowdyModResult Result);

	UFUNCTION()
	void HandleChunkChanged(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions);

	UFUNCTION()
	void HandleRedrawn(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions);

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<AConstructClaimPad> Pad;
	TWeakObjectPtr<AConstructChunkView> View;
	TWeakObjectPtr<UCrowdyVoxelWorld> VoxelWorld;
	TWeakObjectPtr<UConstructModPanel> Panel;
	TWeakObjectPtr<APawn> FollowedPawn;
	FString ControlPath;
	FString ResultsPath;
	FString PendingLine;
	FString StepWhat;
	FString ReloadBefore;
	TArray<FIntVector> Changed;
	TArray<FIntVector> Redrawn;
	EStage Stage = EStage::WaitEntry;
	EStep Step = EStep::Idle;
	int32 LinesDone = 0;
	int64 ClaimedGrid = 0;
	double StartedAt = 0.0;
	double LastPollAt = 0.0;
	double StepDeadline = 0.0;
	uint64 StepSerial = 0;
	bool bModDeleted = false;
};
