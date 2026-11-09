#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructChunkPreview.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

USTRUCT(BlueprintType)
struct FConstructPreviewCell
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Preview")
    FIntVector Cell = FIntVector(0, 0, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Preview")
    uint8 BlockId = 1;
};

USTRUCT(BlueprintType)
struct FConstructPreviewBlockStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Preview")
    uint8 BlockId = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Preview", meta = (ToolTip = "Enable and save Used with Instanced Static Meshes on the parent material."))
    TObjectPtr<UMaterialInterface> Material = nullptr;
};

/** Draws an editable local fixture with the actor origin at the chunk's minimum corner. */
UCLASS()
class CONSTRUCTWORLD_API AConstructChunkPreview : public AActor
{
    GENERATED_BODY()

public:
    AConstructChunkPreview();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk Preview", meta = (ClampMin = "1.0", ClampMax = "10000.0", Units = "cm"))
    float CellSize = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk Preview")
    TArray<FConstructPreviewCell> Cells;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunk Preview")
    TArray<FConstructPreviewBlockStyle> Palette;

    virtual void OnConstruction(const FTransform& Transform) override;

protected:
    virtual void BeginPlay() override;

private:
    void RebuildPreview();

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;

    UPROPERTY(Transient, DuplicateTransient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
};
