#include "ConstructWebcamUIWorld.h"

#include "ConstructPresencePawn.h"
#include "ConstructWebcamPanel.h"
#include "ConstructWebcamSubsystem.h"
#include "ConstructWebcamFeeds.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "TimerManager.h"

bool UConstructWebcamUIWorld::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UConstructWebcamUIWorld::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    UGameInstance* Game = World.GetGameInstance();
    if (!Game || World.GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    AddedHandle = Game->OnLocalPlayerAddedEvent.AddUObject(this, &ThisClass::AddPlayer);
    RemovedHandle = Game->OnLocalPlayerRemovedEvent.AddUObject(this, &ThisClass::RemovePlayer);
    for (ULocalPlayer* Player : Game->GetLocalPlayers())
    {
        AddPlayer(Player);
    }
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
    World.GetTimerManager().SetTimer(RefreshTimer, this, &ThisClass::RefreshPlayers, 0.25f, true);
    RefreshPlayers();
}

void UConstructWebcamUIWorld::AddPlayer(ULocalPlayer* Player)
{
    if (!Player || Players.ContainsByPredicate([Player](const FConstructWebcamPlayerUI& Entry) { return Entry.Player == Player; }))
    {
        return;
    }
    FConstructWebcamPlayerUI Entry;
    Entry.Player = Player;
    Entry.ControllerHandle = Player->OnPlayerControllerChanged().AddUObject(this, &ThisClass::ControllerChanged, TWeakObjectPtr<ULocalPlayer>(Player));
    Players.Add(Entry);
    ControllerChanged(Player->GetPlayerController(GetWorld()), Player);
}

void UConstructWebcamUIWorld::RemovePlayer(ULocalPlayer* Player)
{
    const int32 Index = Players.IndexOfByPredicate([Player](const FConstructWebcamPlayerUI& Entry) { return Entry.Player == Player; });
    if (Index == INDEX_NONE)
    {
        return;
    }
    FConstructWebcamPlayerUI Entry = Players[Index];
    Players.RemoveAt(Index);
    if (Entry.Panel)
    {
        Entry.Panel->Shutdown();
        Entry.Panel->RemoveFromParent();
    }
    if (Entry.Controller.IsValid())
    {
        Entry.Controller->OnEndPlay.RemoveDynamic(this, &ThisClass::ControllerEnded);
    }
    if (Player)
    {
        Player->OnPlayerControllerChanged().Remove(Entry.ControllerHandle);
    }
}

void UConstructWebcamUIWorld::ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player)
{
    FConstructWebcamPlayerUI* Entry = Players.FindByPredicate([Player](const FConstructWebcamPlayerUI& Item) { return Item.Player == Player; });
    if (!Entry)
    {
        return;
    }
    if (Entry->Panel)
    {
        Entry->Panel->Shutdown();
        Entry->Panel->RemoveFromParent();
        Entry->Panel = nullptr;
    }
    if (Entry->Controller.IsValid())
    {
        Entry->Controller->OnEndPlay.RemoveDynamic(this, &ThisClass::ControllerEnded);
    }
    Entry->Controller = IsValid(Controller) && !Controller->IsActorBeingDestroyed() ? Controller : nullptr;
    if (Entry->Controller.IsValid())
    {
        Entry->Controller->OnEndPlay.AddUniqueDynamic(this, &ThisClass::ControllerEnded);
    }
}

void UConstructWebcamUIWorld::RefreshPlayers()
{
    UGameInstance* Game = GetWorld()->GetGameInstance();
    UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
    UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
    UConstructWebcamSubsystem* Camera = Game ? Game->GetSubsystem<UConstructWebcamSubsystem>() : nullptr;
    UConstructWebcamFeeds* Feeds = GetWorld()->GetSubsystem<UConstructWebcamFeeds>();
    const bool bReady = Camera && Feeds && Session && Session->GetUserID() > 0 && !Session->HasAccessEnded() && Auth && Auth->IsSignedIn();
    for (FConstructWebcamPlayerUI& Entry : Players)
    {
        APlayerController* Controller = Entry.Player.IsValid() ? Entry.Player->GetPlayerController(GetWorld()) : nullptr;
        if (Entry.Controller != Controller)
        {
            ControllerChanged(Controller, Entry.Player);
        }
        const bool bPresence = IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalController() && Cast<AConstructPresencePawn>(Controller->GetPawn());
        if (!bReady || !bPresence)
        {
            if (Entry.Panel)
            {
                Entry.Panel->Shutdown();
                Entry.Panel->RemoveFromParent();
                Entry.Panel = nullptr;
            }
            continue;
        }
        if (!Entry.Panel)
        {
            Entry.Panel = CreateWidget<UConstructWebcamPanel>(Controller);
            if (Entry.Panel)
            {
                Entry.Panel->BindSources(Camera, Feeds);
                Entry.Panel->AddToPlayerScreen(35);
            }
        }
    }
}

void UConstructWebcamUIWorld::CloseAll()
{
    for (FConstructWebcamPlayerUI& Entry : Players)
    {
        if (Entry.Panel)
        {
            Entry.Panel->Shutdown();
            Entry.Panel->RemoveFromParent();
            Entry.Panel = nullptr;
        }
    }
}

void UConstructWebcamUIWorld::ControllerEnded(AActor* Actor, EEndPlayReason::Type)
{
    for (FConstructWebcamPlayerUI& Entry : Players)
    {
        if (Entry.Controller == Actor)
        {
            ControllerChanged(nullptr, Entry.Player);
        }
    }
}

void UConstructWebcamUIWorld::SignedOut(bool bSuccess)
{
    if (bSuccess)
    {
        CloseAll();
    }
}

void UConstructWebcamUIWorld::SignedIn(bool bSuccess, FString)
{
    if (bSuccess)
    {
        CloseAll();
        RefreshPlayers();
    }
}

void UConstructWebcamUIWorld::AccessEnded(const FString&, const FString&)
{
    CloseAll();
}

void UConstructWebcamUIWorld::Deinitialize()
{
    GetWorld()->GetTimerManager().ClearTimer(RefreshTimer);
    if (UGameInstance* Game = GetWorld()->GetGameInstance())
    {
        Game->OnLocalPlayerAddedEvent.Remove(AddedHandle);
        Game->OnLocalPlayerRemovedEvent.Remove(RemovedHandle);
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
    while (!Players.IsEmpty())
    {
        RemovePlayer(Players.Last().Player.Get());
    }
    Super::Deinitialize();
}
