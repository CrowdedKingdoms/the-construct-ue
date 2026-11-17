#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructStudioObservation.generated.h"

class FJsonObject;

/** The claimed grid the player stands in, as its id and chunk box. */
struct FConstructStudioObservedGrid
{
	int64 GridId = 0;
	FInt64Vector Low = FInt64Vector(0, 0, 0);
	FInt64Vector High = FInt64Vector(0, 0, 0);
	bool bOwned = false;
};

/** Another player the observation may list, in world units, with the chunk it stands in. */
struct FConstructStudioObservedActor
{
	FString ActorId;
	FString Label;
	FVector Location = FVector::ZeroVector;
	FInt64Vector Chunk = FInt64Vector(0, 0, 0);
};

/** Everything one observation is built from, in world units and degrees. */
struct FConstructStudioObservationInput
{
	FString ObservationId;
	FString ControlledEntityId;
	FDateTime Now;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FRotator Look = FRotator::ZeroRotator;
	/** One cell's edge in world units; positions are reported in cells. */
	double VoxelSize = 100.0;
	TOptional<FConstructStudioObservedGrid> Grid;
	/** Other players in any order; only those inside Grid are listed. */
	TArray<FConstructStudioObservedActor> Others;
	bool bModalOpen = false;
	bool bTextInputFocused = false;
};

/** What the Studio agent observes of the local player's game, in the platform's crowdy.game-observation/1 shape. */
UCLASS()
class CONSTRUCTSTUDIODOCKCORE_API UConstructStudioObservation : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The most other players one observation lists. */
	static constexpr int32 MaxNearbyActors = 16;

	/** Seconds an observation stays current. */
	static constexpr double LifeSeconds = 1.5;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** The chunk the local player's pawn stands in; unset with no pawn. */
	TOptional<FInt64Vector> GetPlayerChunk() const;

	/** The local player's observation; Grid is the claimed grid the player stands in, if any. Null with no pawn. */
	TSharedPtr<FJsonObject> Observe(const TOptional<FConstructStudioObservedGrid>& Grid) const;

	/** The observation for In: every number a decimal string, positions in cells, nearby players inside the grid nearest first. */
	static TSharedRef<FJsonObject> Build(const FConstructStudioObservationInput& In);

	/** A number as the platform's decimal string: at most three places, no exponent, no trailing zeros. */
	static FString Decimal(double Value);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	bool bHasGame = false;
};
