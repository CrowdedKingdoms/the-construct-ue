#include "ConstructModScriptHost.h"

#include "ConstructClaimsSubsystem.h"
#include "ConstructDeveloperSettings.h"
#include "ConstructModScriptPanel.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/CrowdyModScripts.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "ConstructScriptHost"

namespace
{
using FGroup = ECrowdyModScriptGroup;
struct FCallCaption
{
    const TCHAR* Name;
    FGroup Group;
    const TCHAR* Description;
};
const FCallCaption Captions[] = {
    {TEXT("user_state_get"), FGroup::State, TEXT("Read your app state")},
    {TEXT("user_state_set"), FGroup::State, TEXT("Update your app state")},
    {TEXT("chunk_get"), FGroup::WorldRead, TEXT("Read blocks on this land")},
    {TEXT("voxels_list"), FGroup::WorldRead, TEXT("Read block types and voxel state on this land")},
    {TEXT("actors_list"), FGroup::WorldRead, TEXT("Read players on this land")},
    {TEXT("actors_list_radius"), FGroup::WorldRead, TEXT("Read nearby players on this land")},
    {TEXT("voxel_set"), FGroup::WorldWrite, TEXT("Request block edits using your own permissions")},
    {TEXT("grid_info"), FGroup::Meta, TEXT("Read this land's boundaries")},
    {TEXT("grid_permission_check"), FGroup::Meta, TEXT("Check your code permissions on this land")},
    {TEXT("clock"), FGroup::Meta, TEXT("Read the current time")},
    {TEXT("emit_event"), FGroup::Egress, TEXT("Send events to scripts here on this computer")},
    {TEXT("emit_spatial"), FGroup::Egress, TEXT("Send nearby messages from this land")},
    {TEXT("emit_channel"), FGroup::Egress, TEXT("Send messages to this land's channels")},
    {TEXT("hud_set"), FGroup::Present, TEXT("Show text in your Mod Script HUD")},
    {TEXT("overlay_draw"), FGroup::Present, TEXT("Draw shapes only you can see")},
    {TEXT("pointer_clicks"), FGroup::Input, TEXT("Read mouse clicks that the game receives")}
};

FText GroupName(FGroup Group)
{
    switch (Group)
    {
    case FGroup::State: return LOCTEXT("State", "Your app state");
    case FGroup::WorldRead: return LOCTEXT("WorldRead", "Read this land");
    case FGroup::WorldWrite: return LOCTEXT("WorldWrite", "Edit this land");
    case FGroup::Meta: return LOCTEXT("Meta", "Land information");
    case FGroup::Egress: return LOCTEXT("Egress", "Messages and events");
    case FGroup::Present: return LOCTEXT("Present", "Text and local shapes");
    case FGroup::Input: return LOCTEXT("Input", "Game input");
    default: return LOCTEXT("Other", "Other capabilities");
    }
}

FText RefusedCaption(const FString& Call)
{
    static const TMap<FString, FText> RefusedCaptions = {
        {TEXT("avatar_state_get"), LOCTEXT("Refused_avatar_state_get", "Read your avatar state")},
        {TEXT("input_axes"), LOCTEXT("Refused_input_axes", "Read movement input")},
        {TEXT("input_look"), LOCTEXT("Refused_input_look", "Read look input")},
        {TEXT("input_key"), LOCTEXT("Refused_input_key", "Read keyboard input")},
        {TEXT("pose_get"), LOCTEXT("Refused_pose_get", "Read poses")},
        {TEXT("pose_set"), LOCTEXT("Refused_pose_set", "Change poses")},
        {TEXT("pose_release"), LOCTEXT("Refused_pose_release", "Release poses")},
        {TEXT("teleport_request"), LOCTEXT("Refused_teleport_request", "Request teleportation")},
        {TEXT("actor_spawn"), LOCTEXT("Refused_actor_spawn", "Spawn actors")},
        {TEXT("actor_pose"), LOCTEXT("Refused_actor_pose", "Change actor poses")},
        {TEXT("actor_despawn"), LOCTEXT("Refused_actor_despawn", "Remove actors")},
        {TEXT("scene_catalog"), LOCTEXT("Refused_scene_catalog", "Read the scene catalog")},
        {TEXT("scene_instances"), LOCTEXT("Refused_scene_instances", "Read scene instances")},
        {TEXT("avatar_appearance"), LOCTEXT("Refused_avatar_appearance", "Change avatar appearance")},
        {TEXT("avatar_state_set"), LOCTEXT("Refused_avatar_state_set", "Change avatar state")},
        {TEXT("voice_set"), LOCTEXT("Refused_voice_set", "Change voice settings")},
        {TEXT("video_set"), LOCTEXT("Refused_video_set", "Change video settings")},
        {TEXT("send_client_event"), LOCTEXT("Refused_send_client_event", "Send client events")},
        {TEXT("events_poll"), LOCTEXT("Refused_events_poll", "Read events")},
        {TEXT("send_text"), LOCTEXT("Refused_send_text", "Send text")},
        {TEXT("send_actor_message"), LOCTEXT("Refused_send_actor_message", "Send actor messages")},
        {TEXT("send_channel_message"), LOCTEXT("Refused_send_channel_message", "Send channel messages")},
    };
    if (const FText* Caption = RefusedCaptions.Find(Call))
    {
        return *Caption;
    }
    return FText::Format(LOCTEXT("RefusedOther", "Other capability: {0}"), FText::FromString(Call.Left(128)));
}

FText StopReason(ECrowdyModScriptStop Reason)
{
    switch (Reason)
    {
    case ECrowdyModScriptStop::Removed: return LOCTEXT("Removed", "No longer offered on this land.");
    case ECrowdyModScriptStop::Changed: return LOCTEXT("Changed", "The code or capabilities changed.");
    case ECrowdyModScriptStop::Unconsented: return LOCTEXT("Unconsented", "Your agreement was taken back elsewhere.");
    case ECrowdyModScriptStop::Revoked: return LOCTEXT("Revoked", "You revoked this script.");
    case ECrowdyModScriptStop::LeftGrid: return LOCTEXT("Left", "You left this land.");
    case ECrowdyModScriptStop::CircuitOpen: return LOCTEXT("Circuit", "Stopped after repeated script failures.");
    case ECrowdyModScriptStop::Failed: return LOCTEXT("Failed", "The script could not start.");
    default: return LOCTEXT("Stopped", "Stopped.");
    }
}
}

bool UConstructModScriptHost::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}

bool UConstructModScriptHost::IsCheckMode(const UClass* Class)
{
    static const FName HarnessPackage(TEXT("/Script/ConstructHarness"));
    for (const UClass* Current = Class; Current; Current = Current->GetSuperClass())
    {
        if (Current->GetOutermost()->GetFName() == HarnessPackage)
        {
            return true;
        }
    }
    return false;
}

void UConstructModScriptHost::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    const AGameModeBase* Mode = World.GetAuthGameMode();
    bEnabledForWorld = GetDefault<UConstructDeveloperSettings>()->bEnableModScripts && World.GetNetMode() != NM_DedicatedServer && !IsCheckMode(Mode ? Mode->GetClass() : nullptr);
    if (!bEnabledForWorld)
    {
        return;
    }
    Claims = World.GetSubsystem<UConstructClaimsSubsystem>();
    BindSource(World.GetSubsystem<UCrowdyModScripts>());
    if (UGameInstance* Game = World.GetGameInstance())
    {
        if (UCrowdySDKSubsystem* SDK = Game->GetSubsystem<UCrowdySDKSubsystem>())
        {
            SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::SignedOut);
        }
        if (UCrowdyGameSession* Session = Game->GetSubsystem<UCrowdyGameSession>())
        {
            Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
        }
    }
}

void UConstructModScriptHost::BindSource(UCrowdyModScripts* NewSource)
{
    if (NewSource == Source || !NewSource || NewSource->GetWorld() != GetWorld())
    {
        return;
    }
    UnbindSource();
    Source = NewSource;
    Source->OnModScriptPrompt.AddUniqueDynamic(this, &ThisClass::Prompted);
    Source->OnModScriptPromptWithdrawn.AddUniqueDynamic(this, &ThisClass::PromptWithdrawn);
    Source->OnModScriptStarted.AddUniqueDynamic(this, &ThisClass::Started);
    Source->OnModScriptStopped.AddUniqueDynamic(this, &ThisClass::Stopped);
    Source->OnModScriptError.AddUniqueDynamic(this, &ThisClass::SourceError);
    Presentation->OnAnswerRequested.AddUObject(this, &ThisClass::Answer);
    Presentation->OnRevokeRequested.AddUObject(this, &ThisClass::Revoke);
    Presentation->OnForgetRequested.AddUObject(this, &ThisClass::Forget);
    Presentation->SetContext(Source->IsAvailable() ? FText::GetEmpty() : LOCTEXT("RuntimeUnavailable", "Mod Scripts are unavailable in this build."));
    TScriptInterface<ICrowdyModScriptHost> Host;
    Host.SetObject(this);
    Host.SetInterface(this);
    Source->SetHost(Host);
    VoxelWorld = GetWorld()->GetSubsystem<UCrowdyVoxelWorld>();
    if (UCrowdyVoxelWorld* Voxels = VoxelWorld.Get())
    {
        Voxels->OnEditDone.AddUniqueDynamic(this, &ThisClass::EditDone);
    }
    // Subscribe before reading the snapshot so a source that is already running is not missed.
    for (const FCrowdyModScript& Script : Source->GetRunning())
    {
        Started(Script);
    }
}

void UConstructModScriptHost::UnbindSource()
{
    LeaveVisit();
    if (Source)
    {
        TScriptInterface<ICrowdyModScriptHost> Host;
        Host.SetObject(this);
        Host.SetInterface(this);
        Source->ClearHost(Host);
        Source->OnModScriptPrompt.RemoveDynamic(this, &ThisClass::Prompted);
        Source->OnModScriptPromptWithdrawn.RemoveDynamic(this, &ThisClass::PromptWithdrawn);
        Source->OnModScriptStarted.RemoveDynamic(this, &ThisClass::Started);
        Source->OnModScriptStopped.RemoveDynamic(this, &ThisClass::Stopped);
        Source->OnModScriptError.RemoveDynamic(this, &ThisClass::SourceError);
    }
    if (UCrowdyVoxelWorld* Voxels = VoxelWorld.Get())
    {
        Voxels->OnEditDone.RemoveDynamic(this, &ThisClass::EditDone);
    }
    Presentation->OnAnswerRequested.RemoveAll(this);
    Presentation->OnRevokeRequested.RemoveAll(this);
    Presentation->OnForgetRequested.RemoveAll(this);
    Source = nullptr;
    VoxelWorld.Reset();
    Presentation->SetContext(FText::GetEmpty());
}

void UConstructModScriptHost::SetController(APlayerController* Controller)
{
    APlayerController* NextController = IsValid(Controller) && !Controller->IsActorBeingDestroyed() ? Controller : nullptr;
    if (NextController == PlayerController.Get() && (NextController ? IsValid(Panel) : !Panel))
    {
        return;
    }
    if (Panel)
    {
        Panel->Shutdown();
        Panel->RemoveFromParent();
        Panel = nullptr;
    }
    PlayerController = NextController;
    if (PlayerController.IsValid())
    {
        Panel = CreateWidget<UConstructModScriptPanel>(PlayerController.Get());
        if (Panel)
        {
            Panel->SetPresentation(Presentation.ToSharedRef());
            Panel->AddToPlayerScreen(45);
        }
    }
}

bool UConstructModScriptHost::Inside(const FCrowdyModScriptGrid& Grid, const FInt64Vector& Chunk)
{
    return Grid.GridId > 0 && Chunk.X >= Grid.LowChunkX && Chunk.X <= Grid.HighChunkX && Chunk.Y >= Grid.LowChunkY && Chunk.Y <= Grid.HighChunkY && Chunk.Z >= Grid.LowChunkZ && Chunk.Z <= Grid.HighChunkZ;
}

bool UConstructModScriptHost::ShouldCancelVisit(const FCrowdyModScriptGrid& Grid, const FInt64Vector& Chunk, bool bCurrentLookupSucceeded) const
{
    if (Grid.GridId > 0)
    {
        return !Inside(Grid, Chunk) || !bCurrentLookupSucceeded;
    }
    // An empty SDK grid can still have an asynchronous entry awaiting its platform reply.
    return AppliedLookup != 0 && (!bCurrentLookupSucceeded || (bHasChunk && VisitChunk != Chunk));
}

void UConstructModScriptHost::LeaveVisit()
{
    if (Source && (bHasChunk || Source->GetGrid().GridId > 0))
    {
        Source->LeaveGrid();
    }
    bHasChunk = false;
    AppliedLookup = 0;
}

void UConstructModScriptHost::UpdateVisit()
{
    const APlayerController* Controller = PlayerController.Get();
    const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    UConstructClaimsSubsystem* ClaimSource = Claims.Get();
    if (!Source || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !ClaimSource || Pawn->GetActorLocation().ContainsNaN())
    {
        LeaveVisit();
        return;
    }
    FInt64Vector Chunk;
    FIntVector Cell;
    UCrowdyVoxelLibrary::WorldToVoxel(this, Pawn->GetActorLocation(), Chunk.X, Chunk.Y, Chunk.Z, Cell);
    const FCrowdyModScriptGrid Grid = Source->GetGrid();
    const FConstructClaimLookup* Lookup = ClaimSource->GetLookup(Chunk);
    const bool bCurrentLookupSucceeded = Lookup && !Lookup->bPending && Lookup->bSucceeded && ClaimSource->IsLatestLookup(Chunk, Lookup->Request);
    if (ShouldCancelVisit(Grid, Chunk, bCurrentLookupSucceeded))
    {
        LeaveVisit();
    }
    if (!bHasChunk || VisitChunk != Chunk)
    {
        VisitChunk = Chunk;
        bHasChunk = true;
        AppliedLookup = 0;
        NextLookup = 0.0;
    }
    if (bCurrentLookupSucceeded)
    {
        if (AppliedLookup != Lookup->Request)
        {
            AppliedLookup = Lookup->Request;
            if (Lookup->Claim.IsSet() && Lookup->Claim->GridId > 0)
            {
                Source->EnterGrid(Chunk.X, Chunk.Y, Chunk.Z);
            }
            else
            {
                Source->LeaveGrid();
            }
        }
        return;
    }
    // A pending or failed ownership lookup grants no visit, including an unresolved SDK entry.
    AppliedLookup = 0;
    const double Now = GetWorld()->GetTimeSeconds();
    if ((!Lookup || !Lookup->bPending) && Now >= NextLookup)
    {
        NextLookup = Now + 1.0;
        ClaimSource->ConfirmChunk(Chunk);
    }
}

void UConstructModScriptHost::Tick(float DeltaSeconds)
{
    if (!IsTickable())
    {
        return;
    }
    UGameInstance* Game = GetWorld()->GetGameInstance();
    UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
    UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    const bool bReady = Auth && Auth->IsSignedIn() && Session && !Session->HasAccessEnded() && Session->GetUserID() > 0 && Controller && Controller->IsLocalController();
    if (!bReady)
    {
        if (SessionUser != 0 || Panel)
        {
            EndSession();
        }
        return;
    }
    if (SessionUser != Session->GetUserID())
    {
        EndSession();
        SessionUser = Session->GetUserID();
    }
    SetController(Controller);
    if (Source && Source->IsAvailable())
    {
        UpdateVisit();
    }
}

bool UConstructModScriptHost::IsTickable() const
{
    return bEnabledForWorld && !bEnded && !IsTemplate();
}

TStatId UConstructModScriptHost::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UConstructModScriptHost, STATGROUP_Tickables);
}

void UConstructModScriptHost::Prompted(const FCrowdyModScriptPrompt& Prompt)
{
    FConstructScriptConsent Display;
    Display.RequestId = Prompt.PromptId;
    Display.Author = FText::Format(LOCTEXT("Author", "Land owner {0}"), FText::AsNumber(Prompt.AuthorId));
    Display.Scripts = FText::FromString(FString::Join(Prompt.Scripts, TEXT(", ")));
    for (FGroup Group : Prompt.Groups)
    {
        Display.Groups.Add(GroupName(Group));
    }
    TSet<FString> Shown;
    for (int32 GroupIndex = 0; GroupIndex < 7; ++GroupIndex)
    {
        const FGroup Group = static_cast<FGroup>(GroupIndex);
        for (const FCallCaption& Caption : Captions)
        {
            if (Caption.Group == Group && Prompt.Calls.Contains(Caption.Name) && !Prompt.NotOffered.Contains(Caption.Name))
            {
                Display.Calls.Add(FText::Format(LOCTEXT("GroupedCall", "{0}: {1}"), GroupName(Group), FText::FromString(Caption.Description)));
                Shown.Add(Caption.Name);
            }
        }
    }
    for (const FString& Call : Prompt.Calls)
    {
        if (!Shown.Contains(Call) && !Prompt.NotOffered.Contains(Call))
        {
            Display.Calls.Add(FText::Format(LOCTEXT("OtherCall", "Other requested capability: {0}"), FText::FromString(Call.Left(128))));
        }
    }
    for (const FString& Call : Prompt.NotOffered)
    {
        Display.NotOffered.Add(RefusedCaption(Call));
    }
    Presentation->ShowPrompt(MoveTemp(Display));
}

void UConstructModScriptHost::PromptWithdrawn(int32 Id)
{
    Presentation->WithdrawPrompt(Id);
}

void UConstructModScriptHost::Started(const FCrowdyModScript& Script)
{
    Presentation->Started(Label(Script));
}

void UConstructModScriptHost::Stopped(const FCrowdyModScript& Script, ECrowdyModScriptStop Reason)
{
    Presentation->Stopped(Script.ModId, Script.Generation, StopReason(Reason));
}

void UConstructModScriptHost::SourceError(const FCrowdyModScriptError& Error)
{
    GetWorld()->GetTimerManager().ClearTimer(FeedbackTimer);
    const FString Message = Error.Message.IsEmpty() ? Error.Code : Error.Message;
    const FConstructScriptCard* Card = Presentation->Find(Error.ModId);
    const FText Detail = FText::FromString(Message.IsEmpty() ? TEXT("The Mod Script request failed.") : Message);
    Presentation->Error(Error.ModId, Card ? FText::Format(LOCTEXT("NamedError", "{0}: {1}"), FText::FromString(Card->Label.Name), Detail) : Detail);
}

void UConstructModScriptHost::Answer(int32 Id, bool bRun)
{
    if (Source && !bEnded)
    {
        UCrowdyModScripts* AnswerSource = Source;
        // Withdrawal is only for stale unanswered prompts. Submitting a choice consumes this dialog,
        // while running and failure states still come only from the source's events.
        Presentation->WithdrawPrompt(Id);
        if (Source == AnswerSource && !bEnded)
        {
            AnswerSource->AnswerPrompt(Id, bRun);
        }
    }
}

void UConstructModScriptHost::Revoke(const FString& Id)
{
    if (Source && !bEnded)
    {
        Source->Revoke(Id);
    }
}

void UConstructModScriptHost::Forget(int64 AuthorId)
{
    if (Source && !bEnded)
    {
        Source->ForgetAuthor(AuthorId);
    }
}

void UConstructModScriptHost::ShowFeedback(const FText& Message)
{
    if (!bEnded)
    {
        Presentation->SetNotice(Message);
        GetWorld()->GetTimerManager().SetTimer(FeedbackTimer, this, &ThisClass::HideFeedback, 3.0f);
    }
}

void UConstructModScriptHost::HideFeedback()
{
    Presentation->SetNotice(FText::GetEmpty());
}

void UConstructModScriptHost::EditDone(int64 Id, const FCrowdyVoxelWrite& Write, ECrowdyVoxelResult Result, const FString& Message)
{
    FConstructScriptLabel SourceLabel;
    if (!SubmittedEdits.RemoveAndCopyValue(Id, SourceLabel) || Result == ECrowdyVoxelResult::Ok)
    {
        return;
    }
    ShowFeedback(FText::Format(LOCTEXT("WriteRefused", "{0}: {1}"), FText::FromString(SourceLabel.Name), FText::FromString(Message.IsEmpty() ? TEXT("The block edit was refused.") : Message)));
}

void UConstructModScriptHost::EndSession()
{
    LeaveVisit();
    SetController(nullptr);
    GetWorld()->GetTimerManager().ClearTimer(FeedbackTimer);
    SubmittedEdits.Reset();
    Presentation->Reset();
    SessionUser = 0;
}

void UConstructModScriptHost::SignedOut(bool bSuccess)
{
    if (bSuccess)
    {
        EndSession();
    }
}

void UConstructModScriptHost::AccessEnded(const FString& Reason, const FString& BlockedUntil)
{
    EndSession();
}

void UConstructModScriptHost::Deinitialize()
{
    bEnded = true;
    bEnabledForWorld = false;
    SetController(nullptr);
    UnbindSource();
    GetWorld()->GetTimerManager().ClearTimer(FeedbackTimer);
    if (UGameInstance* Game = GetWorld()->GetGameInstance())
    {
        if (UCrowdySDKSubsystem* SDK = Game->GetSubsystem<UCrowdySDKSubsystem>())
        {
            SDK->OnLogout.RemoveDynamic(this, &ThisClass::SignedOut);
        }
        if (UCrowdyGameSession* Session = Game->GetSubsystem<UCrowdyGameSession>())
        {
            Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
        }
    }
    SubmittedEdits.Reset();
    Presentation->Reset();
    Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE
