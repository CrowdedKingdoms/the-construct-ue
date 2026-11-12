#include "ConstructModsSubsystem.h"

#include "ConstructModMenu.h"
#include "ConstructClaimPad.h"
#include "ConstructClaimsSubsystem.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

bool UConstructModsSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UConstructModsSubsystem::OnWorldBeginPlay(UWorld& World)
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
}

void UConstructModsSubsystem::AddPlayer(ULocalPlayer* Player)
{
    if (!Player || Players.Contains(Player))
    {
        return;
    }
    FConstructModsLocalUI& Entry = Players.Add(Player);
    const TWeakObjectPtr<ULocalPlayer> WeakPlayer(Player);
    Entry.ControllerHandle = Player->OnPlayerControllerChanged().AddUObject(this, &ThisClass::ControllerChanged, WeakPlayer);
    if (UConstructUIInputSubsystem* Input = Player->GetSubsystem<UConstructUIInputSubsystem>())
    {
        Entry.OpenHandle = Input->OnModsOpenRequested.AddUObject(this, &ThisClass::OpenRequested, WeakPlayer);
    }
    ControllerChanged(Player->GetPlayerController(GetWorld()), WeakPlayer);
}

void UConstructModsSubsystem::RemovePlayer(ULocalPlayer* Player)
{
    if (FConstructModsLocalUI* Entry = Players.Find(Player))
    {
        if (Entry->Menu)
        {
            Entry->Menu->Close();
        }
        if (APlayerController* Controller = Entry->Controller.Get())
        {
            Controller->OnEndPlay.RemoveDynamic(this, &ThisClass::ControllerEnded);
        }
        Player->OnPlayerControllerChanged().Remove(Entry->ControllerHandle);
        if (UConstructUIInputSubsystem* Input = Player->GetSubsystem<UConstructUIInputSubsystem>())
        {
            Input->OnModsOpenRequested.Remove(Entry->OpenHandle);
        }
        Players.Remove(Player);
    }
}

void UConstructModsSubsystem::ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player)
{
    if (FConstructModsLocalUI* Entry = Players.Find(Player.Get()))
    {
        if (Entry->Menu)
        {
            Entry->Menu->Close();
        }
        Entry->Menu = nullptr;
        if (APlayerController* Previous = Entry->Controller.Get())
        {
            Previous->OnEndPlay.RemoveDynamic(this, &ThisClass::ControllerEnded);
        }
        Entry->Controller = IsValid(Controller) && !Controller->IsActorBeingDestroyed() ? Controller : nullptr;
        if (Entry->Controller.IsValid())
        {
            Controller->OnEndPlay.AddUniqueDynamic(this, &ThisClass::ControllerEnded);
        }
    }
}

UConstructModMenu* UConstructModsSubsystem::GetMenu(ULocalPlayer* Player)
{
    FConstructModsLocalUI* Entry = Players.Find(Player);
    APlayerController* Controller = Player ? Player->GetPlayerController(GetWorld()) : nullptr;
    if (!Entry || !IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController() || Controller->GetWorld() != GetWorld())
    {
        return nullptr;
    }
    if (Entry->Controller.Get() != Controller)
    {
        ControllerChanged(Controller, Player);
        Entry = Players.Find(Player);
    }
    if (!Entry->Menu)
    {
        Entry->Menu = CreateWidget<UConstructModMenu>(Controller);
    }
    return Entry->Menu;
}

void UConstructModsSubsystem::OpenRequested(TWeakObjectPtr<ULocalPlayer> Player)
{
    if (UConstructModMenu* Menu = GetMenu(Player.Get()))
    {
        Menu->Open();
    }
}

UConstructModPanel* UConstructModsSubsystem::OpenForPad(AConstructClaimPad& Pad)
{
    UGameInstance* Game = GetWorld()->GetGameInstance();
    APlayerController* Interactor = Pad.OfferController.Get();
    ULocalPlayer* Player = Interactor ? Interactor->GetLocalPlayer() : Game ? Game->GetFirstGamePlayer() : nullptr;
    UConstructModMenu* Menu = GetMenu(Player);
    return Menu ? Menu->OpenForPad(Pad) : nullptr;
}

void UConstructModsSubsystem::CloseAll()
{
    for (auto& Pair : Players)
    {
        if (Pair.Value.Menu)
        {
            Pair.Value.Menu->Close();
        }
    }
}

void UConstructModsSubsystem::SignedIn(bool bSuccess, FString)
{
    if (bSuccess)
    {
        CloseAll();
    }
}

void UConstructModsSubsystem::SignedOut(bool bSuccess)
{
    if (bSuccess)
    {
        CloseAll();
    }
}

void UConstructModsSubsystem::AccessEnded(const FString&, const FString&)
{
    CloseAll();
}

void UConstructModsSubsystem::Deinitialize()
{
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
    TArray<TObjectPtr<ULocalPlayer>> Keys;
    Players.GetKeys(Keys);
    for (ULocalPlayer* Player : Keys)
    {
        RemovePlayer(Player);
    }
    Super::Deinitialize();
}
void UConstructModsSubsystem::ControllerEnded(AActor* Actor, EEndPlayReason::Type)
{
    TArray<TWeakObjectPtr<ULocalPlayer>> EndingPlayers;
    for (const auto& Pair : Players)
    {
        if (Pair.Value.Controller.Get() == Actor)
        {
            EndingPlayers.Add(Pair.Key.Get());
        }
    }
    for (const TWeakObjectPtr<ULocalPlayer> Player : EndingPlayers)
    {
        ControllerChanged(nullptr, Player);
    }
}