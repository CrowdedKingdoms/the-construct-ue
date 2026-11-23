#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InstanceDataTypes.h"
#include "Subsystem/CrowdyVoxels.h"
#include "ConstructChunkView.generated.h"

class UCrowdyVoxelWorld;
class UInstancedStaticMeshComponent;

struct FConstructChunkCell
{
    FIntVector Cell = FIntVector::ZeroValue;
    int32 Type = 0;
};

/** Local renderer input. Coordinates identify a chunk; cells identify its occupied positions. */
struct FConstructChunkCells
{
    FInt64Vector3 Chunk = FInt64Vector3::ZeroValue;
    TArray<FConstructChunkCell> Cells;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FConstructChunksRedrawn, int64, ChunkX, int64, ChunkY, int64, ChunkZ, const TArray<FIntVector>&, Positions);

UCLASS()
class CONSTRUCTWORLD_API AConstructChunkView : public AActor
{
    GENERATED_BODY()

public:
    AConstructChunkView();

    /** Replaces the current draw with fetched chunks' grid cells, nearest chunks first. */
    UFUNCTION(BlueprintCallable, Category = "Construct|Chunks")
    void ShowChunks(const TArray<FCrowdyVoxelChunk>& Chunks, int64 CentreX, int64 CentreY, int64 CentreZ);

    /** Replaces the current draw with local cell lists, nearest chunks first. */
    void ShowCells(const TArray<FConstructChunkCells>& Chunks, const FInt64Vector3& Centre);

    UFUNCTION(BlueprintCallable, Category = "Construct|Chunks")
    void ShowLoadedChunks(int64 CentreX, int64 CentreY, int64 CentreZ);

    UPROPERTY(BlueprintAssignable, Category = "Construct|Chunks")
    FConstructChunksRedrawn OnRedrawn;

    UFUNCTION(BlueprintCallable, Category = "Construct|Chunks")
    void ClearChunks();

    UFUNCTION(BlueprintPure, Category = "Construct|Chunks")
    int32 GetCubeCount() const;

    UFUNCTION(BlueprintPure, Category = "Construct|Chunks")
    bool FindCube(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FIntVector Cell, FVector& WorldCentre, int32& Type) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FConstructLiveViewTestAccess;
#endif

    struct FCubeKey
    {
        FInt64Vector3 Chunk;
        FIntVector Cell;

        bool operator==(const FCubeKey& Other) const
        {
            return Chunk == Other.Chunk && Cell == Other.Cell;
        }

        friend uint32 GetTypeHash(const FCubeKey& Key)
        {
            return HashCombine(GetTypeHash(Key.Chunk), GetTypeHash(Key.Cell));
        }
    };

    struct FDrawnCube
    {
        FPrimitiveInstanceId InstanceId;
        int32 Type;
    };

    using FVisitCells = TFunctionRef<void(FIntVector, int32)>;
    using FVisitChunk = TFunctionRef<void(const FInt64Vector3&, FVisitCells)>;
    using FRedrawnChunks = TMap<FInt64Vector3, TArray<FIntVector>>;

    UFUNCTION()
    void ChunkLoaded(int64 ChunkX, int64 ChunkY, int64 ChunkZ);

    UFUNCTION()
    void ChunkUnloaded(int64 ChunkX, int64 ChunkY, int64 ChunkZ);

    UFUNCTION()
    void ChunkChanged(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions);

    auto MakeChunkReader() const;
    void BeginLiveDraw(const FInt64Vector3& Centre);
    void AddLiveChunk(const FInt64Vector3& Chunk, TFunctionRef<void(FVisitCells)> ForEachCell, FVisitChunk VisitChunk);
    void ChangeLiveCells(const FInt64Vector3& Chunk, const TArray<FIntVector>& Positions, TFunctionRef<int32(FIntVector)> ReadCell, FVisitChunk VisitChunk);
    void RemoveLiveChunk(const FInt64Vector3& Chunk, FVisitChunk VisitChunk);
    void RebuildLive(FVisitChunk VisitChunk);
    void BroadcastRedrawn(const FRedrawnChunks& Chunks);
    void AddCube(const FCubeKey& Key, int32 Type, const FVector& Origin, double VoxelSize);
    void RemoveCube(const FCubeKey& Key);
    void ClearDrawnCubes();
    void StopLiveDraw();

    UPROPERTY(EditInstanceOnly, Category = "Construct|Chunks")
    bool bShowLoadedOnBeginPlay = false;

    UPROPERTY()
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;

    TMap<FCubeKey, FDrawnCube> DrawnCubes;
    TWeakObjectPtr<UCrowdyVoxelWorld> VoxelWorld;
    TArray<FInt64Vector3> LiveChunks;
    FInt64Vector3 LiveCentre = FInt64Vector3::ZeroValue;
    uint32 DrawGeneration = 0;
    bool bLiveDraw = false;
    bool bLiveTruncated = false;
};
