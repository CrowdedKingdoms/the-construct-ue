#include "ConstructChatSubsystem.h"

#include "ConstructChatComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UConstructChatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UConstructChatSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (bShuttingDown || Instance.IsValid() || &InWorld != GetWorld())
	{
		return;
	}
	UGameInstance* Game = InWorld.GetGameInstance();
	if (!Game)
	{
		return;
	}
	Instance = Game;
	PlayerAddedHandle = Game->OnLocalPlayerAddedEvent.AddUObject(this, &ThisClass::WatchPlayer);
	PlayerRemovedHandle = Game->OnLocalPlayerRemovedEvent.AddUObject(this, &ThisClass::ForgetPlayer);
	for (ULocalPlayer* Player : Game->GetLocalPlayers())
	{
		WatchPlayer(Player);
	}
}

void UConstructChatSubsystem::WatchPlayer(ULocalPlayer* Player)
{
	if (bShuttingDown || !IsValid(Player))
	{
		return;
	}

	const TWeakObjectPtr<ULocalPlayer> PlayerKey(Player);
	if (!PlayerObservations.Contains(PlayerKey))
	{
		FPlayerObservation Observation;
		Observation.ControllerChangedHandle =
		    Player->OnPlayerControllerChanged().AddUObject(this, &ThisClass::ControllerChanged, PlayerKey);
		PlayerObservations.Add(PlayerKey, Observation);
	}
	ControllerChanged(Player->GetPlayerController(GetWorld()), PlayerKey);
}

void UConstructChatSubsystem::ForgetPlayer(ULocalPlayer* Player)
{
	const TWeakObjectPtr<ULocalPlayer> PlayerKey(Player);
	FPlayerObservation Previous;
	if (PlayerObservations.RemoveAndCopyValue(PlayerKey, Previous))
	{
		if (IsValid(Player))
		{
			Player->OnPlayerControllerChanged().Remove(Previous.ControllerChangedHandle);
		}
		if (UConstructChatComponent* Component = Previous.Component.Get())
		{
			Component->ShutdownLocalRuntime();
		}
	}
}

void UConstructChatSubsystem::ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player)
{
	FPlayerObservation* Observation = PlayerObservations.Find(Player);
	if (bShuttingDown || !Observation)
	{
		return;
	}

	const auto IsCurrentLocalController = [this, Controller, Player]()
	{
		return IsValid(Controller) && Controller->GetWorld() == GetWorld() && Controller->IsLocalController() &&
		       Player.IsValid() && Controller->GetLocalPlayer() == Player.Get() &&
		       Player->GetPlayerController(GetWorld()) == Controller && !Controller->IsActorBeingDestroyed();
	};
	const bool bValidController = IsCurrentLocalController();
	UConstructChatComponent* Previous = Observation->Component.Get();
	if (Previous && (!bValidController || Previous->GetOwner() != Controller))
	{
		Observation->Component.Reset();
		Previous->ShutdownLocalRuntime();
	}
	// Releasing an input modal can invoke game delegates, which may remove the player or replace its controller.
	Observation = PlayerObservations.Find(Player);
	if (bShuttingDown || !Observation || !IsCurrentLocalController())
	{
		return;
	}

	UConstructChatComponent* Component = Controller->FindComponentByClass<UConstructChatComponent>();
	if (Component)
	{
		Observation->Component = Component;
		Component->InitializeLocalRuntime();
		return;
	}
	Component = NewObject<UConstructChatComponent>(Controller);
	Observation->Component = Component;
	Controller->AddInstanceComponent(Component);
	Component->RegisterComponent();
}

void UConstructChatSubsystem::Deinitialize()
{
	bShuttingDown = true;
	if (UGameInstance* Game = Instance.Get())
	{
		Game->OnLocalPlayerAddedEvent.Remove(PlayerAddedHandle);
		Game->OnLocalPlayerRemovedEvent.Remove(PlayerRemovedHandle);
	}
	Instance.Reset();
	PlayerAddedHandle.Reset();
	PlayerRemovedHandle.Reset();
	TMap<TWeakObjectPtr<ULocalPlayer>, FPlayerObservation> PreviousPlayers = MoveTemp(PlayerObservations);
	PlayerObservations.Reset();
	for (const TPair<TWeakObjectPtr<ULocalPlayer>, FPlayerObservation>& Entry : PreviousPlayers)
	{
		if (ULocalPlayer* Player = Entry.Key.Get())
		{
			Player->OnPlayerControllerChanged().Remove(Entry.Value.ControllerChangedHandle);
		}
		if (UConstructChatComponent* Component = Entry.Value.Component.Get())
		{
			Component->ShutdownLocalRuntime();
		}
	}
	Super::Deinitialize();
}
