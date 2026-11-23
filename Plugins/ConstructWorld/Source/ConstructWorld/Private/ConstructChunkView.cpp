#include "ConstructChunkView.h"

#include "Algo/StableSort.h"
#include "Components/SceneComponent.h"
#include "ConstructChunkDrawing.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "Subsystem/CrowdyVoxelWorld.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr int32 BlockMaterialCount = ConstructChunkDrawing::BlockMaterialCount;
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

    FVector ChunkOrigin(const AConstructChunkView* View, const FInt64Vector3& Chunk)
    {
        return UCrowdyVoxelLibrary::GetChunkOrigin(View, Chunk.X, Chunk.Y, Chunk.Z);
    }

    FTransform CellTransform(FIntVector Cell, const FVector& Origin, double VoxelSize)
    {
        const FVector Centre = Origin + ConstructChunkDrawing::GetCellOffset(Cell, VoxelSize);
        return FTransform(FQuat::Identity, Centre, FVector(VoxelSize / ConstructChunkDrawing::EngineCubeSize));
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
        const FString Path = ConstructChunkDrawing::GetBlockMaterialPath(Index + 1);
        const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(*Path);

        UInstancedStaticMeshComponent* Batch = CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(*Name));
        Batch->SetupAttachment(RootComponent);
        Batch->SetAbsolute(true, true, true);
        ConstructChunkDrawing::ConfigureBatch(*Batch, Cube.Object, Material.Object);
        Batches.Add(Batch);
    }
}

void AConstructChunkView::BeginPlay()
{
    Super::BeginPlay();
    if (bShowLoadedOnBeginPlay)
    {
        FInt64Vector3 Centre;
        FIntVector Cell;
        UCrowdyVoxelLibrary::WorldToVoxel(this, GetActorLocation(), Centre.X, Centre.Y, Centre.Z, Cell);
        ShowLoadedChunks(Centre.X, Centre.Y, Centre.Z);
    }
}

void AConstructChunkView::EndPlay(const EEndPlayReason::Type Reason)
{
    StopLiveDraw();
    Super::EndPlay(Reason);
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
    const double VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
    TArray<FTransform> Transforms[BlockMaterialCount];
    TArray<FCubeKey> Keys[BlockMaterialCount];
    for (const int32 ChunkIndex : NearestFirst(Coords, Centre))
    {
        if (DrawnCubes.Num() == MaxCubes)
        {
            break;
        }
        const FConstructChunkCells& Chunk = Chunks[ChunkIndex];
        const FVector Origin = ChunkOrigin(this, Chunk.Chunk);
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

            const int32 Material = GetMaterialIndex(Cell.Type);
            Transforms[Material].Add(CellTransform(Cell.Cell, Origin, VoxelSize));
            Keys[Material].Add(Key);
            DrawnCubes.Add(Key, { FPrimitiveInstanceId(), Cell.Type });
            if (DrawnCubes.Num() == MaxCubes)
            {
                break;
            }
        }
    }
    for (int32 Material = 0; Material < BlockMaterialCount; ++Material)
    {
        if (!Transforms[Material].IsEmpty())
        {
            const TArray<FPrimitiveInstanceId> Ids = Batches[Material]->AddInstancesById(Transforms[Material], true, false);
            for (int32 Index = 0; Index < Ids.Num(); ++Index)
            {
                DrawnCubes.FindChecked(Keys[Material][Index]).InstanceId = Ids[Index];
            }
        }
    }
}

auto AConstructChunkView::MakeChunkReader() const
{
    return [this](const FInt64Vector3& Chunk, FVisitCells Visit)
    {
        if (const UCrowdyVoxelWorld* Voxels = VoxelWorld.Get())
        {
            Voxels->ForEachFilledVoxel(Chunk.X, Chunk.Y, Chunk.Z, Visit);
        }
    };
}

void AConstructChunkView::ShowLoadedChunks(int64 CentreX, int64 CentreY, int64 CentreZ)
{
    BeginLiveDraw(FInt64Vector3(CentreX, CentreY, CentreZ));
    UCrowdyVoxelWorld* Voxels = GetWorld() ? GetWorld()->GetSubsystem<UCrowdyVoxelWorld>() : nullptr;
    if (!Voxels)
    {
        StopLiveDraw();
        return;
    }

    VoxelWorld = Voxels;
    Voxels->OnChunkLoaded.AddUniqueDynamic(this, &ThisClass::ChunkLoaded);
    Voxels->OnChunkUnloaded.AddUniqueDynamic(this, &ThisClass::ChunkUnloaded);
    Voxels->OnChunkChanged.AddUniqueDynamic(this, &ThisClass::ChunkChanged);
    Voxels->ForEachLoadedChunk([this](int64 X, int64 Y, int64 Z)
    {
        LiveChunks.Emplace(X, Y, Z);
    });
    RebuildLive(MakeChunkReader());
}

void AConstructChunkView::BeginLiveDraw(const FInt64Vector3& Centre)
{
    ClearChunks();
    LiveCentre = Centre;
    bLiveDraw = true;
}

void AConstructChunkView::ChunkLoaded(int64 ChunkX, int64 ChunkY, int64 ChunkZ)
{
    const FInt64Vector3 Chunk(ChunkX, ChunkY, ChunkZ);
    const auto Reader = MakeChunkReader();
    AddLiveChunk(Chunk, [&Reader, &Chunk](FVisitCells Visit) { Reader(Chunk, Visit); }, Reader);
}

void AConstructChunkView::ChunkUnloaded(int64 ChunkX, int64 ChunkY, int64 ChunkZ)
{
    RemoveLiveChunk(FInt64Vector3(ChunkX, ChunkY, ChunkZ), MakeChunkReader());
}

void AConstructChunkView::ChunkChanged(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions)
{
    if (const UCrowdyVoxelWorld* Voxels = VoxelWorld.Get())
    {
        ChangeLiveCells(FInt64Vector3(ChunkX, ChunkY, ChunkZ), Positions, [Voxels, ChunkX, ChunkY, ChunkZ](FIntVector Cell)
        {
            return Voxels->GetVoxel(ChunkX, ChunkY, ChunkZ, Cell).Type;
        }, MakeChunkReader());
    }
}

void AConstructChunkView::AddLiveChunk(const FInt64Vector3& Chunk, TFunctionRef<void(FVisitCells)> ForEachCell, FVisitChunk VisitChunk)
{
    if (!bLiveDraw || LiveChunks.Contains(Chunk))
    {
        return;
    }
    LiveChunks.Add(Chunk);

    const int32 Remaining = MaxCubes - DrawnCubes.Num();
    int32 Occupied = 0;
    ForEachCell([&Occupied, Remaining](FIntVector Cell, int32 Type)
    {
        if (Occupied <= Remaining && Type > 0 && ConstructChunkDrawing::IsInsideChunk(Cell))
        {
            ++Occupied;
        }
    });
    if (Occupied > Remaining)
    {
        RebuildLive(VisitChunk);
        return;
    }

    const double VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
    const FVector Origin = ChunkOrigin(this, Chunk);
    TArray<FIntVector> Redrawn;
    Redrawn.Reserve(Occupied);
    ForEachCell([this, &Chunk, &Origin, &Redrawn, VoxelSize](FIntVector Cell, int32 Type)
    {
        if (Type > 0 && ConstructChunkDrawing::IsInsideChunk(Cell))
        {
            AddCube({Chunk, Cell}, Type, Origin, VoxelSize);
            Redrawn.Add(Cell);
        }
    });
    if (!Redrawn.IsEmpty())
    {
        OnRedrawn.Broadcast(Chunk.X, Chunk.Y, Chunk.Z, Redrawn);
    }
}

void AConstructChunkView::ChangeLiveCells(const FInt64Vector3& Chunk, const TArray<FIntVector>& Positions, TFunctionRef<int32(FIntVector)> ReadCell, FVisitChunk VisitChunk)
{
    if (!bLiveDraw || !LiveChunks.Contains(Chunk))
    {
        return;
    }

    TArray<FConstructChunkCell> Changes;
    TSet<FIntVector> Seen;
    int32 FinalCount = DrawnCubes.Num();
    bool bCouldChangeCapSelection = false;
    for (const FIntVector Cell : Positions)
    {
        if (!ConstructChunkDrawing::IsInsideChunk(Cell) || Seen.Contains(Cell))
        {
            continue;
        }
        Seen.Add(Cell);

        const FDrawnCube* Drawn = DrawnCubes.Find({Chunk, Cell});
        const int32 RawType = ReadCell(Cell);
        const int32 Type = RawType >= 1 && RawType <= MAX_uint8 ? RawType : 0;
        if (Drawn && Type <= 0)
        {
            --FinalCount;
            bCouldChangeCapSelection = true;
        }
        else if (!Drawn && Type > 0)
        {
            ++FinalCount;
            bCouldChangeCapSelection = true;
        }

        bCouldChangeCapSelection |= bLiveTruncated && !Drawn;
        if ((Drawn && Drawn->Type != Type) || (!Drawn && Type > 0))
        {
            Changes.Add({Cell, Type});
        }
    }
    if (FinalCount > MaxCubes || (bLiveTruncated && bCouldChangeCapSelection))
    {
        RebuildLive(VisitChunk);
        return;
    }

    // Remove first so a replacement batch at the limit never temporarily exceeds it.
    for (const FConstructChunkCell& Change : Changes)
    {
        const FCubeKey Key{Chunk, Change.Cell};
        if (FDrawnCube* Drawn = DrawnCubes.Find(Key))
        {
            if (Change.Type > 0 && GetMaterialIndex(Drawn->Type) == GetMaterialIndex(Change.Type))
            {
                Drawn->Type = Change.Type;
            }
            else
            {
                RemoveCube(Key);
            }
        }
    }

    const double VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
    const FVector Origin = ChunkOrigin(this, Chunk);
    TArray<FIntVector> Redrawn;
    Redrawn.Reserve(Changes.Num());
    for (const FConstructChunkCell& Change : Changes)
    {
        const FCubeKey Key{Chunk, Change.Cell};
        if (Change.Type > 0 && !DrawnCubes.Contains(Key))
        {
            AddCube(Key, Change.Type, Origin, VoxelSize);
        }
        Redrawn.Add(Change.Cell);
    }
    if (!Redrawn.IsEmpty())
    {
        OnRedrawn.Broadcast(Chunk.X, Chunk.Y, Chunk.Z, Redrawn);
    }
}

void AConstructChunkView::RemoveLiveChunk(const FInt64Vector3& Chunk, FVisitChunk VisitChunk)
{
    if (!bLiveDraw || LiveChunks.RemoveSingle(Chunk) == 0)
    {
        return;
    }
    if (bLiveTruncated)
    {
        RebuildLive(VisitChunk);
        return;
    }

    TArray<FIntVector> Redrawn;
    for (const auto& Pair : DrawnCubes)
    {
        if (Pair.Key.Chunk == Chunk)
        {
            Redrawn.Add(Pair.Key.Cell);
        }
    }
    for (const FIntVector Cell : Redrawn)
    {
        RemoveCube({Chunk, Cell});
    }
    if (!Redrawn.IsEmpty())
    {
        OnRedrawn.Broadcast(Chunk.X, Chunk.Y, Chunk.Z, Redrawn);
    }
}

void AConstructChunkView::RebuildLive(FVisitChunk VisitChunk)
{
    const TMap<FCubeKey, FDrawnCube> Previous = MoveTemp(DrawnCubes);
    ClearDrawnCubes();
    bLiveTruncated = false;

    const double VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
    for (const int32 Index : NearestFirst(LiveChunks, LiveCentre))
    {
        if (bLiveTruncated)
        {
            break;
        }
        const FInt64Vector3& Chunk = LiveChunks[Index];
        const FVector Origin = ChunkOrigin(this, Chunk);
        VisitChunk(Chunk, [this, &Chunk, &Origin, VoxelSize](FIntVector Cell, int32 Type)
        {
            if (Type <= 0 || !ConstructChunkDrawing::IsInsideChunk(Cell))
            {
                return;
            }
            if (DrawnCubes.Num() == MaxCubes)
            {
                bLiveTruncated = true;
                return;
            }
            AddCube({Chunk, Cell}, Type, Origin, VoxelSize);
        });
    }

    FRedrawnChunks Redrawn;
    for (const auto& Pair : DrawnCubes)
    {
        const FDrawnCube* Old = Previous.Find(Pair.Key);
        if (!Old || Old->Type != Pair.Value.Type)
        {
            Redrawn.FindOrAdd(Pair.Key.Chunk).Add(Pair.Key.Cell);
        }
    }
    for (const auto& Pair : Previous)
    {
        if (!DrawnCubes.Contains(Pair.Key))
        {
            Redrawn.FindOrAdd(Pair.Key.Chunk).Add(Pair.Key.Cell);
        }
    }
    BroadcastRedrawn(Redrawn);
}

void AConstructChunkView::BroadcastRedrawn(const FRedrawnChunks& Chunks)
{
    const uint32 Generation = DrawGeneration;
    for (const auto& Pair : Chunks)
    {
        if (Generation != DrawGeneration)
        {
            break;
        }
        OnRedrawn.Broadcast(Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value);
    }
}

void AConstructChunkView::AddCube(const FCubeKey& Key, int32 Type, const FVector& Origin, double VoxelSize)
{
    UInstancedStaticMeshComponent& Batch = *Batches[GetMaterialIndex(Type)];
    const FPrimitiveInstanceId Id = Batch.AddInstanceById(CellTransform(Key.Cell, Origin, VoxelSize), true);
    DrawnCubes.Add(Key, {Id, Type});
}

void AConstructChunkView::RemoveCube(const FCubeKey& Key)
{
    FDrawnCube Cube;
    if (DrawnCubes.RemoveAndCopyValue(Key, Cube))
    {
        Batches[GetMaterialIndex(Cube.Type)]->RemoveInstanceById(Cube.InstanceId);
    }
}

void AConstructChunkView::StopLiveDraw()
{
    ++DrawGeneration;
    bLiveDraw = false;
    bLiveTruncated = false;
    if (UCrowdyVoxelWorld* Voxels = VoxelWorld.Get())
    {
        Voxels->OnChunkLoaded.RemoveDynamic(this, &ThisClass::ChunkLoaded);
        Voxels->OnChunkUnloaded.RemoveDynamic(this, &ThisClass::ChunkUnloaded);
        Voxels->OnChunkChanged.RemoveDynamic(this, &ThisClass::ChunkChanged);
    }
    VoxelWorld.Reset();
    LiveChunks.Reset();
}

void AConstructChunkView::ClearDrawnCubes()
{
    for (UInstancedStaticMeshComponent* Batch : Batches)
    {
        Batch->ClearInstances();
    }
    DrawnCubes.Reset();
}

void AConstructChunkView::ClearChunks()
{
    StopLiveDraw();
    ClearDrawnCubes();
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
    UInstancedStaticMeshComponent& Batch = *Batches[GetMaterialIndex(Drawn->Type)];
    if (!Batch.IsValidId(Drawn->InstanceId) || !Batch.GetInstanceTransform(Batch.GetInstanceIndexForId(Drawn->InstanceId), Instance, true))
    {
        return false;
    }

    WorldCentre = Instance.GetLocation();
    Type = Drawn->Type;
    return true;
}
