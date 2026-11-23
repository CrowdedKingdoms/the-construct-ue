#include "ConstructVoiceSubsystem.h"

#include "ConstructDeveloperSettings.h"
#include "ConstructVoiceComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UConstructVoiceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UConstructVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bEnabled = GetDefault<UConstructDeveloperSettings>()->bEnableVoice;
}

void UConstructVoiceSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!bEnabled || ObservedInstance.IsValid() || &InWorld != GetWorld())
	{
		return;
	}

	UGameInstance* Instance = InWorld.GetGameInstance();
	if (!Instance)
	{
		return;
	}

	ObservedInstance = Instance;
	LocalPlayerAddedHandle = Instance->OnLocalPlayerAddedEvent.AddUObject(this, &ThisClass::LocalPlayerAdded);
	LocalPlayerRemovedHandle = Instance->OnLocalPlayerRemovedEvent.AddUObject(this, &ThisClass::LocalPlayerRemoved);
	for (ULocalPlayer* Player : Instance->GetLocalPlayers())
	{
		LocalPlayerAdded(Player);
	}
}

void UConstructVoiceSubsystem::LocalPlayerAdded(ULocalPlayer* Player)
{
	UGameInstance* Instance = ObservedInstance.Get();
	if (!bEnabled || !IsValid(Player) || !Instance || !Instance->GetLocalPlayers().Contains(Player))
	{
		return;
	}

	const TWeakObjectPtr<ULocalPlayer> PlayerKey(Player);
	if (!ControllerChangedHandles.Contains(PlayerKey))
	{
		const FDelegateHandle Handle =
		    Player->OnPlayerControllerChanged().AddUObject(this, &ThisClass::PlayerControllerChanged, PlayerKey);
		ControllerChangedHandles.Add(PlayerKey, Handle);
	}
	InstallForController(Player->GetPlayerController(GetWorld()), Player);
}

void UConstructVoiceSubsystem::LocalPlayerRemoved(ULocalPlayer* Player)
{
	if (!Player)
	{
		return;
	}
	const TWeakObjectPtr<ULocalPlayer> PlayerKey(Player);
	const TWeakObjectPtr<UConstructVoiceComponent> Previous = ActiveComponents.FindRef(PlayerKey);
	ActiveComponents.Remove(PlayerKey);
	if (const FDelegateHandle* Handle = ControllerChangedHandles.Find(PlayerKey))
	{
		Player->OnPlayerControllerChanged().Remove(*Handle);
		ControllerChangedHandles.Remove(PlayerKey);
	}
	if (UConstructVoiceComponent* Component = Previous.Get())
	{
		Component->ShutdownLocalRuntime();
	}
}

void UConstructVoiceSubsystem::PlayerControllerChanged(APlayerController* Controller,
                                                       TWeakObjectPtr<ULocalPlayer> Player)
{
	if (ControllerChangedHandles.Contains(Player))
	{
		InstallForController(Controller, Player.Get());
	}
}

void UConstructVoiceSubsystem::InstallForController(APlayerController* Controller, ULocalPlayer* Player)
{
	const TWeakObjectPtr<ULocalPlayer> PlayerKey(Player);
	const TWeakObjectPtr<APlayerController> ControllerKey(Controller);
	const auto IsCurrentLocalController = [this, ControllerKey, PlayerKey]()
	{
		APlayerController* CurrentController = ControllerKey.Get();
		ULocalPlayer* CurrentPlayer = PlayerKey.Get();
		const UGameInstance* Instance = ObservedInstance.Get();
		return bEnabled && CurrentController && CurrentPlayer && Instance &&
		       Instance->GetLocalPlayers().Contains(CurrentPlayer) && ControllerChangedHandles.Contains(PlayerKey) &&
		       CurrentController->GetWorld() == GetWorld() && CurrentController->IsLocalController() &&
		       CurrentController->GetLocalPlayer() == CurrentPlayer &&
		       CurrentPlayer->GetPlayerController(GetWorld()) == CurrentController &&
		       !CurrentController->IsActorBeingDestroyed();
	};

	if (UConstructVoiceComponent* Previous = ActiveComponents.FindRef(PlayerKey).Get();
	    Previous && (!IsCurrentLocalController() || Previous->GetOwner() != Controller))
	{
		ActiveComponents.Remove(PlayerKey);
		Previous->ShutdownLocalRuntime();
	}
	// Releasing a component can invoke game delegates that replace or remove this player.
	if (!IsCurrentLocalController())
	{
		return;
	}

	UConstructVoiceComponent* Voice = Controller->FindComponentByClass<UConstructVoiceComponent>();
	const bool bCreated = Voice == nullptr;
	if (bCreated)
	{
		Voice = NewObject<UConstructVoiceComponent>(Controller);
	}
	// Activation can synchronously replace the controller. Nested cleanup must already own this component.
	ActiveComponents.Add(PlayerKey, Voice);
	const TWeakObjectPtr<UConstructVoiceComponent> VoiceKey(Voice);
	const auto StillOwnsComponent = [this, PlayerKey, VoiceKey, &IsCurrentLocalController]()
	{
		return IsCurrentLocalController() && VoiceKey.IsValid() && ActiveComponents.FindRef(PlayerKey) == VoiceKey;
	};
	if (bCreated)
	{
		Controller->AddInstanceComponent(Voice);
		if (!StillOwnsComponent())
		{
			return;
		}
		Voice->RegisterComponent();
	}
	if (!StillOwnsComponent())
	{
		return;
	}
	Voice->InitializeLocalRuntime();
}

void UConstructVoiceSubsystem::Deinitialize()
{
	bEnabled = false;
	TMap<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<UConstructVoiceComponent>> PreviousComponents =
	    MoveTemp(ActiveComponents);
	ActiveComponents.Reset();
	if (UGameInstance* Instance = ObservedInstance.Get())
	{
		Instance->OnLocalPlayerAddedEvent.Remove(LocalPlayerAddedHandle);
		Instance->OnLocalPlayerRemovedEvent.Remove(LocalPlayerRemovedHandle);
	}
	for (const auto& Entry : ControllerChangedHandles)
	{
		if (ULocalPlayer* Player = Entry.Key.Get())
		{
			Player->OnPlayerControllerChanged().Remove(Entry.Value);
		}
	}
	ControllerChangedHandles.Reset();
	ObservedInstance.Reset();
	LocalPlayerAddedHandle.Reset();
	LocalPlayerRemovedHandle.Reset();
	for (const auto& Entry : PreviousComponents)
	{
		if (UConstructVoiceComponent* Component = Entry.Value.Get())
		{
			Component->ShutdownLocalRuntime();
		}
	}
	Super::Deinitialize();
}
