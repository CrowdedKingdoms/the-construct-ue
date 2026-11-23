#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructModChunkAnchor.generated.h"

/** Owns the menu's loading interest without storing any voxel values. */
UCLASS()
class CONSTRUCTWORLD_API AConstructModChunkAnchor : public AActor
{
    GENERATED_BODY()

public:
    AConstructModChunkAnchor();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};