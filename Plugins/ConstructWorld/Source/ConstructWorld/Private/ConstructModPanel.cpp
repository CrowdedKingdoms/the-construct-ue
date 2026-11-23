#include "ConstructModPanel.h"

#include "ConstructChunkView.h"
#include "ConstructModChunkAnchor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "Subsystem/CrowdyVoxelWorld.h"
#include "TimerManager.h"

UCrowdyMods* UConstructModPanel::Mods() const
{
    UWorld* World = BoundWorld.Get();
    UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
    return Game ? Game->GetSubsystem<UCrowdyMods>() : nullptr;
}

bool UConstructModPanel::CanUseSession() const
{
    UWorld* World = BoundWorld.Get();
    UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
    const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
    const APlayerController* Controller = GetOwningPlayer();
    return bBound && IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Session && !Session->HasAccessEnded() && Session->GetUserID() == UserId && UserId > 0;
}

bool UConstructModPanel::IsCurrent(uint64 ForBinding) const
{
    return bBound && BoundWorld.IsValid() && Binding == ForBinding;
}

bool UConstructModPanel::IsMutationCurrent(uint64 ForBinding, uint64 ForMutation) const
{
    return IsCurrent(ForBinding) && Mutation == ForMutation && MutationKind != EMutation::None;
}

void UConstructModPanel::BindGrid(int64 InGridId, FInt64Vector InChunk)
{
    CloseBinding();
    BoundWorld = GetWorld();
    GridId = InGridId;
    Chunk = InChunk;
    LandLabel = FText::FromString(FString::Printf(TEXT("Chunk (%lld, %lld, %lld)  |  Grid %lld"), Chunk.X, Chunk.Y, Chunk.Z, GridId));
    UGameInstance* Game = GetGameInstance();
    const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
    UserId = Session ? Session->GetUserID() : 0;
    bBound = GridId > 0 && BoundWorld.IsValid();
    SelectedStarter = ModName = TEXT("construct-spinning-child");
    State = StableBefore = EConstructModPanelState::Loading;
    SpawnView();
    ReadStatus();
    FetchStarter();
    if (UWorld* World = BoundWorld.Get())
    {
        const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
        World->GetTimerManager().SetTimer(StatusTimer, FTimerDelegate::CreateLambda([WeakThis]()
        {
            if (UConstructModPanel* Self = WeakThis.Get(); Self && Self->bBound && Self->bHasMod)
            {
                Self->ReadStatus();
            }
        }), 2.0f, true);
    }
}

void UConstructModPanel::CloseBinding()
{
    bBound = false;
    ++Binding;
    ++Mutation;
    ++StateRevision;
    ++StatusRequest;
    ++LogRequest;
    ++ReloadRequest;
    ++ErrorRequest;
    MutationKind = EMutation::None;
    bStatusPending = bLogPending = bReloadPending = bStarterPending = bWakePending = bEnableAnswered = false;
    bListKnown = bHasMod = bStarterReady = false;
    if (UWorld* World = BoundWorld.Get())
    {
        World->GetTimerManager().ClearTimer(StatusTimer);
        World->GetTimerManager().ClearTimer(ErrorTimer);
    }
    if (IsValid(View))
    {
        View->ClearChunks();
        View->Destroy();
    }
    if (IsValid(Anchor))
    {
        Anchor->Destroy();
    }
    View = nullptr;
    Anchor = nullptr;
    ListedMods.Reset();
    SubmittedFiles.Reset();
    SubmittedHashes.Reset();
    LastBuildId.Reset();
    BuildLog.Reset();
    LogText.Reset();
    CachedLog = FText::GetEmpty();
    bRunningFromList = false;
    Notice.Reset();
    StarterError.Reset();
    PlatformStarter = FCrowdyModStarter();
    ShownMod = FCrowdyMod();
    BoundWorld.Reset();
}

void UConstructModPanel::NativeDestruct()
{
    CloseBinding();
    Super::NativeDestruct();
}

void UConstructModPanel::SpawnView()
{
    UWorld* World = BoundWorld.Get();
    if (!bBound || !World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
    {
        return;
    }
    FActorSpawnParameters Spawn;
    Spawn.ObjectFlags |= RF_Transient;
    const FVector Centre = UCrowdyVoxelLibrary::GetChunkOrigin(this, Chunk.X, Chunk.Y, Chunk.Z) + FVector(UCrowdyVoxelLibrary::GetVoxelSize(this) * 8.0);
    Anchor = World->SpawnActor<AConstructModChunkAnchor>(Centre, FRotator::ZeroRotator, Spawn);
    View = World->SpawnActor<AConstructChunkView>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    if (View)
    {
        View->ShowLoadedChunks(Chunk.X, Chunk.Y, Chunk.Z);
    }
}

void UConstructModPanel::ChangeState(EConstructModPanelState Next)
{
    if (State != Next)
    {
        State = Next;
        OnStateChanged.Broadcast(State);
    }
}

bool UConstructModPanel::CanBuild() const
{
    return bBound && MutationKind == EMutation::None && !bLogPending && (State == EConstructModPanelState::NoMod || State == EConstructModPanelState::BuildFailed);
}

bool UConstructModPanel::CanDeploy() const
{
    return bBound && MutationKind == EMutation::None && !bLogPending && State == EConstructModPanelState::Built && !LastBuildId.IsEmpty();
}

bool UConstructModPanel::CanToggle(bool bEnabled) const
{
    return bBound && MutationKind == EMutation::None && !bLogPending && (bEnabled ? State == EConstructModPanelState::Off : State == EConstructModPanelState::On || State == EConstructModPanelState::Blocked);
}

bool UConstructModPanel::CanLog() const
{
    return bBound && MutationKind == EMutation::None && !bLogPending && (State == EConstructModPanelState::Built || State == EConstructModPanelState::Off || State == EConstructModPanelState::On);
}

bool UConstructModPanel::CanDelete() const
{
    return bBound && MutationKind == EMutation::None && !bLogPending && (State == EConstructModPanelState::Off || State == EConstructModPanelState::Blocked);
}

bool UConstructModPanel::CanRefresh() const
{
    return bBound && MutationKind == EMutation::None && !bStatusPending && !bReloadPending && State != EConstructModPanelState::Loading && State != EConstructModPanelState::Building;
}

bool UConstructModPanel::BeginMutation(EMutation Kind)
{
    if (!bBound || MutationKind != EMutation::None || bLogPending)
    {
        return false;
    }
    StableBefore = State;
    MutationKind = Kind;
    ++Mutation;
    ++StateRevision;
    bEnableAnswered = false;
    Notice.Reset();
    return true;
}

void UConstructModPanel::GetLastBuildFiles(FString& BuildId, TArray<FConstructModFileHash>& Files) const
{
    BuildId = LastBuildId;
    Files = SubmittedHashes;
}

void UConstructModPanel::ShowError(ECrowdyModResult Result, const FString& Message, bool bRestore)
{
    const EConstructModPanelState ReturnState = State == EConstructModPanelState::Error ? StableBefore : State;
    if (State != EConstructModPanelState::Error)
    {
        StableBefore = State;
    }
    const uint64 ForBinding = Binding;
    const uint64 ForError = ++ErrorRequest;
    Notice = FString::Printf(TEXT("%s: %s"), *StaticEnum<ECrowdyModResult>()->GetNameStringByValue(int64(Result)), *Message);
    ChangeState(EConstructModPanelState::Error);
    UWorld* World = BoundWorld.Get();
    if (bRestore && IsCurrent(ForBinding) && World && ReturnState != EConstructModPanelState::Loading && ReturnState != EConstructModPanelState::Building && ReturnState != EConstructModPanelState::Error)
    {
        const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
        World->GetTimerManager().SetTimer(ErrorTimer, FTimerDelegate::CreateLambda([WeakThis, ForBinding, ForError, ReturnState]()
        {
            if (UConstructModPanel* Self = WeakThis.Get(); Self && Self->IsCurrent(ForBinding) && Self->ErrorRequest == ForError && Self->State == EConstructModPanelState::Error)
            {
                Self->ChangeState(ReturnState);
            }
        }), 2.0f, false);
    }
}

void UConstructModPanel::ApplyMod(const FCrowdyMod& Mod)
{
    ShownMod = Mod;
    bHasMod = true;
    ChangeState(Mod.bBlocked ? EConstructModPanelState::Blocked : Mod.bEnabled ? EConstructModPanelState::On : EConstructModPanelState::Off);
}

void UConstructModPanel::DeriveState()
{
    if (!bListKnown || (SelectedStarter == TEXT("platform-greeter") && !bStarterReady))
    {
        return;
    }
    const FCrowdyMod* Found = ListedMods.FindByPredicate([this](const FCrowdyMod& Mod) { return Mod.GridId == GridId && Mod.Name == ModName; });
    if (Found)
    {
        ApplyMod(*Found);
    }
    else
    {
        bHasMod = false;
        ShownMod = FCrowdyMod();
        ChangeState(EConstructModPanelState::NoMod);
    }
}

void UConstructModPanel::ReadStatus(bool bManual)
{
    if (!bBound || bStatusPending || MutationKind != EMutation::None || (!bManual && (State == EConstructModPanelState::Building || State == EConstructModPanelState::BuildFailed || State == EConstructModPanelState::Built)))
    {
        return;
    }
    UCrowdyMods* SDK = Mods();
    if (!SDK || !CanUseSession())
    {
        if (!bListKnown)
        {
            ShowError(ECrowdyModResult::NotSignedIn, TEXT("Sign in to read this grid's mods."), false);
        }
        return;
    }
    bStatusPending = true;
    const uint64 ForBinding = Binding, Request = ++StatusRequest, Revision = StateRevision;
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    SDK->ListMods(GridId, [WeakThis, ForBinding, Request, Revision](const FCrowdyModList& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->StatusReturned(ForBinding, Request, Revision, Reply);
        }
    });
}

void UConstructModPanel::StatusReturned(uint64 ForBinding, uint64 Request, uint64 Revision, const FCrowdyModList& Reply)
{
    if (!IsCurrent(ForBinding) || StatusRequest != Request)
    {
        return;
    }
    bStatusPending = false;
    if (Revision != StateRevision || MutationKind != EMutation::None)
    {
        return;
    }
    if (Reply.Result != ECrowdyModResult::Success)
    {
        ShowError(Reply.Result, Reply.Message, bListKnown);
        return;
    }
    if (State == EConstructModPanelState::Error)
    {
        if (SelectedStarter == TEXT("platform-greeter") && !bStarterReady && !StarterError.IsEmpty())
        {
            Notice = FString::Printf(TEXT("%s: %s"), *StaticEnum<ECrowdyModResult>()->GetNameStringByValue(int64(StarterResult)), *StarterError);
        }
        else
        {
            Notice.Reset();
        }
    }
    const bool bPreserveBuild = State == EConstructModPanelState::BuildFailed || State == EConstructModPanelState::Built || (State == EConstructModPanelState::Error && (StableBefore == EConstructModPanelState::Built || StableBefore == EConstructModPanelState::BuildFailed));
    ListedMods = Reply.Mods;
    bListKnown = true;
    if (!bPreserveBuild)
    {
        bRunningFromList = true;
        DeriveState();
    }
}

void UConstructModPanel::SelectStarter(const FString& Key)
{
    if (!bBound || MutationKind != EMutation::None || bLogPending || (Key != TEXT("construct-spinning-child") && Key != TEXT("platform-greeter")) || Key == SelectedStarter)
    {
        return;
    }
    SelectedStarter = Key;
    LastBuildId.Reset();
    SubmittedHashes.Reset();
    SubmittedFiles.Reset();
    BuildLog.Reset();
    ModName = Key == TEXT("platform-greeter") ? PlatformStarter.Name : Key;
    ChangeState(EConstructModPanelState::Loading);
    if (Key == TEXT("platform-greeter") && !bStarterReady)
    {
        FetchStarter();
    }
    else if (bListKnown)
    {
        DeriveState();
    }
    else
    {
        ReadStatus();
    }
}

void UConstructModPanel::FetchStarter()
{
    UCrowdyMods* SDK = Mods();
    if (!bBound || bStarterPending || !SDK || !CanUseSession())
    {
        return;
    }
    bStarterPending = true;
    const uint64 ForBinding = Binding;
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    SDK->GetStarter([WeakThis, ForBinding](const FCrowdyModStarter& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->StarterReturned(ForBinding, Reply);
        }
    });
}

void UConstructModPanel::StarterReturned(uint64 ForBinding, const FCrowdyModStarter& Reply)
{
    if (!IsCurrent(ForBinding))
    {
        return;
    }
    bStarterPending = false;
    StarterResult = Reply.Result;
    bStarterReady = Reply.Result == ECrowdyModResult::Success && !Reply.Name.IsEmpty() && !Reply.Files.IsEmpty();
    if (bStarterReady)
    {
        PlatformStarter = Reply;
        StarterError.Reset();
    }
    else
    {
        StarterError = Reply.Message.IsEmpty() ? TEXT("The greeting starter could not be fetched.") : Reply.Message;
    }
    if (SelectedStarter == TEXT("platform-greeter"))
    {
        if (!bStarterReady)
        {
            ShowError(Reply.Result, StarterError, false);
        }
        else
        {
            ModName = PlatformStarter.Name;
            if (bListKnown)
            {
                Notice.Reset();
            }
            DeriveState();
        }
    }
}

void UConstructModPanel::Refresh()
{
    if (!CanRefresh())
    {
        return;
    }
    if (SelectedStarter == TEXT("platform-greeter") && !bStarterReady)
    {
        FetchStarter();
    }
    ReadStatus(true);
    UWorld* World = BoundWorld.Get();
    UCrowdyVoxelWorld* Voxels = World ? World->GetSubsystem<UCrowdyVoxelWorld>() : nullptr;
    if (!Voxels || bReloadPending || !Voxels->IsChunkLoaded(Chunk.X, Chunk.Y, Chunk.Z))
    {
        return;
    }
    bReloadPending = true;
    const uint64 ForBinding = Binding, Request = ++ReloadRequest;
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    Voxels->ReloadChunk(Chunk.X, Chunk.Y, Chunk.Z, [WeakThis, ForBinding, Request](ECrowdyVoxelResult Result, const FString& Message)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->ReloadReturned(ForBinding, Request, Result == ECrowdyVoxelResult::Ok, Message);
        }
    });
}

void UConstructModPanel::ReloadReturned(uint64 ForBinding, uint64 Request, bool bSuccess, const FString& Message)
{
    if (!IsCurrent(ForBinding) || Request != ReloadRequest)
    {
        return;
    }
    bReloadPending = false;
    if (!bSuccess)
    {
        Notice = Message;
    }
}
FText UConstructModPanel::RefreshHint() const
{
    const UWorld* World = BoundWorld.Get();
    const UCrowdyVoxelWorld* Voxels = World ? World->GetSubsystem<UCrowdyVoxelWorld>() : nullptr;
    if (bReloadPending)
    {
        return NSLOCTEXT("ConstructMods", "Reloading", "Reading the latest blocks...");
    }
    if (!Voxels || !Voxels->IsChunkLoaded(Chunk.X, Chunk.Y, Chunk.Z))
    {
        return NSLOCTEXT("ConstructMods", "ChunkLoading", "Blocks are loading. Refresh can reload them once this chunk is ready.");
    }
    return NSLOCTEXT("ConstructMods", "RefreshHint", "Refresh reads the latest blocks. Closing this panel leaves an enabled mod on.");
}