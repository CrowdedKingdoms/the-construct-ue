#include "ConstructStudioPanelWorld.h"

#include "ConstructDeveloperSettings.h"
#include "ConstructStudioPanel.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UConstructStudioPanelWorld::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UConstructStudioPanelWorld::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    bEnabled = World.GetNetMode() != NM_DedicatedServer && GetDefault<UConstructDeveloperSettings>()->bEnableStudioDock;
}

void UConstructStudioPanelWorld::Tick(float DeltaSeconds)
{
    RefreshIn -= DeltaSeconds;
    if (RefreshIn > 0.0f || !GetWorld() || !GetWorld()->GetGameInstance())
    {
        return;
    }
    RefreshIn = 0.15f;
    UGameInstance* Game = GetWorld()->GetGameInstance();
    for (int32 Index = Players.Num() - 1; Index >= 0; --Index)
    {
        FConstructStudioPlayerPanel& Entry = Players[Index];
        if (!Entry.Player.IsValid() || !Game->GetLocalPlayers().Contains(Entry.Player.Get()))
        {
            if (Entry.Panel)
            {
                Entry.Panel->Shutdown();
                Entry.Panel->RemoveFromParent();
            }
            Players.RemoveAt(Index);
        }
    }
    for (ULocalPlayer* Player : Game->GetLocalPlayers())
    {
        FConstructStudioPlayerPanel* Entry = Players.FindByPredicate([Player](const FConstructStudioPlayerPanel& Item) { return Item.Player.Get() == Player; });
        if (!Entry)
        {
            Entry = &Players.AddDefaulted_GetRef();
            Entry->Player = Player;
        }
        APlayerController* Controller = Player->GetPlayerController(GetWorld());
        if (Entry->Controller.Get() != Controller)
        {
            if (Entry->Panel)
            {
                Entry->Panel->Shutdown();
                Entry->Panel->RemoveFromParent();
                Entry->Panel = nullptr;
            }
            Entry->Controller = Controller;
        }
        if (!Entry->Panel && IsValid(Controller) && Controller->IsLocalController() && !Controller->IsActorBeingDestroyed())
        {
            Entry->Panel = CreateWidget<UConstructStudioPanel>(Controller);
            if (Entry->Panel)
            {
                Entry->Panel->AddToViewport(40);
            }
        }
        if (Entry->Panel)
        {
            Entry->Panel->RefreshOwnedChunk();
        }
    }
}

bool UConstructStudioPanelWorld::IsTickable() const
{
    return bEnabled && !bEnded && !IsTemplate();
}

TStatId UConstructStudioPanelWorld::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UConstructStudioPanelWorld, STATGROUP_Tickables);
}

UConstructStudioPanel* UConstructStudioPanelWorld::GetPanel(ULocalPlayer* Player) const
{
    const FConstructStudioPlayerPanel* Entry = Players.FindByPredicate([Player](const FConstructStudioPlayerPanel& Item) { return Item.Player.Get() == Player; });
    return Entry ? Entry->Panel.Get() : nullptr;
}

void UConstructStudioPanelWorld::ClearPanels()
{
    for (FConstructStudioPlayerPanel& Entry : Players)
    {
        if (Entry.Panel)
        {
            Entry.Panel->Shutdown();
            Entry.Panel->RemoveFromParent();
        }
    }
    Players.Reset();
}

void UConstructStudioPanelWorld::Deinitialize()
{
    bEnded = true;
    ClearPanels();
    Super::Deinitialize();
}

