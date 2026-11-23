#include "ConstructModChunkAnchor.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Subsystem/CrowdyVoxelWorld.h"

AConstructModChunkAnchor::AConstructModChunkAnchor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ChunkAnchor")));
}

void AConstructModChunkAnchor::BeginPlay()
{
    Super::BeginPlay();
    if (UCrowdyVoxelWorld* Voxels = GetWorld()->GetSubsystem<UCrowdyVoxelWorld>())
    {
        Voxels->Follow(this, 1);
    }
}

void AConstructModChunkAnchor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* World = GetWorld())
    {
        if (UCrowdyVoxelWorld* Voxels = World->GetSubsystem<UCrowdyVoxelWorld>())
        {
            Voxels->StopFollowing(this);
        }
    }
    Super::EndPlay(Reason);
}