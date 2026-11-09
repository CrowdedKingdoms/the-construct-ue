#include "ConstructChunkPreview.h"

#include "Components/SceneComponent.h"
#include "ConstructChunkDrawing.h"
#include "Containers/BitArray.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    struct FPreviewBatch
    {
        UMaterialInterface* Material = nullptr;
        TArray<FTransform> Transforms;
    };
}

AConstructChunkPreview::AConstructChunkPreview()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ChunkOrigin"));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    CubeMesh = Cube.Object;

    FConstructPreviewCell& Fixture = Cells.AddDefaulted_GetRef();
    Fixture.Cell = FIntVector(1, 2, 3);
    Palette.AddDefaulted();
}

void AConstructChunkPreview::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildPreview();
}

void AConstructChunkPreview::BeginPlay()
{
    Super::BeginPlay();
    RebuildPreview();
}

void AConstructChunkPreview::RebuildPreview()
{
    for (UInstancedStaticMeshComponent* Batch : Batches)
    {
        if (IsValid(Batch))
        {
            Batch->DestroyComponent();
        }
    }
    Batches.Reset();

    if (!CubeMesh || !GetWorld() || !FMath::IsFinite(CellSize) || CellSize < 1.0f || CellSize > 10000.0f)
    {
        return;
    }

    TMap<uint8, FPreviewBatch> ByBlock;
    for (const FConstructPreviewBlockStyle& Style : Palette)
    {
        if (Style.BlockId != 0 && !ByBlock.Contains(Style.BlockId))
        {
            ByBlock.Add(Style.BlockId).Material = Style.Material;
        }
    }

    const FVector Scale(CellSize / ConstructChunkDrawing::EngineCubeSize);
    constexpr int32 CellsPerAxis = ConstructChunkDrawing::CellsPerAxis;
    TBitArray<> Occupied(false, ConstructChunkDrawing::CellsPerChunk);
    for (const FConstructPreviewCell& Entry : Cells)
    {
        const FIntVector& Cell = Entry.Cell;
        FPreviewBatch* Batch = ByBlock.Find(Entry.BlockId);
        if (!Batch || !ConstructChunkDrawing::IsInsideChunk(Cell))
        {
            continue;
        }

        const int32 Index = Cell.X + CellsPerAxis * (Cell.Y + CellsPerAxis * Cell.Z);
        if (Occupied[Index])
        {
            continue;
        }
        Occupied[Index] = true;

        const FVector Centre = ConstructChunkDrawing::GetCellOffset(Cell, CellSize);
        Batch->Transforms.Emplace(FQuat::Identity, Centre, Scale);
    }

    for (const TPair<uint8, FPreviewBatch>& Pair : ByBlock)
    {
        const FPreviewBatch& Batch = Pair.Value;
        if (Batch.Transforms.IsEmpty())
        {
            continue;
        }

        UInstancedStaticMeshComponent* Meshes = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
        Meshes->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        Meshes->SetupAttachment(RootComponent);
        ConstructChunkDrawing::ConfigureBatch(*Meshes, CubeMesh, Batch.Material);
        Meshes->AddInstances(Batch.Transforms, false, false, false);
        Meshes->RegisterComponent();
        Batches.Add(Meshes);
    }
}
