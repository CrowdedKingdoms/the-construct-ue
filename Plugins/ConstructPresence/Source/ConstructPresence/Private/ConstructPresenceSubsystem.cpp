#include "ConstructPresenceSubsystem.h"

#include "ConstructPresencePawn.h"
#include "Engine/World.h"
#include "Replication/Subsystems/CrowdyEntitySubsystem.h"
#include "Utils/CrowdyUtilities.h"

DEFINE_LOG_CATEGORY(LogConstructPresence);

bool UConstructPresenceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UConstructPresenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCrowdyActorTracker>();
	Collection.InitializeDependency<UCrowdyEntitySubsystem>();
	Tracker = GetWorld()->GetSubsystem<UCrowdyActorTracker>();
	Entities = GetWorld()->GetSubsystem<UCrowdyEntitySubsystem>();
}

void UConstructPresenceSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	BindLifecycleEvents();
}

void UConstructPresenceSubsystem::BindLifecycleEvents()
{
	if (UCrowdyActorTracker* ActiveTracker = Tracker.Get())
	{
		ActiveTracker->ToggleOwnerTracking(false);
		ActiveTracker->OnRemoteEntityAppeared.AddUniqueDynamic(this, &ThisClass::RemoteAppeared);
		ActiveTracker->OnRemoteEntityLeft.AddUniqueDynamic(this, &ThisClass::RemoteLeft);
		ActiveTracker->OnRemoteEntityLeftAnnounced.AddUniqueDynamic(this, &ThisClass::RemoteLeft);
		ActiveTracker->OnRemoteEntityTimedOut.AddUniqueDynamic(this, &ThisClass::RemoteTimedOut);
	}
	if (UCrowdyEntitySubsystem* Registry = Entities.Get())
	{
		Registry->OnEntityRegistered.AddUniqueDynamic(this, &ThisClass::EntityRegistered);
	}
}

void UConstructPresenceSubsystem::UnbindLifecycleEvents()
{
	if (UCrowdyActorTracker* ActiveTracker = Tracker.Get())
	{
		ActiveTracker->OnRemoteEntityAppeared.RemoveDynamic(this, &ThisClass::RemoteAppeared);
		ActiveTracker->OnRemoteEntityLeft.RemoveDynamic(this, &ThisClass::RemoteLeft);
		ActiveTracker->OnRemoteEntityLeftAnnounced.RemoveDynamic(this, &ThisClass::RemoteLeft);
		ActiveTracker->OnRemoteEntityTimedOut.RemoveDynamic(this, &ThisClass::RemoteTimedOut);
	}
	if (UCrowdyEntitySubsystem* Registry = Entities.Get())
	{
		Registry->OnEntityRegistered.RemoveDynamic(this, &ThisClass::EntityRegistered);
	}
}

void UConstructPresenceSubsystem::Deinitialize()
{
	UnbindLifecycleEvents();
	PendingRemotePlayers.Reset();
	RemotePlayers.Reset();
	Entities.Reset();
	Tracker.Reset();
	Super::Deinitialize();
}

void UConstructPresenceSubsystem::EntityRegistered(const FGuid& UUID)
{
	if (PendingRemotePlayers.Remove(UUID) > 0)
	{
		RemoteAppeared(UUID, FInstancedStruct(), 0);
	}
}

void UConstructPresenceSubsystem::RemoteAppeared(FGuid UUID, FInstancedStruct InitialState, int32 ActorCount)
{
	if (!UUID.IsValid() || RemotePlayers.Contains(UUID) || PendingRemotePlayers.Contains(UUID))
	{
		return;
	}
	UCrowdyEntitySubsystem* Registry = Entities.Get();
	if (!Registry)
	{
		return;
	}
	AConstructPresencePawn* Pawn = Cast<AConstructPresencePawn>(Registry->FindEntity(UUID));
	if (!Pawn)
	{
		PendingRemotePlayers.Add(UUID);
		return;
	}
	// Registration precedes the pool assigning the component's new identity.
	if (!Pawn->Entity || Pawn->Entity->IsLocallyOwned() ||
	    UCrowdyUtilities::GetCrowdyEntityRole(this, Pawn) != ECrowdyRole::RemoteProxy)
	{
		return;
	}
	Pawn->ResetRemoteName();
	RemotePlayers.Add(UUID);
	UE_LOG(LogConstructPresence, Log, TEXT("ConstructPresence: remote player appeared"));
}

void UConstructPresenceSubsystem::RecordDeparture(const FGuid& UUID, const TCHAR* Reason)
{
	PendingRemotePlayers.Remove(UUID);
	if (RemotePlayers.Remove(UUID) != 0)
	{
		UE_LOG(LogConstructPresence, Log, TEXT("ConstructPresence: remote player left: %s"), Reason);
	}
}

void UConstructPresenceSubsystem::RemoteLeft(const FCrowdyActorLeft& ActorLeft, int32 ActorCount)
{
	const TCHAR* Reason =
	    ActorLeft.Reason == ECrowdyActorLeftReason::SessionReleased ? TEXT("session released") : TEXT("stale");
	RecordDeparture(ActorLeft.UUID, Reason);
}

void UConstructPresenceSubsystem::RemoteTimedOut(FGuid UUID, int32 ActorCount)
{
	RecordDeparture(UUID, TEXT("timed out"));
}
