#pragma once

#include "Components/InstancedStaticMeshComponent.h"

namespace ConstructChunkDrawing
{
    inline constexpr int32 CellsPerAxis = 16;
    inline constexpr int32 CellsPerChunk = CellsPerAxis * CellsPerAxis * CellsPerAxis;
    inline constexpr double EngineCubeSize = 100.0;

    inline bool IsInsideChunk(const FIntVector& Cell)
    {
        return Cell.X >= 0 && Cell.X < CellsPerAxis
            && Cell.Y >= 0 && Cell.Y < CellsPerAxis
            && Cell.Z >= 0 && Cell.Z < CellsPerAxis;
    }

    inline FVector GetCellOffset(const FIntVector& Cell, double CellSize)
    {
        return (FVector(Cell) + FVector(0.5)) * CellSize;
    }

    inline void ConfigureBatch(UInstancedStaticMeshComponent& Batch, UStaticMesh* Cube, UMaterialInterface* Material)
    {
        Batch.SetStaticMesh(Cube);
        if (Material)
        {
            Batch.SetMaterial(0, Material);
        }
        Batch.bDisallowNanite = true;
        Batch.SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch.SetGenerateOverlapEvents(false);
        Batch.SetCanEverAffectNavigation(false);
    }
}
