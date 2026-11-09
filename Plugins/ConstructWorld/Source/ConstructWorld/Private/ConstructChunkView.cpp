#include "ConstructChunkView.h"

#include "Algo/StableSort.h"
#include "Components/SceneComponent.h"
#include "ConstructChunkDrawing.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr int32 BlockMaterialCount = 12;
    constexpr int32 MaxCubes = 4096;

    int32 GetMaterialIndex(int32 Type)
    {
        return ((Type - 1) % BlockMaterialCount + BlockMaterialCount) % BlockMaterialCount;
    }

    uint64 AxisDistance(int64 A, int64 B)
    {
        // Subtract in unsigned space so the full signed coordinate range fits.
        return A >= B ? uint64(A) - uint64(B) : uint64(B) - uint64(A);
    }

    uint64 ChunkDistance(const FInt64Vector3& Chunk, const FInt64Vector3& Centre)
    {
        return FMath::Max3(AxisDistance(Chunk.X, Centre.X), AxisDistance(Chunk.Y, Centre.Y), AxisDistance(Chunk.Z, Centre.Z));
    }

    // Indexes into Coords, nearest to Centre first, ties in the given order.
    TArray<int32> NearestFirst(TConstArrayView<FInt64Vector3> Coords, const FInt64Vector3& Centre)
    {
        TArray<TPair<uint64, int32>> Order;
        Order.Reserve(Coords.Num());
        for (int32 Index = 0; Index < Coords.Num(); ++Index)
        {
            Order.Emplace(ChunkDistance(Coords[Index], Centre), Index);
        }
        Algo::StableSortBy(Order, [](const TPair<uint64, int32>& Entry) { return Entry.Key; });
        TArray<int32> Indexes;
        Indexes.Reserve(Order.Num());
        for (const TPair<uint64, int32>& Entry : Order)
        {
            Indexes.Add(Entry.Value);
        }
        return Indexes;
    }
}

AConstructChunkView::AConstructChunkView()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ChunkView"));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Batches.Reserve(BlockMaterialCount);
    for (int32 Index = 0; Index < BlockMaterialCount; ++Index)
    {
        const FString Name = FString::Printf(TEXT("Block%02d"), Index + 1);
        const FString MaterialName = TEXT("MI_Construct") + Name;
        const FString Path = FString::Printf(TEXT("/Game/Construct/World/%s.%s"), *MaterialName, *MaterialName);
        const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(*Path);

        UInstancedStaticMeshComponent* Batch = CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(*Name));
        Batch->SetupAttachment(RootComponent);
        Batch->SetAbsolute(true, true, true);
        ConstructChunkDrawing::ConfigureBatch(*Batch, Cube.Object, Material.Object);
        Batches.Add(Batch);
    }
}

void AConstructChunkView::ShowChunks(const TArray<FCrowdyVoxelChunk>& Chunks, int64 CentreX, int64 CentreY, int64 CentreZ)
{
    // The grid's non-zero cells are exactly the fetched voxels inside the grid with a type of 1 to 255. Chunks are taken
    // nearest first and only up to the cap, so a wide fetch never converts cells that could not be drawn.
    const FInt64Vector3 Centre(CentreX, CentreY, CentreZ);
    TArray<FInt64Vector3> Coords;
    Coords.Reserve(Chunks.Num());
    for (const FCrowdyVoxelChunk& Chunk : Chunks)
    {
        Coords.Emplace(Chunk.X, Chunk.Y, Chunk.Z);
    }
    TArray<FConstructChunkCells> Cells;
    int32 Left = MaxCubes;
    for (const int32 Fetched : NearestFirst(Coords, Centre))
    {
        if (Left == 0)
        {
            break;
        }
        const FCrowdyVoxelChunk& Chunk = Chunks[Fetched];
        FConstructChunkCells& Out = Cells.AddDefaulted_GetRef();
        Out.Chunk = Coords[Fetched];
        const int32 Count = FMath::Min(Chunk.Grid.Num(), ConstructChunkDrawing::CellsPerChunk);
        for (int32 Index = 0; Index < Count && Left > 0; ++Index)
        {
            if (Chunk.Grid[Index] != 0)
            {
                Out.Cells.Add({ FIntVector(Index & 15, (Index >> 4) & 15, Index >> 8), Chunk.Grid[Index] });
                --Left;
            }
        }
    }
    ShowCells(Cells, Centre);
}

void AConstructChunkView::ShowCells(const TArray<FConstructChunkCells>& Chunks, const FInt64Vector3& Centre)
{
    ClearChunks();

    TArray<FInt64Vector3> Coords;
    Coords.Reserve(Chunks.Num());
    for (const FConstructChunkCells& Chunk : Chunks)
    {
        Coords.Add(Chunk.Chunk);
    }
    // Placed by the SDK's helpers, so a cube stands in the chunk the SDK gives an actor at that spot.
    const double VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
    const FVector CubeScale(VoxelSize / ConstructChunkDrawing::EngineCubeSize);

    TArray<FTransform> Transforms[BlockMaterialCount];
    for (const int32 ChunkIndex : NearestFirst(Coords, Centre))
    {
        if (DrawnCubes.Num() == MaxCubes)
        {
            break;
        }
        const FConstructChunkCells& Chunk = Chunks[ChunkIndex];
        const FVector Origin = UCrowdyVoxelLibrary::GetChunkOrigin(this, Chunk.Chunk.X, Chunk.Chunk.Y, Chunk.Chunk.Z);
        for (const FConstructChunkCell& Cell : Chunk.Cells)
        {
            if (Cell.Type < 1 || !ConstructChunkDrawing::IsInsideChunk(Cell.Cell))
            {
                continue;
            }
            const FCubeKey Key{ Chunk.Chunk, Cell.Cell };
            if (DrawnCubes.Contains(Key))
            {
                continue;
            }

            const FVector WorldCentre = Origin + ConstructChunkDrawing::GetCellOffset(Cell.Cell, VoxelSize);
            const int32 Index = Transforms[GetMaterialIndex(Cell.Type)].Emplace(FQuat::Identity, WorldCentre, CubeScale);
            DrawnCubes.Add(Key, { Index, Cell.Type });
            if (DrawnCubes.Num() == MaxCubes)
            {
                break;
            }
        }
    }
    for (int32 Index = 0; Index < BlockMaterialCount; ++Index)
    {
        if (!Transforms[Index].IsEmpty())
        {
            Batches[Index]->AddInstances(Transforms[Index], false, true, false);
        }
    }
}

void AConstructChunkView::ClearChunks()
{
    for (UInstancedStaticMeshComponent* Batch : Batches)
    {
        Batch->ClearInstances();
    }
    DrawnCubes.Reset();
}

int32 AConstructChunkView::GetCubeCount() const
{
    return DrawnCubes.Num();
}

bool AConstructChunkView::FindCube(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FIntVector Cell, FVector& WorldCentre, int32& Type) const
{
    WorldCentre = FVector::ZeroVector;
    Type = 0;

    const FDrawnCube* Drawn = DrawnCubes.Find({ FInt64Vector3(ChunkX, ChunkY, ChunkZ), Cell });
    if (!Drawn)
    {
        return false;
    }

    FTransform Instance;
    const UInstancedStaticMeshComponent& Batch = *Batches[GetMaterialIndex(Drawn->Type)];
    if (!Batch.GetInstanceTransform(Drawn->InstanceIndex, Instance, true))
    {
        return false;
    }

    WorldCentre = Instance.GetLocation();
    Type = Drawn->Type;
    return true;
}
