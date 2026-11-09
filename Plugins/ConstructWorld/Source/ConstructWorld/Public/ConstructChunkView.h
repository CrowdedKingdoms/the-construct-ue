#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystem/CrowdyVoxels.h"
#include "ConstructChunkView.generated.h"

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

UCLASS()
class CONSTRUCTWORLD_API AConstructChunkView : public AActor
{
    GENERATED_BODY()

public:
    AConstructChunkView();

    /** Replaces the current draw with fetched chunks' grid cells, nearest chunks first; voxels outside the grid are not drawn. */
    UFUNCTION(BlueprintCallable, Category = "Construct|Chunks")
    void ShowChunks(const TArray<FCrowdyVoxelChunk>& Chunks, int64 CentreX, int64 CentreY, int64 CentreZ);

    /** Replaces the current draw with local cell lists, nearest chunks first. */
    void ShowCells(const TArray<FConstructChunkCells>& Chunks, const FInt64Vector3& Centre);

    UFUNCTION(BlueprintCallable, Category = "Construct|Chunks")
    void ClearChunks();

    UFUNCTION(BlueprintPure, Category = "Construct|Chunks")
    int32 GetCubeCount() const;

    UFUNCTION(BlueprintPure, Category = "Construct|Chunks")
    bool FindCube(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FIntVector Cell, FVector& WorldCentre, int32& Type) const;

private:
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
        int32 InstanceIndex;
        int32 Type;
    };

    UPROPERTY()
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;

    TMap<FCubeKey, FDrawnCube> DrawnCubes;
};
