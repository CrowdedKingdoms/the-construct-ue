#include "ConstructHubSubsystem.h"

#include "ConstructHubStatusWidget.h"
#include "CrowdyServerObject.h"
#include "CrowdyServerObjectDefinition.h"
#include "CrowdyServerObjectSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "StructUtils/PropertyBag.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
    TOptional<int64> ReadReadyValue(const UCrowdyServerObject* Object, FName Field)
    {
        if (!Object || Object->GetStatus() != ECrowdyServerObjectStatus::Ready)
        {
            return {};
        }

        const FInstancedPropertyBag State = Object->GetStateList();
        const auto Value = State.GetValueInt64(Field);
        return Value.HasValue() ? TOptional<int64>(Value.GetValue()) : TOptional<int64>();
    }
}

bool UConstructHubSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UConstructHubSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    UGameInstance* Game = InWorld.GetGameInstance();
    if (bShuttingDown || Instance.IsValid() || &InWorld != GetWorld() || !Game || InWorld.GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    Instance = Game;
    PlayerAddedHandle = Game->OnLocalPlayerAddedEvent.AddUObject(this, &ThisClass::WatchPlayer);
    PlayerRemovedHandle = Game->OnLocalPlayerRemovedEvent.AddUObject(this, &ThisClass::ForgetPlayer);
    if (UCrowdySDKSubsystem* SDK = Game->GetSubsystem<UCrowdySDKSubsystem>())
    {
        SDK->OnLogin.AddUniqueDynamic(this, &ThisClass::SignedIn);
        SDK->OnRegister.AddUniqueDynamic(this, &ThisClass::SignedIn);
        SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::SignedOut);
    }
    if (UCrowdyGameSession* Session = Game->GetSubsystem<UCrowdyGameSession>())
    {
        Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
    }
    const TArray<ULocalPlayer*> InitialPlayers = Game->GetLocalPlayers();
    for (ULocalPlayer* Player : InitialPlayers)
    {
        if (Game->GetLocalPlayers().Contains(Player))
        {
            WatchPlayer(Player);
        }
    }
}

void UConstructHubSubsystem::Deinitialize()
{
    bShuttingDown = true;
    if (UGameInstance* Game = Instance.Get())
    {
        Game->OnLocalPlayerAddedEvent.Remove(PlayerAddedHandle);
        Game->OnLocalPlayerRemovedEvent.Remove(PlayerRemovedHandle);
        if (UCrowdySDKSubsystem* SDK = Game->GetSubsystem<UCrowdySDKSubsystem>())
        {
            SDK->OnLogin.RemoveDynamic(this, &ThisClass::SignedIn);
            SDK->OnRegister.RemoveDynamic(this, &ThisClass::SignedIn);
            SDK->OnLogout.RemoveDynamic(this, &ThisClass::SignedOut);
        }
        if (UCrowdyGameSession* Session = Game->GetSubsystem<UCrowdyGameSession>())
        {
            Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
        }
    }

    for (const auto& Entry : Players)
    {
        if (IsValid(Entry.Key.Get()))
        {
            Entry.Key->OnPlayerControllerChanged().Remove(Entry.Value.ControllerChangedHandle);
        }
        if (Entry.Value.Widget)
        {
            Entry.Value.Widget->RemoveFromParent();
        }
    }
    Players.Reset();
    ReleaseServerObjects();
    Instance.Reset();
    Super::Deinitialize();
}

void UConstructHubSubsystem::WatchPlayer(ULocalPlayer* Player)
{
    if (bShuttingDown || !IsValid(Player))
    {
        return;
    }

    const TWeakObjectPtr<ULocalPlayer> PlayerKey(Player);
    FConstructHubLocalPlayer& Entry = Players.FindOrAdd(Player);
    if (!Entry.ControllerChangedHandle.IsValid())
    {
        Entry.ControllerChangedHandle = Player->OnPlayerControllerChanged().AddUObject(this, &ThisClass::ControllerChanged, PlayerKey);
    }
    ControllerChanged(Player->GetPlayerController(GetWorld()), PlayerKey);
}

void UConstructHubSubsystem::ForgetPlayer(ULocalPlayer* Player)
{
    FConstructHubLocalPlayer Previous;
    if (!Players.RemoveAndCopyValue(Player, Previous))
    {
        return;
    }

    if (IsValid(Player))
    {
        Player->OnPlayerControllerChanged().Remove(Previous.ControllerChangedHandle);
    }
    if (Previous.Widget)
    {
        Previous.Widget->RemoveFromParent();
    }
    RefreshServerObjects();
}

void UConstructHubSubsystem::ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player)
{
    ULocalPlayer* LocalPlayer = Player.Get();
    FConstructHubLocalPlayer* Entry = Players.Find(LocalPlayer);
    if (bShuttingDown || !Entry || !LocalPlayer)
    {
        return;
    }

    const auto IsCurrentLocalController = [this, Controller, Player]()
    {
        const ULocalPlayer* CurrentPlayer = Player.Get();
        return CurrentPlayer && IsValid(Controller) && Controller->GetWorld() == GetWorld() && Controller->IsLocalController()
            && Controller->GetLocalPlayer() == CurrentPlayer && CurrentPlayer->GetPlayerController(GetWorld()) == Controller
            && !Controller->IsActorBeingDestroyed();
    };
    if (Entry->Widget && (!IsCurrentLocalController() || Entry->Controller.Get() != Controller))
    {
        UConstructHubStatusWidget* PreviousWidget = Entry->Widget;
        Entry->Widget = nullptr;
        Entry->Controller.Reset();
        // Removing a viewport widget can notify listeners that change the local player list.
        PreviousWidget->RemoveFromParent();
        Entry = Players.Find(Player.Get());
    }
    if (bShuttingDown || !Entry || !IsCurrentLocalController())
    {
        return;
    }
    if (!Entry->Widget)
    {
        Entry->Widget = CreateWidget<UConstructHubStatusWidget>(Controller);
        Entry->Controller = Controller;
        if (Entry->Widget)
        {
            Entry->Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }
    if (Entry->Widget && !Entry->Widget->IsInViewport() && GetWorld()->GetGameViewport())
    {
        Entry->Widget->AddToPlayerScreen(20);
    }
    RefreshServerObjects();
}

void UConstructHubSubsystem::RefreshServerObjects()
{
    const UGameInstance* Game = Instance.Get();
    const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
    const int64 UserId = Session && !Session->HasAccessEnded() ? Session->GetUserID() : 0;
    if (bShuttingDown || Players.IsEmpty() || UserId <= 0)
    {
        ReleaseServerObjects();
    }
    else
    {
        if (BoundUserId != UserId)
        {
            ReleaseServerObjects();
            BoundUserId = UserId;
        }
        // Loaded here, never in a constructor: a failed lookup at startup leaves an empty package, and the SDK's newtype then
        // refuses to make the asset on a clean project. Config/DefaultGame.ini always cooks the folder.
        if (!WorldDefinition)
        {
            WorldDefinition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructWorld.CSO_ConstructWorld"));
            ProgressDefinition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructProgress.CSO_ConstructProgress"));
        }
        FollowObject(WorldWatch, WorldDefinition, TEXT("main"));
        FollowObject(ProgressWatch, ProgressDefinition, LexToString(UserId));
    }
    RefreshWidgets();
}

void UConstructHubSubsystem::FollowObject(FWatchedObject& Watch, UCrowdyServerObjectDefinition* Definition, const FString& InstanceId)
{
    UGameInstance* Game = Instance.Get();
    UCrowdyServerObjectSubsystem* Objects = Game ? Game->GetSubsystem<UCrowdyServerObjectSubsystem>() : nullptr;
    if (!Objects || !Definition || Watch.Object.IsValid())
    {
        return;
    }

    FString Error;
    if (UCrowdyServerObject* Object = Objects->Acquire(Definition, InstanceId, this, Error))
    {
        Watch.Object = Object;
        Watch.StatusHandle = Object->OnStatusChanged.AddUObject(this, &ThisClass::ObjectStatusChanged);
        Watch.ValuesHandle = Object->WatchValues(FOnCrowdyServerValuesChanged::FDelegate::CreateUObject(this, &ThisClass::ObjectValuesChanged));
    }
}

void UConstructHubSubsystem::ReleaseServerObjects()
{
    BoundUserId = 0;
    ReleaseObject(WorldWatch);
    ReleaseObject(ProgressWatch);
}

void UConstructHubSubsystem::ReleaseObject(FWatchedObject& Watch)
{
    if (UCrowdyServerObject* Object = Watch.Object.Get())
    {
        Object->OnStatusChanged.Remove(Watch.StatusHandle);
        Object->UnwatchValues(Watch.ValuesHandle);
        if (UGameInstance* Game = Instance.Get())
        {
            if (UCrowdyServerObjectSubsystem* Objects = Game->GetSubsystem<UCrowdyServerObjectSubsystem>())
            {
                Objects->Release(Object, this);
            }
        }
    }
    Watch = {};
}

void UConstructHubSubsystem::RefreshWidgets()
{
    const TOptional<int64> Pulses = ReadReadyValue(WorldWatch.Object.Get(), TEXT("Pulses"));
    const TOptional<int64> Level = ReadReadyValue(ProgressWatch.Object.Get(), TEXT("Level"));
    for (const auto& Entry : Players)
    {
        if (Entry.Value.Widget)
        {
            Entry.Value.Widget->SetHubStatus(Pulses, Level);
        }
    }
}

void UConstructHubSubsystem::ObjectStatusChanged(UCrowdyServerObject*, ECrowdyServerObjectStatus)
{
    RefreshWidgets();
}

void UConstructHubSubsystem::ObjectValuesChanged(UCrowdyServerObject*, TConstArrayView<FString>)
{
    RefreshWidgets();
}

void UConstructHubSubsystem::SignedIn(bool bSuccess, FString)
{
    if (bSuccess)
    {
        RefreshServerObjects();
    }
}

void UConstructHubSubsystem::SignedOut(bool bSuccess)
{
    if (bSuccess)
    {
        ReleaseServerObjects();
        RefreshWidgets();
    }
}

void UConstructHubSubsystem::AccessEnded(const FString&, const FString&)
{
    ReleaseServerObjects();
    RefreshWidgets();
}
