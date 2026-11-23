#include "ConstructStudioDock.h"

#include "ConstructStudioCaptureRequest.h"
#include "ConstructStudioDockFrames.h"
#include "ConstructStudioDockPlatform.h"
#include "ConstructStudioDockReplies.h"
#include "ConstructStudioObservation.h"
#include "ConstructStudioScriptWatch.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Subsystem/CrowdyModScripts.h"

void UConstructStudioScriptWatch::Start(UCrowdyModScripts& Watched)
{
	Scripts = &Watched;
	Watched.OnModScriptStarted.AddDynamic(this, &UConstructStudioScriptWatch::Started);
	Watched.OnModScriptStopped.AddDynamic(this, &UConstructStudioScriptWatch::Stopped);
}

void UConstructStudioScriptWatch::Stop()
{
	if (UCrowdyModScripts* Watched = Scripts.Get())
	{
		Watched->OnModScriptStarted.RemoveDynamic(this, &UConstructStudioScriptWatch::Started);
		Watched->OnModScriptStopped.RemoveDynamic(this, &UConstructStudioScriptWatch::Stopped);
	}
	Scripts.Reset();
	Dock.Reset();
}

void UConstructStudioScriptWatch::Started(const FCrowdyModScript& Script)
{
	if (UConstructStudioDock* Owner = Dock.Get())
	{
		Owner->ReceiveScriptStarted(Generation, Script.ModId, Script.Digest);
	}
}

void UConstructStudioScriptWatch::Stopped(const FCrowdyModScript& Script, ECrowdyModScriptStop Reason)
{
	if (UConstructStudioDock* Owner = Dock.Get())
	{
		Owner->ReceiveScriptStopped(Generation, Script.ModId, Script.Digest, Reason);
	}
}

#if !UE_BUILD_SHIPPING
bool UConstructStudioDock::SendPrompt(const FString& Text)
{
	if (State != EConstructStudioDockState::Ready)
	{
		return false;
	}
	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("text"), Text);
	Payload->SetStringField(TEXT("mode"), TEXT("queue"));
	return TellAgent(Generation, TEXT("page.prompt"), Payload);
}

bool UConstructStudioDock::IsRunInFlightForChecks() const
{
	return bRunInFlight || BuildReply.IsValid();
}
#endif

void UConstructStudioDock::ReceiveAgentRequest(int64 InGeneration, const FString& Method, const FString& Id, const TSharedPtr<FJsonObject>& Params)
{
	using namespace ConstructStudioDockReplies;
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	if (!ConstructStudioDockFrames::IsAnsweredInUnreal(Method))
	{
		RefuseToAgent(InGeneration, Id, EConstructStudioDockRefusal::NotOffered);
		return;
	}
	if (!HoldRequest(InGeneration, Id))
	{
		return;
	}
	FString Text;
	FString Template;
	if (Method.Equals(TEXT("studio.screenshot"), ESearchCase::CaseSensitive))
	{
		const bool bLabelled = ParamString(Params, TEXT("label"), 128, Text) && !Text.IsEmpty();
		ReceiveScreenshotRequest(InGeneration, bLabelled ? Text : FString(TEXT("Agent screenshot")), Id);
		return;
	}
	if (Method.Equals(TEXT("studio.draftTest"), ESearchCase::CaseSensitive) || Method.Equals(TEXT("studio.deployLive"), ESearchCase::CaseSensitive))
	{
		const bool bLive = Method.Equals(TEXT("studio.deployLive"), ESearchCase::CaseSensitive);
		ReceiveDeployRequest(InGeneration, bLive ? EConstructStudioDeployKind::Live : EConstructStudioDeployKind::DraftTest, Id);
		return;
	}
	if (Method.Equals(TEXT("studio.runtimeStatus"), ESearchCase::CaseSensitive))
	{
		AnswerRuntimeStatus(InGeneration, Id);
		return;
	}
	if (Method.Equals(TEXT("studio.runtimeLogs"), ESearchCase::CaseSensitive))
	{
		AnswerRuntimeLogs(InGeneration, Id, ParamLimit(Params, 40));
		return;
	}
	if (Method.Equals(TEXT("studio.clientLogs"), ESearchCase::CaseSensitive))
	{
		AnswerClientLogs(InGeneration, Id, ParamLimit(Params, 80));
		return;
	}
	if (Method.Equals(TEXT("studio.projectList"), ESearchCase::CaseSensitive))
	{
		AnswerProjectList(InGeneration, Id);
		return;
	}
	if (Method.Equals(TEXT("studio.projectOpen"), ESearchCase::CaseSensitive))
	{
		OpenProject(InGeneration, Id, ParamString(Params, TEXT("projectId"), 128, Text) ? Text : FString());
		return;
	}
	if (Method.Equals(TEXT("studio.projectCreate"), ESearchCase::CaseSensitive))
	{
		ParamString(Params, TEXT("template"), 16, Template);
		CreateProject(InGeneration, Id, ParamString(Params, TEXT("name"), 64, Text) ? Text.TrimStartAndEnd() : FString(), Template);
		return;
	}
	LookUpGrid(InGeneration, Id, Method.Equals(TEXT("game.observe"), ESearchCase::CaseSensitive));
}

bool UConstructStudioDock::HoldRequest(int64 InGeneration, const FString& Id)
{
	// The first request with an id is the one answered; a second with the same id waits on nothing.
	if (HeldRequests.Contains(Id))
	{
		return false;
	}
	if (HeldRequests.Num() >= MaxHeldRequests)
	{
		RefuseToAgent(InGeneration, Id, EConstructStudioDockRefusal::Failed, TEXT("Too many requests are waiting for the game. Try again shortly."));
		return false;
	}
	HeldRequests.Add(Id);
	return true;
}

bool UConstructStudioDock::AnswerRequest(int64 InGeneration, const FString& Id, const TSharedRef<FJsonObject>& Result)
{
	if (!IsCurrent(InGeneration) || HeldRequests.Remove(Id) == 0)
	{
		return false;
	}
	return ReplyToAgent(InGeneration, Id, Result);
}

bool UConstructStudioDock::FailRequest(int64 InGeneration, const FString& Id, const FString& Message)
{
	if (!IsCurrent(InGeneration) || HeldRequests.Remove(Id) == 0)
	{
		return false;
	}
	return RefuseToAgent(InGeneration, Id, EConstructStudioDockRefusal::Failed, Message);
}

void UConstructStudioDock::RefuseHeldRequests(const TCHAR* Message)
{
	const TArray<FString> Held = MoveTemp(HeldRequests);
	HeldRequests.Reset();
	for (const FString& Id : Held)
	{
		RefuseToAgent(Generation, Id, EConstructStudioDockRefusal::Failed, Message);
	}
}

void UConstructStudioDock::ResetRequests()
{
	HeldRequests.Reset();
	ObservedClaims.Reset();
	ForgetDeploy();
}

void UConstructStudioDock::CheckReplacedMod()
{
	// A CLIENT-only project's mod is named for its CLIENT module.
	const FString ModName = CurrentServerModule.IsEmpty() ? CurrentClientModule : CurrentServerModule;
	if (ModName.IsEmpty())
	{
		RaisePrompt(DeployKind);
		return;
	}
	if (!Platform.IsValid() || !Platform->ListMods || !Platform->GetProjectStatus)
	{
		FailDeploy(ConstructStudioDockReplies::NotReachableText);
		return;
	}
	bDeployChecking = true;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const int64 Checking = Generation;
	Platform->ListMods(CurrentGridId, [Weak, Checking, RequestId = DeployRequestId, GridId = CurrentGridId, ModName](const FCrowdyModList& List)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveGridMods(Checking, RequestId, GridId, ModName, List);
		}
	});
}

void UConstructStudioDock::ReceiveGridMods(int64 InGeneration, const FString& RequestId, int64 GridId, const FString& ModName, const FCrowdyModList& List)
{
	if (!IsCurrent(InGeneration) || !bDeployChecking || !RequestId.Equals(DeployRequestId, ESearchCase::CaseSensitive))
	{
		return;
	}
	// The player moved to another grid meanwhile, and the prompt will name that one.
	if (GridId != CurrentGridId)
	{
		CheckReplacedMod();
		return;
	}
	if (List.Result != ECrowdyModResult::Success)
	{
		FailDeploy(List.Message.IsEmpty() ? FString(TEXT("The grid's mods could not be read, so the deploy was not offered.")) : List.Message);
		return;
	}
	// The platform links no mod to a project, and a mod of the player's own is theirs to replace; only another player's is refused.
	const FCrowdyMod* Found = List.Mods.FindByPredicate([&ModName](const FCrowdyMod& Mod) { return Mod.Name.Equals(ModName, ESearchCase::CaseSensitive); });
	if (Found && Found->OwnerUserId != 0 && Found->OwnerUserId != BootUser)
	{
		Send(DeployKind == EConstructStudioDeployKind::Live ? TEXT("studio.deployLive") : TEXT("studio.draftTest"), TEXT("refused: replaces a mod"));
		FailDeploy(FString::Printf(TEXT("This grid has a mod named %s that another player owns, and deploying would replace it. Use a project with another module name."), *ModName));
		return;
	}
	FinishReplacedCheck(ModName);
}

void UConstructStudioDock::FinishReplacedCheck(const FString& ModName)
{
	bDeployChecking = false;
	const FString ModNow = CurrentServerModule.IsEmpty() ? CurrentClientModule : CurrentServerModule;
	// A save during the check may have moved the revision or renamed the module: read the project again and check anew.
	if (bProjectDirty || bProjectReadInFlight || !ModNow.Equals(ModName, ESearchCase::CaseSensitive))
	{
		bDeployReading = true;
		if (!bProjectReadInFlight)
		{
			bProjectDirty = false;
			ReadProject();
		}
		return;
	}
	RaisePrompt(DeployKind);
}

void UConstructStudioDock::FailDeploy(const FString& Message)
{
	FailRequest(Generation, DeployRequestId, Message);
	ForgetDeploy();
}

void UConstructStudioDock::AnswerDeploy(const TSharedRef<FJsonObject>& Result)
{
	AnswerRequest(Generation, DeployRequestId, Result);
	ForgetDeploy();
}

void UConstructStudioDock::ForgetDeploy()
{
	DeployRequestId.Reset();
	DeployRequestAge = 0.0;
	bDeployReading = false;
	bDeployChecking = false;
	BuildReply.Reset();
	AwaitedClientModId.Reset();
	AwaitedClientDigest.Reset();
	AwaitedClientGridId = 0;
	ClientWaitAge = 0.0;
	bServerModReading = false;
	bAwaitingClient = false;
	bClientStatusReading = false;
	DropRunCapture();
	DropScriptWatch();
}

void UConstructStudioDock::ReceiveServerMod(int64 InGeneration, const FString& RequestId, const FString& ModId, const FString& Module, const FCrowdyModList& List)
{
	if (!IsCurrent(InGeneration) || !bServerModReading || !BuildReply.IsValid() || !RequestId.Equals(DeployRequestId, ESearchCase::CaseSensitive))
	{
		return;
	}
	bServerModReading = false;
	// A list that cannot be read keeps the run ok, but the SERVER half is not called running.
	const FCrowdyMod* Mod = List.Result != ECrowdyModResult::Success ? nullptr : List.Mods.FindByPredicate([&ModId, &Module](const FCrowdyMod& Each)
	{
		return ModId.IsEmpty() ? Each.Name.Equals(Module, ESearchCase::CaseSensitive) : Each.ModId.Equals(ModId, ESearchCase::CaseSensitive);
	});
	if (Mod)
	{
		ConstructStudioDockReplies::ApplyServerMod(*BuildReply, *Mod);
	}
	ContinueBuild();
}

void UConstructStudioDock::ContinueBuild()
{
	if (!BuildReply.IsValid())
	{
		return;
	}
	// The CLIENT half is awaited, and shown in a screenshot, only when every half deployed and the SERVER mod may run.
	if (!ConstructStudioDockReplies::IsOk(*BuildReply) || AwaitedClientModId.IsEmpty())
	{
		AnswerBuild();
		return;
	}
	WaitForClient();
}

void UConstructStudioDock::AnswerBuild()
{
	if (!BuildReply.IsValid())
	{
		return;
	}
	ConstructStudioDockReplies::Settle(*BuildReply);
	AnswerDeploy(BuildReply.ToSharedRef());
}

void UConstructStudioDock::WaitForClient()
{
	ClientWaitAge = 0.0;
	bAwaitingClient = true;
	DropScriptWatch();
	UWorld* World = GetWorld();
	UCrowdyModScripts* Scripts = World ? World->GetSubsystem<UCrowdyModScripts>() : nullptr;
	// Heard before looking, so a start between the look and the binding is not missed.
	if (Scripts)
	{
		ScriptWatch = NewObject<UConstructStudioScriptWatch>(this);
		ScriptWatch->Dock = this;
		ScriptWatch->Generation = Generation;
		ScriptWatch->Start(*Scripts);
	}
	// An unchanged CLIENT crate is not restarted, so it may already run here and never start again.
	if (ConstructStudioDockReplies::IsHalfRunning(*BuildReply, ECrowdyStudioTarget::Client) || IsAwaitedClientRunning())
	{
		ClientStarted();
	}
}

bool UConstructStudioDock::IsAwaitedScript(const FString& ModId, const FString& Digest) const
{
	if (ModId.IsEmpty() || !ModId.Equals(AwaitedClientModId, ESearchCase::CaseSensitive))
	{
		return false;
	}
	// The version that ran before the deploy is not the one just deployed.
	return AwaitedClientDigest.IsEmpty() || Digest.Equals(AwaitedClientDigest, ESearchCase::IgnoreCase);
}

bool UConstructStudioDock::IsAwaitedClientRunning() const
{
	if (!Platform.IsValid() || !Platform->GetRunningScripts)
	{
		return false;
	}
	return Platform->GetRunningScripts().ContainsByPredicate([this](const FCrowdyModScript& Script) { return IsAwaitedScript(Script.ModId, Script.Digest); });
}

void UConstructStudioDock::ClientStarted()
{
	bAwaitingClient = false;
	DropScriptWatch();
	if (!BuildReply.IsValid())
	{
		return;
	}
	ConstructStudioDockReplies::MarkClientRunning(*BuildReply);
	CaptureForRun();
}

void UConstructStudioDock::ClientFailed(const FString& Message)
{
	if (!bAwaitingClient || !BuildReply.IsValid())
	{
		return;
	}
	bAwaitingClient = false;
	DropScriptWatch();
	ConstructStudioDockReplies::MarkClientNotStarted(*BuildReply, Message.IsEmpty() ? FString(TEXT("Deployed, but it failed to start in this game."))
		: FString::Printf(TEXT("Deployed, but it failed to start in this game: %s"), *Message.Left(500)), true);
	AnswerBuild();
}

void UConstructStudioDock::ClientWaitEnded()
{
	// The screenshot took the rest of the wait, so the answer goes without one.
	if (bRunCapturing)
	{
		DropRunCapture();
		AnswerBuild();
		return;
	}
	bAwaitingClient = false;
	DropScriptWatch();
	// Found only now, with no time left for a screenshot.
	if (IsAwaitedClientRunning())
	{
		ConstructStudioDockReplies::MarkClientRunning(*BuildReply);
		AnswerBuild();
		return;
	}
	ConstructStudioDockReplies::MarkClientNotStarted(*BuildReply,
		TEXT("Deployed, but this game did not see it start within 20 seconds; the player may still be asked to trust it, or Mod Scripts may be off."), false);
	AnswerBuild();
}

void UConstructStudioDock::ReceiveScriptStarted(int64 InGeneration, const FString& ModId, const FString& Digest)
{
	if (!IsCurrent(InGeneration) || !bAwaitingClient || !IsAwaitedScript(ModId, Digest))
	{
		return;
	}
	ClientStarted();
}

void UConstructStudioDock::ReceiveScriptStopped(int64 InGeneration, const FString& ModId, const FString& Digest, ECrowdyModScriptStop Why)
{
	// A version that fails to fetch or compile is reported without its digest, so the status read below decides whose failure it was.
	if (!IsCurrent(InGeneration) || !bAwaitingClient || bClientStatusReading || Why != ECrowdyModScriptStop::Failed
		|| ModId.IsEmpty() || !ModId.Equals(AwaitedClientModId, ESearchCase::CaseSensitive))
	{
		return;
	}
	if (!Platform.IsValid() || !Platform->GetScriptStatus)
	{
		ClientFailed(FString());
		return;
	}
	bClientStatusReading = true;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->GetScriptStatus(AwaitedClientGridId, [Weak, InGeneration, RequestId = DeployRequestId, ModId](const FCrowdyModScriptStatusList& List)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveClientStatus(InGeneration, RequestId, ModId, List);
		}
	});
}

void UConstructStudioDock::ReceiveClientStatus(int64 InGeneration, const FString& RequestId, const FString& ModId, const FCrowdyModScriptStatusList& List)
{
	if (!IsCurrent(InGeneration) || !bClientStatusReading || !RequestId.Equals(DeployRequestId, ESearchCase::CaseSensitive))
	{
		return;
	}
	bClientStatusReading = false;
	const FCrowdyModScriptStatus* Found = List.Result != ECrowdyModScriptReadResult::Success ? nullptr
		: List.Scripts.FindByPredicate([this, &ModId](const FCrowdyModScriptStatus& Each)
		{
			return Each.State == ECrowdyModScriptState::Erred && IsAwaitedScript(ModId, Each.Script.Digest);
		});
	// The failure was an older version's, or the read could not tell: keep waiting for the deployed one.
	if (!Found)
	{
		return;
	}
	ClientFailed(Found->Message);
}

void UConstructStudioDock::CaptureForRun()
{
	if (!BuildReply.IsValid())
	{
		return;
	}
	// A missing screenshot is not a failed run: the answer goes without one.
	if (!HostObject.IsValid())
	{
		AnswerBuild();
		return;
	}
	bRunCapturing = true;
	RequestCapture(DeployKind == EConstructStudioDeployKind::Live ? TEXT("After live deploy") : TEXT("After draft test"), false, FString(), true);
}

void UConstructStudioDock::FinishRunCapture(const FConstructStudioCapture& Capture)
{
	if (!BuildReply.IsValid() || !bRunCapturing)
	{
		return;
	}
	bRunCapturing = false;
	if (Capture.bCaptured && !Capture.PngBase64.IsEmpty())
	{
		BuildReply->SetObjectField(TEXT("screenshot"), ConstructStudioDockReplies::Screenshot(Capture, ++CaptureCount));
	}
	AnswerBuild();
}

void UConstructStudioDock::DropRunCapture()
{
	bRunCapturing = false;
	PendingCaptures.RemoveAll([](const TObjectPtr<UConstructStudioCaptureRequest>& Request) { return Request && Request->bForRun; });
}

void UConstructStudioDock::DropScriptWatch()
{
	if (ScriptWatch)
	{
		ScriptWatch->Stop();
	}
	ScriptWatch = nullptr;
}

void UConstructStudioDock::AnswerRuntimeStatus(int64 InGeneration, const FString& Id)
{
	if (CurrentProjectId.IsEmpty())
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NoProjectText);
		return;
	}
	if (!Platform.IsValid() || !Platform->GetProjectStatus)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->GetProjectStatus(CurrentGridId, CurrentProjectId, [Weak, InGeneration, Id](const FCrowdyModProjectStatus& Status)
	{
		UConstructStudioDock* Dock = Weak.Get();
		if (!Dock)
		{
			return;
		}
		if (Status.Result != ECrowdyModResult::Success)
		{
			Dock->FailRequest(InGeneration, Id, Status.Message);
			return;
		}
		Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::RuntimeReply(Status.Targets));
	});
}

void UConstructStudioDock::AnswerRuntimeLogs(int64 InGeneration, const FString& Id, int32 Limit)
{
	// A CLIENT-only project's mod is named for its CLIENT module.
	const FString ModName = CurrentServerModule.IsEmpty() ? CurrentClientModule : CurrentServerModule;
	if (ModName.IsEmpty())
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NoProjectText);
		return;
	}
	if (!Platform.IsValid() || !Platform->GetModLogs)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	// One line more than asked says whether more were held.
	Platform->GetModLogs(CurrentGridId, ModName, Limit + 1, [Weak, InGeneration, Id, ModName, Limit](const FCrowdyModLogs& Logs)
	{
		UConstructStudioDock* Dock = Weak.Get();
		if (!Dock)
		{
			return;
		}
		// A mod never deployed on this grid has logged nothing yet.
		if (Logs.Result == ECrowdyModResult::NotFound)
		{
			Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::NoLines());
			return;
		}
		if (Logs.Result != ECrowdyModResult::Success)
		{
			Dock->FailRequest(InGeneration, Id, Logs.Message);
			return;
		}
		Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::ModLog(Logs.Lines, ModName, Limit));
	});
}

void UConstructStudioDock::AnswerClientLogs(int64 InGeneration, const FString& Id, int32 Limit)
{
	if (CurrentProjectId.IsEmpty())
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NoProjectText);
		return;
	}
	if (CurrentClientModule.IsEmpty())
	{
		AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::NoLines());
		return;
	}
	if (!Platform.IsValid() || !Platform->GetProjectStatus || !Platform->GetScriptLog)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->GetProjectStatus(CurrentGridId, CurrentProjectId, [Weak, InGeneration, Id, Limit](const FCrowdyModProjectStatus& Status)
	{
		UConstructStudioDock* Dock = Weak.Get();
		if (!Dock)
		{
			return;
		}
		if (Status.Result != ECrowdyModResult::Success)
		{
			Dock->FailRequest(InGeneration, Id, Status.Message);
			return;
		}
		const FCrowdyModRunTarget* Client = Status.Targets.FindByPredicate([](const FCrowdyModRunTarget& Target) { return Target.Target == ECrowdyStudioTarget::Client; });
		if (!Client || Client->ModId.IsEmpty())
		{
			Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::NoLines());
			return;
		}
		Dock->ReadClientLog(InGeneration, Id, Limit, Client->ModId);
	});
}

void UConstructStudioDock::ReadClientLog(int64 InGeneration, const FString& Id, int32 Limit, const FString& ModId)
{
	if (!IsCurrent(InGeneration) || !Platform.IsValid() || !Platform->GetScriptLog)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->GetScriptLog(CurrentGridId, ModId, [Weak, InGeneration, Id, Limit](const FCrowdyModScriptLog& Log)
	{
		UConstructStudioDock* Dock = Weak.Get();
		if (!Dock)
		{
			return;
		}
		// Outside the grid, or a grid that does not serve the CLIENT half, nothing runs here to log.
		if (Log.Result == ECrowdyModScriptReadResult::NotInGrid)
		{
			Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::NoLines());
			return;
		}
		if (Log.Result != ECrowdyModScriptReadResult::Success)
		{
			Dock->FailRequest(InGeneration, Id, Log.Message);
			return;
		}
		// A script's lines are its author's text; only the player's own reach the model.
		if (Log.AuthorId != Dock->BootUser)
		{
			Dock->FailRequest(InGeneration, Id, TEXT("These CLIENT logs are another author's, so they are not passed to the agent."));
			return;
		}
		Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::ScriptLog(Log, Limit));
	});
}

void UConstructStudioDock::AnswerProjectList(int64 InGeneration, const FString& Id)
{
	if (!Platform.IsValid() || !Platform->ListProjects)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->ListProjects([Weak, InGeneration, Id](const FCrowdyStudioProjectList& List)
	{
		UConstructStudioDock* Dock = Weak.Get();
		if (!Dock)
		{
			return;
		}
		if (List.Result != ECrowdyStudioResult::Success)
		{
			Dock->FailRequest(InGeneration, Id, List.Message);
			return;
		}
		Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::ProjectList(List.Projects, Dock->CurrentProjectId, List.bMore));
	});
}

void UConstructStudioDock::OpenProject(int64 InGeneration, const FString& Id, const FString& ProjectId)
{
	if (ProjectId.IsEmpty())
	{
		FailRequest(InGeneration, Id, TEXT("studio.projectOpen needs a projectId."));
		return;
	}
	if (!Platform.IsValid() || !Platform->GetProject)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->GetProject(ProjectId, [Weak, InGeneration, Id](const FCrowdyStudioProjectInfo& Info)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ProjectOpened(InGeneration, Id, Info);
		}
	});
}

void UConstructStudioDock::CreateProject(int64 InGeneration, const FString& Id, const FString& Name, const FString& Template)
{
	if (Name.IsEmpty())
	{
		FailRequest(InGeneration, Id, TEXT("studio.projectCreate needs a name."));
		return;
	}
	if (!Platform.IsValid() || !Platform->CreateProject)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->CreateProject(Name, ConstructStudioDockReplies::KindForTemplate(Template), [Weak, InGeneration, Id](const FCrowdyStudioProjectInfo& Info)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ProjectOpened(InGeneration, Id, Info);
		}
	});
}

void UConstructStudioDock::ProjectOpened(int64 InGeneration, const FString& Id, const FCrowdyStudioProjectInfo& Info)
{
	if (!IsCurrent(InGeneration) || State == EConstructStudioDockState::Failed)
	{
		return;
	}
	if (Info.Result != ECrowdyStudioResult::Success || Info.Project.ProjectId.IsEmpty())
	{
		FailRequest(InGeneration, Id, Info.Message);
		return;
	}
	CurrentServerModule = Info.Project.ServerModule;
	CurrentClientModule = Info.Project.ClientModule;
	ReceiveProject(InGeneration, Info.Project.ProjectId, Info.Project.Name, Info.Project.Revision, !Info.Project.ClientModule.IsEmpty());
	TellAgent(InGeneration, TEXT("page.project"), ConstructStudioDockReplies::ProjectEvent(Info.Project));
	AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::ProjectReply(Info.Project));
}

void UConstructStudioDock::LookUpGrid(int64 InGeneration, const FString& Id, bool bObserve)
{
	const TOptional<FInt64Vector> Chunk = PlayerChunk();
	if (!Chunk.IsSet() && bObserve)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NothingToObserveText);
		return;
	}
	if (!Chunk.IsSet())
	{
		AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::NoGridReply());
		return;
	}
	const bool bSameLookup = ObservedClaims.IsValid() && ObservedClaimChunk == Chunk.GetValue() && ObservedClaimGridId == CurrentGridId;
	if (!bSameLookup)
	{
		ObservedClaims.Reset();
	}
	// The agent observes often, and a claim does not move while the player stays in its chunk.
	if (bObserve && bSameLookup && ObservedClaimAge < ClaimReuseSeconds)
	{
		ReceiveGridClaim(InGeneration, Id, true, *ObservedClaims);
		return;
	}
	if (!Platform.IsValid() || !Platform->FindClaimAt)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->FindClaimAt(Chunk.GetValue(), [Weak, InGeneration, Id, bObserve, Looked = Chunk.GetValue(), GridId = CurrentGridId](const FCrowdyClaimSearch& Search)
	{
		UConstructStudioDock* Dock = Weak.Get();
		if (!Dock)
		{
			return;
		}
		if (bObserve)
		{
			Dock->KeepObservedClaims(InGeneration, Looked, GridId, Search);
		}
		Dock->ReceiveGridClaim(InGeneration, Id, bObserve, Search);
	});
}

void UConstructStudioDock::KeepObservedClaims(int64 InGeneration, const FInt64Vector& Chunk, int64 GridId, const FCrowdyClaimSearch& Search)
{
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	if (Search.Result != ECrowdyClaimResult::Success)
	{
		ObservedClaims.Reset();
		return;
	}
	ObservedClaims = MakeShared<FCrowdyClaimSearch>(Search);
	ObservedClaimChunk = Chunk;
	ObservedClaimGridId = GridId;
	ObservedClaimAge = 0.0;
}

void UConstructStudioDock::ReceiveGridClaim(int64 InGeneration, const FString& Id, bool bObserve, const FCrowdyClaimSearch& Search)
{
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	// Only the dock's own grid counts; a claim on any other grid at the player's chunk is not the one the agent works in.
	const int64 GridId = CurrentGridId;
	const FCrowdyChunkClaimInfo* Claim = Search.Result != ECrowdyClaimResult::Success ? nullptr
		: Search.Claims.FindByPredicate([GridId](const FCrowdyChunkClaimInfo& Each) { return Each.GridId == GridId; });
	TOptional<FConstructStudioObservedGrid> Grid;
	if (Claim)
	{
		FConstructStudioObservedGrid& Shown = Grid.Emplace();
		Shown.GridId = Claim->GridId;
		Shown.Low = FInt64Vector(Claim->ChunkX, Claim->ChunkY, Claim->ChunkZ);
		Shown.High = Shown.Low;
		Shown.bOwned = Claim->bOwnedByMe;
	}
	// An observation without the grid is still worth answering; grid.context is not.
	if (bObserve)
	{
		AnswerObservation(InGeneration, Id, Grid);
		return;
	}
	if (Search.Result != ECrowdyClaimResult::Success)
	{
		FailRequest(InGeneration, Id, Search.Message);
		return;
	}
	if (!Claim)
	{
		AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::NoGridReply());
		return;
	}
	if (!Platform.IsValid() || !Platform->ListGridChannels)
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NotReachableText);
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const int64 AppId = AgentBoot.IsValid() ? AgentBoot->AppId : 0;
	const FCrowdyChunkClaimInfo Found = *Claim;
	Platform->ListGridChannels(Found.GridId, [Weak, InGeneration, Id, AppId, Found](const FCrowdyGridChannels& Channels)
	{
		UConstructStudioDock* Dock = Weak.Get();
		if (!Dock)
		{
			return;
		}
		if (Channels.Result != ECrowdyClaimResult::Success)
		{
			Dock->FailRequest(InGeneration, Id, Channels.Message);
			return;
		}
		Dock->AnswerRequest(InGeneration, Id, ConstructStudioDockReplies::GridReply(AppId, Found, Channels.Channels));
	});
}

void UConstructStudioDock::AnswerObservation(int64 InGeneration, const FString& Id, const TOptional<FConstructStudioObservedGrid>& Grid)
{
	const TSharedPtr<FJsonObject> Observation = Observe(Grid);
	if (!Observation.IsValid())
	{
		FailRequest(InGeneration, Id, ConstructStudioDockReplies::NothingToObserveText);
		return;
	}
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetObjectField(TEXT("observation"), Observation);
	Reply->SetStringField(TEXT("capturedAt"), FDateTime::UtcNow().ToIso8601());
	AnswerRequest(InGeneration, Id, Reply);
}

TOptional<FInt64Vector> UConstructStudioDock::PlayerChunk() const
{
	if (ChunkForTests.IsSet())
	{
		return ChunkForTests;
	}
	const UWorld* World = GetWorld();
	const UConstructStudioObservation* Observation = World ? World->GetSubsystem<UConstructStudioObservation>() : nullptr;
	return Observation ? Observation->GetPlayerChunk() : TOptional<FInt64Vector>();
}

TSharedPtr<FJsonObject> UConstructStudioDock::Observe(const TOptional<FConstructStudioObservedGrid>& Grid) const
{
	if (ObserveForTests)
	{
		return ObserveForTests(Grid);
	}
	const UWorld* World = GetWorld();
	const UConstructStudioObservation* Observation = World ? World->GetSubsystem<UConstructStudioObservation>() : nullptr;
	return Observation ? Observation->Observe(Grid) : nullptr;
}
