#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Replication/Subsystems/CrowdyActorTracker.h"
#include "ConstructPresenceSubsystem.generated.h"

class UCrowdyEntitySubsystem;
struct FConstructPresenceSubsystemTestAccess;

DECLARE_LOG_CATEGORY_EXTERN(LogConstructPresence, Log, All);

/** Presentation lifecycle for the SDK's pooled remote presence pawns. */
UCLASS()
class CONSTRUCTPRESENCE_API UConstructPresenceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend struct FConstructPresenceSubsystemTestAccess;

	void BindLifecycleEvents();
	void UnbindLifecycleEvents();
	UFUNCTION()
	void EntityRegistered(const FGuid& UUID);
	UFUNCTION()
	void RemoteAppeared(FGuid UUID, FInstancedStruct InitialState, int32 ActorCount);
	UFUNCTION()
	void RemoteLeft(const FCrowdyActorLeft& ActorLeft, int32 ActorCount);
	UFUNCTION()
	void RemoteTimedOut(FGuid UUID, int32 ActorCount);
	void RecordDeparture(const FGuid& UUID, const TCHAR* Reason);

	TWeakObjectPtr<UCrowdyActorTracker> Tracker;
	TWeakObjectPtr<UCrowdyEntitySubsystem> Entities;
	TSet<FGuid> PendingRemotePlayers;
	TSet<FGuid> RemotePlayers;
};
