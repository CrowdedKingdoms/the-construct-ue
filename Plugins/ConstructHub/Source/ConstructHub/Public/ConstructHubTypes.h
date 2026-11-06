#pragma once

#include "CoreMinimal.h"
#include "ConstructHubTypes.generated.h"

/** A chunk's coordinates in the world grid. */
USTRUCT(BlueprintType)
struct CONSTRUCTHUB_API FConstructHubChunk
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    int64 X = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    int64 Y = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    int64 Z = 0;
};

/** A grid's recorded claim in the hub. */
USTRUCT(BlueprintType)
struct CONSTRUCTHUB_API FConstructHubClaim
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    int64 GridId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    FConstructHubChunk Chunk;

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    int64 OwnerUserId = 0;

    /** Milliseconds since the Unix epoch. */
    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    int64 ClaimedAt = 0;
};

/** One entry in the hub's program catalog. */
USTRUCT(BlueprintType)
struct CONSTRUCTHUB_API FConstructHubProgram
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    int32 ProgramId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    FString SceneId;

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "Construct Hub")
    FString Description;
};
