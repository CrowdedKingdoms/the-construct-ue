#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystem/CrowdyVoxels.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer6Check.generated.h"

class AConstructChunkView;
class AConstructPaintCanvas;
struct FCrowdyChunkFetch;

/**
 * On L_Layer6Check: once signed in, connected, with the other player's pawn in view and the canvas chunk live, follows
 * Tools/Layer6Check's control file: paints the cells a "go" names through the placed AConstructPaintCanvas, and for a
 * "watch" logs what it sees of the other player's cells and the cubes the placed AConstructChunkView draws for them.
 */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer6CheckSubsystem : public UWorldSubsystem
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
		Running,
		Fetching,
		Done,
		Failed
	};

	// One cell of a "go": B logs its pending, edit and after lines in that order.
	struct FPaintStep
	{
		FIntVector Cell = FIntVector::ZeroValue;
		int32 Type = 0;
		uint8 Logged = 0;
	};

	bool Tick(float DeltaTime);
	bool TickEntry();
	bool HasVisiblePeer() const;
	void PollControl();
	void Act(const FString& Line);
	void StartPaint(const TArray<FPaintStep>& Steps);
	void LogCubes();
	void FetchFresh();
	void HandleFresh(const FCrowdyChunkFetch& Fetch);
	void LogCell(const TCHAR* Kind, const FIntVector& Cell);
	void Fail(const FString& Why);

	UFUNCTION()
	void HandleChanged(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions);

	UFUNCTION()
	void HandleRedrawn(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions);

	UFUNCTION()
	void HandlePaintDone(FIntVector Cell, int32 Type, ECrowdyVoxelResult Result, const FString& Message);

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<AConstructPaintCanvas> Canvas;
	TWeakObjectPtr<AConstructChunkView> View;
	FString ControlPath;
	TArray<FPaintStep> Painting;
	TArray<FIntVector> Watched;
	TSet<FIntVector> Redrawn;
	EStage Stage = EStage::WaitEntry;
	int32 LinesDone = 0;
	int32 Answered = 0;
	double StartedAt = 0.0;
	double LastPollAt = 0.0;
	double WatchedAt = 0.0;
	bool bWatching = false;
	bool bCubesLogged = false;
};
