#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConstructLiveViewTestReceiver.generated.h"

UCLASS(Transient, NotBlueprintable)
class UConstructLiveViewTestReceiver : public UObject
{
    GENERATED_BODY()

public:
    TFunction<void(int64, int64, int64, const TArray<FIntVector>&)> Handle;

    UFUNCTION()
    void Redrawn(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions)
    {
        Handle(ChunkX, ChunkY, ChunkZ, Positions);
    }
};
