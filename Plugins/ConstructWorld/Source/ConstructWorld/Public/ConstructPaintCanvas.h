#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystem/CrowdyVoxels.h"
#include "ConstructPaintCanvas.generated.h"

class SWidget;
class UCrowdyVoxelWorld;
class UGameViewportClient;
struct FCrowdyVoxelWrite;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FConstructPaintDone, FIntVector, Cell, int32, Type, ECrowdyVoxelResult, Result, const FString&, Message);

/** A floor-sized view of the chunk beneath this actor. The SDK owns voxel state and write settlement. */
UCLASS()
class CONSTRUCTWORLD_API AConstructPaintCanvas : public AActor
{
    GENERATED_BODY()

public:
    AConstructPaintCanvas();

    UFUNCTION(BlueprintCallable, Category = "Construct|Paint")
    void PaintCell(FIntVector Cell, int32 Type);

    UPROPERTY(BlueprintAssignable, Category = "Construct|Paint")
    FConstructPaintDone OnPaintDone;

    FInt64Vector3 GetCanvasChunk() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FConstructPaintTestAccess;
#endif

    bool MakeWrite(FIntVector Cell, int32 Type, FCrowdyVoxelWrite& Write) const;
    void ShowFeedback(const FString& Message);
    void HideFeedback();

    UFUNCTION()
    void ChunkLoaded(int64 ChunkX, int64 ChunkY, int64 ChunkZ);

    UFUNCTION()
    void EditDone(int64 EditId, const FCrowdyVoxelWrite& Write, ECrowdyVoxelResult Result, const FString& Message);

    TWeakObjectPtr<UCrowdyVoxelWorld> VoxelWorld;
    TSet<int64> PendingEdits;
    bool bWarnedAboutStorage = false;

    TWeakObjectPtr<UGameViewportClient> FeedbackViewport;
    TSharedPtr<SWidget> FeedbackWidget;
    FTimerHandle FeedbackTimer;
};
