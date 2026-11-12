#include "ConstructModPanel.h"

#include "ConstructModSources.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

void UConstructModPanel::Build(const FString& StarterName)
{
    if (!CanBuild() || (StarterName != TEXT("construct-spinning-child") && StarterName != TEXT("platform-greeter")))
    {
        return;
    }
    if (StarterName == TEXT("platform-greeter") && !bStarterReady)
    {
        SelectStarter(StarterName);
        return;
    }
    SelectStarter(StarterName);
    if (!CanBuild() || !BeginMutation(EMutation::Build))
    {
        return;
    }
    const uint64 ForBinding = Binding, ForMutation = Mutation;
    SubmittedFiles.Reset();
    SubmittedHashes.Reset();
    LastBuildId.Reset();
    BuildLog.Reset();
    LogText.Reset();
    BuildStatus = ECrowdyModBuildStatus::Queued;
    BuildStartedAt = FPlatformTime::Seconds();
    FString Error;
    const bool bLoaded = SelectedStarter == TEXT("platform-greeter")
        ? (SubmittedFiles = PlatformStarter.Files, true)
        : ConstructModSources::Read(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Mods"), SelectedStarter), SubmittedFiles, Error);
    if (!bLoaded)
    {
        FCrowdyModBuild Failed;
        Failed.Result = ECrowdyModResult::Failed;
        Failed.Message = Error;
        BuildReturned(ForBinding, ForMutation, Failed);
        return;
    }
    FreezeSources(SubmittedFiles);
    ChangeState(EConstructModPanelState::Building);
    if (!IsMutationCurrent(ForBinding, ForMutation))
    {
        return;
    }
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    UCrowdyMods* SDK = Mods();
    if (!SDK || !CanUseSession())
    {
        FCrowdyModBuild Failed;
        Failed.Result = ECrowdyModResult::NotSignedIn;
        Failed.Message = TEXT("Sign in to build a mod.");
        BuildReturned(ForBinding, ForMutation, Failed);
        return;
    }
    SDK->BuildMod(ModName, SubmittedFiles, [WeakThis, ForBinding, ForMutation](const FCrowdyModBuild& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->BuildProgress(ForBinding, ForMutation, Reply);
        }
    }, [WeakThis, ForBinding, ForMutation](const FCrowdyModBuild& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->BuildReturned(ForBinding, ForMutation, Reply);
        }
    });
}

void UConstructModPanel::BuildProgress(uint64 ForBinding, uint64 ForMutation, const FCrowdyModBuild& Reply)
{
    if (!IsMutationCurrent(ForBinding, ForMutation) || MutationKind != EMutation::Build)
    {
        return;
    }
    BuildStatus = Reply.Status;
    BuildLog = Reply.Log;
    CachedLog = FText::FromString(BuildLog);
    OnBuildProgress.Broadcast(Reply.Status);
}

void UConstructModPanel::BuildReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModBuild& Reply)
{
    if (!IsMutationCurrent(ForBinding, ForMutation) || MutationKind != EMutation::Build)
    {
        return;
    }
    MutationKind = EMutation::None;
    LastBuildId = Reply.BuildId;
    BuildStatus = Reply.Status;
    BuildLog = Reply.Log;
    CachedLog = FText::FromString(BuildLog);
    Notice = Reply.Result == ECrowdyModResult::Success ? Reply.Message
        : FString::Printf(TEXT("%s: %s"), *StaticEnum<ECrowdyModResult>()->GetNameStringByValue(int64(Reply.Result)), *Reply.Message);
    const bool bSucceeded = Reply.Result == ECrowdyModResult::Success && Reply.Status == ECrowdyModBuildStatus::Succeeded && !Reply.BuildId.IsEmpty();
    ChangeState(bSucceeded ? EConstructModPanelState::Built : EConstructModPanelState::BuildFailed);
    if (IsCurrent(ForBinding))
    {
        OnBuildDone.Broadcast(Reply.Result, Reply.Status, LastBuildId);
    }
}

void UConstructModPanel::Deploy()
{
    if (!CanDeploy() || !BeginMutation(EMutation::Deploy))
    {
        return;
    }
    const uint64 ForBinding = Binding, ForMutation = Mutation;
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    const auto Complete = [WeakThis, ForBinding, ForMutation](const FCrowdyModInfo& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->DeployReturned(ForBinding, ForMutation, Reply);
        }
    };
    if (UCrowdyMods* SDK = Mods(); SDK && CanUseSession())
    {
        SDK->DeployMod(GridId, ModName, LastBuildId, Complete);
    }
    else
    {
        FCrowdyModInfo Reply;
        Reply.Result = ECrowdyModResult::NotSignedIn;
        Reply.Message = TEXT("Sign in to deploy a mod.");
        Complete(Reply);
    }
}

void UConstructModPanel::DeployReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModInfo& Reply)
{
    if (!IsMutationCurrent(ForBinding, ForMutation) || MutationKind != EMutation::Deploy)
    {
        return;
    }
    MutationKind = EMutation::None;
    if (Reply.Result == ECrowdyModResult::Success)
    {
        bRunningFromList = false;
        ApplyMod(Reply.Mod);
    }
    else
    {
        ShowError(Reply.Result, Reply.Message, true);
    }
    if (IsCurrent(ForBinding))
    {
        OnDeployed.Broadcast(Reply.Result, Reply.Result == ECrowdyModResult::Success ? Reply.Mod.Version : 0, Reply.Result == ECrowdyModResult::Success && Reply.Mod.bEnabled);
        if (IsCurrent(ForBinding) && Reply.Result == ECrowdyModResult::Success)
        {
            ReadStatus();
        }
    }
}

void UConstructModPanel::SetEnabled(bool bEnabled)
{
    if (!CanToggle(bEnabled) || !BeginMutation(bEnabled ? EMutation::Enable : EMutation::Disable))
    {
        return;
    }
    const uint64 ForBinding = Binding, ForMutation = Mutation;
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    const auto Complete = [WeakThis, ForBinding, ForMutation, bEnabled](const FCrowdyModInfo& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get(); Self && Self->EnabledReturned(ForBinding, ForMutation, bEnabled, Reply))
        {
            Self->SendWake(ForBinding, ForMutation);
        }
    };
    if (UCrowdyMods* SDK = Mods(); SDK && CanUseSession())
    {
        SDK->SetModEnabled(GridId, ModName, bEnabled, Complete);
    }
    else
    {
        FCrowdyModInfo Reply;
        Reply.Result = ECrowdyModResult::NotSignedIn;
        Reply.Message = TEXT("Sign in to switch a mod.");
        Complete(Reply);
    }
}

bool UConstructModPanel::EnabledReturned(uint64 ForBinding, uint64 ForMutation, bool bEnabled, const FCrowdyModInfo& Reply)
{
    if (!IsMutationCurrent(ForBinding, ForMutation) || bEnableAnswered || MutationKind != (bEnabled ? EMutation::Enable : EMutation::Disable))
    {
        return false;
    }
    bEnableAnswered = true;
    if (Reply.Result != ECrowdyModResult::Success)
    {
        MutationKind = EMutation::None;
        ShowError(Reply.Result, Reply.Message, true);
        if (IsCurrent(ForBinding))
        {
            OnEnabledChanged.Broadcast(Reply.Result, false, false);
        }
        return false;
    }
    bWakePending = bEnabled && Reply.Mod.bEnabled;
    if (!bWakePending)
    {
        MutationKind = EMutation::None;
    }
    bRunningFromList = false;
    ApplyMod(Reply.Mod);
    if (!IsCurrent(ForBinding))
    {
        return false;
    }
    OnEnabledChanged.Broadcast(Reply.Result, Reply.Mod.bEnabled, Reply.Mod.bRunning);
    const bool bSendWake = IsMutationCurrent(ForBinding, ForMutation) && bWakePending;
    if (!bSendWake && IsCurrent(ForBinding))
    {
        ReadStatus();
    }
    return bSendWake;
}

void UConstructModPanel::SendWake(uint64 ForBinding, uint64 ForMutation)
{
    if (!IsMutationCurrent(ForBinding, ForMutation) || !bWakePending)
    {
        return;
    }
    UCrowdyMods* SDK = Mods();
    if (!SDK || !CanUseSession())
    {
        CloseBinding();
        return;
    }
    const FString Method = WakeMethod();
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    SDK->CallMod(GridId, ModName, Method, TEXT("{}"), [WeakThis, ForBinding, ForMutation](const FCrowdyModCall& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->WakeReturned(ForBinding, ForMutation, Reply);
        }
    });
}

void UConstructModPanel::WakeReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModCall& Reply)
{
    if (!IsMutationCurrent(ForBinding, ForMutation) || MutationKind != EMutation::Enable || !bWakePending)
    {
        return;
    }
    bWakePending = false;
    MutationKind = EMutation::None;
    Notice = Reply.Result == ECrowdyModResult::Success ? Reply.ReplyJson
        : FString::Printf(TEXT("%s: %s"), *StaticEnum<ECrowdyModResult>()->GetNameStringByValue(int64(Reply.Result)), *Reply.Message);
    OnWakeDone.Broadcast(Reply.Result, Reply.Message);
    if (IsCurrent(ForBinding))
    {
        ReadStatus();
    }
}

void UConstructModPanel::Delete()
{
    if (!CanDelete() || !BeginMutation(EMutation::Delete))
    {
        return;
    }
    const uint64 ForBinding = Binding, ForMutation = Mutation;
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    const auto Complete = [WeakThis, ForBinding, ForMutation](const FCrowdyModDelete& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->DeleteReturned(ForBinding, ForMutation, Reply);
        }
    };
    if (UCrowdyMods* SDK = Mods(); SDK && CanUseSession())
    {
        SDK->DeleteMod(GridId, ModName, Complete);
    }
    else
    {
        FCrowdyModDelete Reply;
        Reply.Result = ECrowdyModResult::NotSignedIn;
        Reply.Message = TEXT("Sign in to delete a mod.");
        Complete(Reply);
    }
}

void UConstructModPanel::DeleteReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModDelete& Reply)
{
    if (!IsMutationCurrent(ForBinding, ForMutation) || MutationKind != EMutation::Delete)
    {
        return;
    }
    MutationKind = EMutation::None;
    if (Reply.Result == ECrowdyModResult::Success)
    {
        bHasMod = false;
        ListedMods.RemoveAll([this](const FCrowdyMod& Mod) { return Mod.GridId == GridId && Mod.Name == ModName; });
        ShownMod = FCrowdyMod();
        LastBuildId.Reset();
        BuildLog.Reset();
        ChangeState(EConstructModPanelState::NoMod);
    }
    else
    {
        ShowError(Reply.Result, Reply.Message, true);
    }
    if (IsCurrent(ForBinding))
    {
        OnDeleted.Broadcast(Reply.Result);
        if (IsCurrent(ForBinding) && Reply.Result == ECrowdyModResult::Success)
        {
            ReadStatus();
        }
    }
}

void UConstructModPanel::ShowLog()
{
    if (!CanLog())
    {
        return;
    }
    if (State == EConstructModPanelState::Built)
    {
        LogText = BuildLog;
        CachedLog = FText::FromString(LogText);
        OnLogShown.Broadcast(ECrowdyModResult::Success, BuildLog.IsEmpty() ? 0 : 1);
        return;
    }
    UCrowdyMods* SDK = Mods();
    if (!SDK || !CanUseSession())
    {
        return;
    }
    bLogPending = true;
    const uint64 ForBinding = Binding, Request = ++LogRequest;
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    SDK->GetModLogs(GridId, ModName, 0, [WeakThis, ForBinding, Request](const FCrowdyModLogs& Reply)
    {
        if (UConstructModPanel* Self = WeakThis.Get())
        {
            Self->LogReturned(ForBinding, Request, Reply);
        }
    });
}

void UConstructModPanel::LogReturned(uint64 ForBinding, uint64 Request, const FCrowdyModLogs& Reply)
{
    if (!IsCurrent(ForBinding) || LogRequest != Request)
    {
        return;
    }
    bLogPending = false;
    if (Reply.Result == ECrowdyModResult::Success)
    {
        LogText.Reset();
        for (const FCrowdyModLogLine& Line : Reply.Lines)
        {
            LogText += FString::Printf(TEXT("%lld  [%d]  %s\n"), Line.AtMs, Line.Level, *Line.Text);
        }
    }
    else
    {
        Notice = Reply.Result == ECrowdyModResult::Success ? Reply.Message
        : FString::Printf(TEXT("%s: %s"), *StaticEnum<ECrowdyModResult>()->GetNameStringByValue(int64(Reply.Result)), *Reply.Message);
    }
    CachedLog = FText::FromString(LogText);
    OnLogShown.Broadcast(Reply.Result, Reply.Result == ECrowdyModResult::Success ? Reply.Lines.Num() : 0);
}
void UConstructModPanel::FreezeSources(const TArray<FCrowdyModFile>& Files)
{
    SubmittedFiles = Files;
    SubmittedHashes.Reset();
    for (const FCrowdyModFile& File : SubmittedFiles)
    {
        FConstructModFileHash Hash;
        Hash.Path = File.Path;
        Hash.Sha1 = ConstructModSources::Sha1(File.Content);
        SubmittedHashes.Add(MoveTemp(Hash));
    }
}

FString UConstructModPanel::WakeMethod() const
{
    return SelectedStarter == TEXT("platform-greeter") ? TEXT("visit") : TEXT("state");
}