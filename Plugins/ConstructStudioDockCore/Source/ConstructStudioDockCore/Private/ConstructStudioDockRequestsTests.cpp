#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioDock.h"
#include "ConstructStudioDockBridge.h"
#include "ConstructStudioDockPlatform.h"
#include "ConstructStudioDockReplies.h"
#include "ConstructStudioDockScheme.h"
#include "ConstructStudioDockTestAccess.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ConstructStudioDockRequestsTests
{
	constexpr EAutomationTestFlags ReqFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using FReqAccess = FConstructStudioDockTestAccess;
	using FReqRig = FConstructStudioDockTestRig;

	template <typename T>
	using TReqHeld = TArray<TFunction<void(const T&)>>;

	/** Gives Value to the oldest held answer; false when none is held. */
	template <typename T>
	bool Give(TReqHeld<T>& Held, const T& Value)
	{
		if (Held.IsEmpty())
		{
			return false;
		}
		TFunction<void(const T&)> OnDone = MoveTemp(Held[0]);
		Held.RemoveAt(0);
		OnDone(Value);
		return true;
	}

	/** Gives Value to the newest held answer; false when none is held. */
	template <typename T>
	bool GiveNewest(TReqHeld<T>& Held, const T& Value)
	{
		if (Held.IsEmpty())
		{
			return false;
		}
		TFunction<void(const T&)> OnDone = Held.Pop();
		OnDone(Value);
		return true;
	}

	/** Records each SDK call the dock makes as a line and holds its answer. Declare it before the rig it serves. */
	struct FReqFake
	{
		TSharedRef<FConstructStudioDockPlatform> Calls = MakeShared<FConstructStudioDockPlatform>();
		TArray<FString> Log;
		TReqHeld<FCrowdyStudioAgentBoot> Boots;
		TReqHeld<FCrowdyStudioConsent> Consents;
		TReqHeld<FCrowdyStudioModelUsage> Usages;
		TReqHeld<FCrowdyStudioLiveModding> Verdicts;
		TReqHeld<FCrowdyModRun> Runs;
		TReqHeld<FCrowdyStudioProjectList> Lists;
		TReqHeld<FCrowdyStudioProjectInfo> Projects;
		TReqHeld<FCrowdyModProjectStatus> Statuses;
		TReqHeld<FCrowdyModLogs> ModLogs;
		TReqHeld<FCrowdyModScriptLog> ScriptLogs;
		TReqHeld<FCrowdyClaimSearch> Claims;
		TReqHeld<FCrowdyGridChannels> Channels;
		TReqHeld<FCrowdyModList> ModLists;
		TReqHeld<FCrowdyModScriptStatusList> ScriptStatuses;

		/** The grid's mods, answered at once to each ListMods unless bHoldMods. */
		FCrowdyModList GridMods;
		bool bHoldMods = false;

		/** The Mod Scripts running in this game. */
		TArray<FCrowdyModScript> Running;

		FReqFake()
		{
			GridMods.Result = ECrowdyModResult::Success;
			Calls->ListMods = [this](int64 GridId, TFunction<void(const FCrowdyModList&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("Mods:%lld"), GridId));
				if (bHoldMods)
				{
					ModLists.Add(MoveTemp(OnDone));
					return;
				}
				OnDone(GridMods);
			};
			Calls->GetScriptStatus = [this](int64 GridId, TFunction<void(const FCrowdyModScriptStatusList&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("ScriptStatus:%lld"), GridId));
				ScriptStatuses.Add(MoveTemp(OnDone));
			};
			Calls->GetRunningScripts = [this]() { return Running; };
			Calls->GetAgentBoot = [this](TFunction<void(const FCrowdyStudioAgentBoot&)> OnDone) { Log.Add(TEXT("Boot")); Boots.Add(MoveTemp(OnDone)); };
			Calls->GetProviderConsent = [this](TFunction<void(const FCrowdyStudioConsent&)> OnDone) { Log.Add(TEXT("Consent")); Consents.Add(MoveTemp(OnDone)); };
			Calls->RecordProviderConsent = [this](TFunction<void(const FCrowdyStudioConsent&)> OnDone) { Log.Add(TEXT("RecordConsent")); Consents.Add(MoveTemp(OnDone)); };
			Calls->GetConsentNotice = []() { return FText::FromString(TEXT("The provider notice")); };
			Calls->GetModelUsage = [this](TFunction<void(const FCrowdyStudioModelUsage&)> OnDone) { Log.Add(TEXT("Usage")); Usages.Add(MoveTemp(OnDone)); };
			Calls->AuthorizeLiveModding = [this](ECrowdyStudioRunMode Mode, TFunction<void(const FCrowdyStudioLiveModding&)> OnDone)
			{
				Log.Add(TEXT("Authorize:") + StaticEnum<ECrowdyStudioRunMode>()->GetNameStringByValue(static_cast<int64>(Mode)));
				Verdicts.Add(MoveTemp(OnDone));
			};
			Calls->RunProject = [this](int64 GridId, const FString& ProjectId, const FString& Revision, ECrowdyStudioRunMode Mode, const FString& ServerModule,
				const FString& ClientModule, TFunction<void(const FCrowdyModRun&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("Run:%lld:%s:%s:%s:%s:%s"), GridId, *ProjectId, *Revision,
					*StaticEnum<ECrowdyStudioRunMode>()->GetNameStringByValue(static_cast<int64>(Mode)), *ServerModule, *ClientModule));
				Runs.Add(MoveTemp(OnDone));
			};
			Calls->ListProjects = [this](TFunction<void(const FCrowdyStudioProjectList&)> OnDone) { Log.Add(TEXT("ListProjects")); Lists.Add(MoveTemp(OnDone)); };
			Calls->GetProject = [this](const FString& ProjectId, TFunction<void(const FCrowdyStudioProjectInfo&)> OnDone)
			{
				Log.Add(TEXT("Project:") + ProjectId);
				Projects.Add(MoveTemp(OnDone));
			};
			Calls->CreateProject = [this](const FString& Name, ECrowdyStudioProjectKind Kind, TFunction<void(const FCrowdyStudioProjectInfo&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("Create:%s:%s"), *Name, *StaticEnum<ECrowdyStudioProjectKind>()->GetNameStringByValue(static_cast<int64>(Kind))));
				Projects.Add(MoveTemp(OnDone));
			};
			Calls->GetProjectStatus = [this](int64 GridId, const FString& ProjectId, TFunction<void(const FCrowdyModProjectStatus&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("Status:%lld:%s"), GridId, *ProjectId));
				Statuses.Add(MoveTemp(OnDone));
			};
			Calls->GetModLogs = [this](int64 GridId, const FString& ModName, int32 Limit, TFunction<void(const FCrowdyModLogs&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("Logs:%lld:%s:%d"), GridId, *ModName, Limit));
				ModLogs.Add(MoveTemp(OnDone));
			};
			Calls->GetScriptLog = [this](int64 GridId, const FString& ModId, TFunction<void(const FCrowdyModScriptLog&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("ScriptLog:%lld:%s"), GridId, *ModId));
				ScriptLogs.Add(MoveTemp(OnDone));
			};
			Calls->FindClaimAt = [this](const FInt64Vector& Chunk, TFunction<void(const FCrowdyClaimSearch&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("Claim:%lld:%lld:%lld"), Chunk.X, Chunk.Y, Chunk.Z));
				Claims.Add(MoveTemp(OnDone));
			};
			Calls->ListGridChannels = [this](int64 GridId, TFunction<void(const FCrowdyGridChannels&)> OnDone)
			{
				Log.Add(FString::Printf(TEXT("Channels:%lld"), GridId));
				Channels.Add(MoveTemp(OnDone));
			};
		}

		FReqFake(const FReqFake&) = delete;
		FReqFake& operator=(const FReqFake&) = delete;

		int32 Count(const FString& Line) const
		{
			return Log.FilterByPredicate([&Line](const FString& Each) { return Each.Equals(Line, ESearchCase::CaseSensitive); }).Num();
		}

		int32 CountStarting(const FString& Prefix) const
		{
			return Log.FilterByPredicate([&Prefix](const FString& Each) { return Each.StartsWith(Prefix, ESearchCase::CaseSensitive); }).Num();
		}
	};

	FCrowdyStudioAgentBoot ReqBoot()
	{
		FCrowdyStudioAgentBoot Boot;
		Boot.Result = ECrowdyStudioResult::Success;
		Boot.AppId = 9;
		Boot.GraphQLUrl = TEXT("https://api.example.test/graphql");
		Boot.ModelBaseUrl = TEXT("https://api.example.test");
		Boot.ConnectOrigins = {TEXT("https://api.example.test")};
		Boot.StudioOrigin = TEXT("https://studio.example.test");
		FCrowdyStudioModel Model;
		Model.ModelId = TEXT("model-1");
		Boot.Models.Add(Model);
		return Boot;
	}

	FCrowdyStudioProject ReqProject(const FString& ProjectId, const FString& Revision, const FString& ClientModule = TEXT("srv-client"))
	{
		FCrowdyStudioProject Made;
		Made.ProjectId = ProjectId;
		Made.Name = TEXT("Fountain");
		Made.Revision = Revision;
		Made.Kind = ClientModule.IsEmpty() ? ECrowdyStudioProjectKind::Server : ECrowdyStudioProjectKind::Paired;
		Made.ServerModule = TEXT("srv");
		Made.ClientModule = ClientModule;
		Made.UpdatedAtMs = 1763373600000;
		return Made;
	}

	FCrowdyStudioProjectInfo ReqInfo(const FCrowdyStudioProject& Project, ECrowdyStudioResult Result = ECrowdyStudioResult::Success, const FString& Message = FString())
	{
		FCrowdyStudioProjectInfo Info;
		Info.Result = Result;
		Info.Project = Project;
		Info.Message = Message;
		return Info;
	}

	FCrowdyStudioProjectList ReqList(const TArray<FCrowdyStudioProject>& Projects, ECrowdyStudioResult Result = ECrowdyStudioResult::Success)
	{
		FCrowdyStudioProjectList List;
		List.Result = Result;
		List.Projects = Projects;
		List.Message = Result == ECrowdyStudioResult::Success ? FString() : FString(TEXT("The project list could not be read."));
		return List;
	}

	FCrowdyStudioLiveModding ReqVerdict(bool bAllowed)
	{
		FCrowdyStudioLiveModding Verdict;
		Verdict.Result = ECrowdyStudioResult::Success;
		Verdict.bAllowed = bAllowed;
		return Verdict;
	}

	FCrowdyModRunTarget ReqTarget(ECrowdyStudioTarget Target, ECrowdyModRunPhase Phase, const FString& ModId = FString(), const FString& Digest = FString())
	{
		FCrowdyModRunTarget Made;
		Made.Target = Target;
		Made.Phase = Phase;
		Made.Module = Target == ECrowdyStudioTarget::Client ? TEXT("srv-client") : TEXT("srv");
		Made.ModId = ModId;
		Made.Digest = Digest;
		return Made;
	}

	FCrowdyModRun ReqRun(ECrowdyStudioRunMode Mode, const TArray<FCrowdyModRunTarget>& Targets, ECrowdyModResult Result = ECrowdyModResult::Success)
	{
		FCrowdyModRun Run;
		Run.Result = Result;
		Run.Mode = Mode;
		Run.GridId = 100;
		Run.ProjectId = TEXT("p-1");
		Run.Targets = Targets;
		return Run;
	}

	FCrowdyMod ReqMod(const FString& Name, const FString& ModId, bool bEnabled, bool bBlocked = false)
	{
		FCrowdyMod Made;
		Made.Name = Name;
		Made.ModId = ModId;
		Made.GridId = 100;
		Made.bEnabled = bEnabled;
		Made.bBlocked = bBlocked;
		Made.BlockedReason = bBlocked ? TEXT("the app's billing is paused") : TEXT("");
		return Made;
	}

	FCrowdyModScript ReqScript(const FString& ModId, const FString& Digest)
	{
		FCrowdyModScript Made;
		Made.ModId = ModId;
		Made.Digest = Digest;
		Made.GridId = 100;
		return Made;
	}

	/** The runtime entry for Target ("SERVER" or "CLIENT") in a BuildResult; null when there is none. */
	TSharedPtr<FJsonObject> ReqHalf(const TSharedPtr<FJsonObject>& Result, const FString& Target)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("runtime"), Entries))
		{
			return nullptr;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			if (Entry->AsObject()->GetStringField(TEXT("target")).Equals(Target, ESearchCase::CaseSensitive))
			{
				return Entry->AsObject();
			}
		}
		return nullptr;
	}

	FString ReqPhase(const TSharedPtr<FJsonObject>& Result, const FString& Target)
	{
		const TSharedPtr<FJsonObject> Half = ReqHalf(Result, Target);
		return Half.IsValid() ? Half->GetStringField(TEXT("phase")) : FString(TEXT("none"));
	}

	FConstructStudioCapture ReqCapture()
	{
		FConstructStudioCapture Capture;
		Capture.bCaptured = true;
		Capture.PngBase64 = TEXT("iVBORw0KGgo=");
		Capture.Width = 320;
		Capture.Height = 180;
		return Capture;
	}

	TSharedRef<FJsonObject> ReqFileChanged(const FString& Target, const FString& Path)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("target"), Target);
		Payload->SetStringField(TEXT("path"), Path);
		return Payload;
	}

	/** Text with one character, by its UTF-16 code unit, between Before and After. */
	FString ReqWith(const TCHAR* Before, uint32 Unit, const TCHAR* After)
	{
		FString Out = Before;
		Out.AppendChar(static_cast<TCHAR>(Unit));
		Out += After;
		return Out;
	}

	TSharedRef<FJsonObject> ReqParams(const TCHAR* Name, const FString& Value)
	{
		const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(Name, Value);
		return Params;
	}

	/** The frame inside a script the dock ran in its page. */
	TSharedPtr<FJsonObject> ReqFrame(const FString& Script)
	{
		const int32 Start = Script.Find(TEXT("deliver("));
		if (Start == INDEX_NONE || !Script.EndsWith(TEXT(");")))
		{
			return nullptr;
		}
		const int32 From = Start + 8;
		TSharedPtr<FJsonObject> Frame;
		FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Script.Mid(From, Script.Len() - From - 2)), Frame);
		return Frame;
	}

	/** Every frame the dock sent for request Id, oldest first. */
	TArray<TSharedPtr<FJsonObject>> ReqFramesFor(const TArray<FString>& Scripts, const FString& Id)
	{
		TArray<TSharedPtr<FJsonObject>> Found;
		for (const FString& Script : Scripts)
		{
			const TSharedPtr<FJsonObject> Frame = ReqFrame(Script);
			FString FrameId;
			if (Frame.IsValid() && Frame->TryGetStringField(TEXT("id"), FrameId) && FrameId.Equals(Id, ESearchCase::CaseSensitive))
			{
				Found.Add(Frame);
			}
		}
		return Found;
	}

	/** The result of request Id's one answer; null when it was not answered exactly once, or was refused. */
	TSharedPtr<FJsonObject> ReqResult(const TArray<FString>& Scripts, const FString& Id)
	{
		const TArray<TSharedPtr<FJsonObject>> Found = ReqFramesFor(Scripts, Id);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (Found.Num() != 1 || !Found[0]->TryGetObjectField(TEXT("result"), Result))
		{
			return nullptr;
		}
		return *Result;
	}

	/** "code: message" of request Id's one refusal; empty when it was not refused exactly once. */
	FString ReqRefusal(const TArray<FString>& Scripts, const FString& Id)
	{
		const TArray<TSharedPtr<FJsonObject>> Found = ReqFramesFor(Scripts, Id);
		FString Code;
		FString Message;
		if (Found.Num() != 1 || !Found[0]->TryGetStringField(TEXT("code"), Code) || !Found[0]->TryGetStringField(TEXT("message"), Message))
		{
			return FString();
		}
		return Code + TEXT(": ") + Message;
	}

	int32 ReqEvents(const TArray<FString>& Scripts, const FString& Event)
	{
		const FString Needle = FString::Printf(TEXT("\"event\":\"%s\""), *Event);
		return Scripts.FilterByPredicate([&Needle](const FString& Script) { return Script.Contains(Needle); }).Num();
	}

	/** Opens Rig's dock through Fake on project p-1 at rev-1, reaches Ready unless not bReady, and joins its page; Scripts then holds only what follows. */
	bool ReadyJoined(FReqRig& Rig, FReqFake& Fake, TArray<FString>& Scripts, bool bReady = true)
	{
		FReqAccess::UsePlatform(*Rig.Dock, Fake.Calls);
		Rig.Open();
		GiveNewest(Fake.Boots, ReqBoot());
		FCrowdyStudioConsent Consent;
		Consent.Result = ECrowdyStudioResult::Success;
		Consent.bConsented = true;
		GiveNewest(Fake.Consents, Consent);
		GiveNewest(Fake.Lists, ReqList({ReqProject(TEXT("p-1"), TEXT("rev-1"))}));
		if (bReady)
		{
			FReqAccess::Boot(*Rig.Dock, FReqAccess::Generation(*Rig.Dock), EConstructStudioDockState::Ready);
		}
		UConstructStudioDockBridge* Bridge = FReqAccess::Bridge(*Rig.Dock);
		const EConstructStudioDockState Expected = bReady ? EConstructStudioDockState::Ready : EConstructStudioDockState::Booting;
		if (Rig.Dock->GetState() != Expected || !Bridge)
		{
			return false;
		}
		FReqAccess::SetMainFrame(*Rig.Dock, ConstructStudioDockScheme::PageUrl);
		FReqAccess::CaptureScripts(*Rig.Dock, [&Scripts](const FString& Script) { Scripts.Add(Script); });
		TSharedPtr<FJsonObject> Boot;
		FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Bridge->Boot()), Boot);
		if (!Boot.IsValid() || !Boot->HasTypedField<EJson::String>(TEXT("nonce")))
		{
			return false;
		}
		Bridge->Send(FString::Printf(TEXT("{\"v\":5,\"n\":\"%s\",\"from\":\"page\",\"t\":\"dock\",\"event\":\"joined\",\"payload\":{}}"), *Boot->GetStringField(TEXT("nonce"))));
		Scripts.Reset();
		return true;
	}

	void Ask(FReqRig& Rig, const FString& Method, const FString& Id, const TSharedPtr<FJsonObject>& Params = nullptr)
	{
		FReqAccess::AgentRequest(*Rig.Dock, FReqAccess::Generation(*Rig.Dock), Method, Id, Params);
	}

	/** Asks for Method, answers the fresh read at Revision, runs the prompt it raises and allows it; false when no prompt showed. */
	bool AskAndRun(FReqRig& Rig, FReqFake& Fake, const FString& Method, const FString& Id, const FString& Revision)
	{
		const int32 Before = Rig.Listener->Prompts.Num();
		Ask(Rig, Method, Id);
		Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), Revision)));
		if (Rig.Listener->Prompts.Num() == Before)
		{
			return false;
		}
		Rig.Dock->AnswerDeployPrompt(Rig.Listener->Prompts.Last().PromptId, true);
		return Give(Fake.Verdicts, ReqVerdict(true));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsProjectsTest, "Construct.StudioDock.Requests.ProjectListOpenAndCreate", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsProjectsTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}

	Ask(F, TEXT("studio.projectList"), TEXT("1"));
	TestEqual(TEXT("projectList asks the platform"), Fake.Count(TEXT("ListProjects")), 2);
	TestEqual(TEXT("and answers nothing before it answers"), ReqFramesFor(Scripts, TEXT("1")).Num(), 0);
	Give(Fake.Lists, ReqList({ReqProject(TEXT("p-1"), TEXT("rev-1")), ReqProject(TEXT("p-2"), TEXT("rev-5"), FString())}));
	const TSharedPtr<FJsonObject> Listed = ReqResult(Scripts, TEXT("1"));
	if (TestTrue(TEXT("projectList is answered once"), Listed.IsValid()))
	{
		TestEqual(TEXT("with both projects"), Listed->GetArrayField(TEXT("projects")).Num(), 2);
		TestEqual(TEXT("and the open one"), Listed->GetStringField(TEXT("currentProjectId")), FString(TEXT("p-1")));
		const TSharedPtr<FJsonObject> Second = Listed->GetArrayField(TEXT("projects"))[1]->AsObject();
		TestEqual(TEXT("a SERVER-only project's kind"), Second->GetStringField(TEXT("kind")), FString(TEXT("SERVER")));
		TestEqual(TEXT("a Studio project's source"), Second->GetStringField(TEXT("source")), FString(TEXT("STUDIO")));
		TestEqual(TEXT("its time as ISO 8601"), Second->GetStringField(TEXT("updatedAt")), FString(TEXT("2025-11-17T10:00:00.000Z")));
		TestTrue(TEXT("and says whether more are held"), Listed->HasTypedField<EJson::Boolean>(TEXT("more")) && !Listed->GetBoolField(TEXT("more")));
	}
	Ask(F, TEXT("studio.projectList"), TEXT("1b"));
	FCrowdyStudioProjectList Full = ReqList({ReqProject(TEXT("p-1"), TEXT("rev-1"))});
	Full.bMore = true;
	Give(Fake.Lists, Full);
	const TSharedPtr<FJsonObject> Truncated = ReqResult(Scripts, TEXT("1b"));
	TestTrue(TEXT("a list the platform cut short says more are held"), Truncated.IsValid() && Truncated->GetBoolField(TEXT("more")));
	Ask(F, TEXT("studio.projectList"), TEXT("2"));
	Give(Fake.Lists, ReqList({}, ECrowdyStudioResult::Failed));
	TestEqual(TEXT("a failed list is refused plainly"), ReqRefusal(Scripts, TEXT("2")), FString(TEXT("PAGE_ERROR: The project list could not be read.")));

	Ask(F, TEXT("studio.projectOpen"), TEXT("3"));
	TestEqual(TEXT("projectOpen without a projectId is refused"), ReqRefusal(Scripts, TEXT("3")), FString(TEXT("PAGE_ERROR: studio.projectOpen needs a projectId.")));
	Ask(F, TEXT("studio.projectOpen"), TEXT("4"), ReqParams(TEXT("projectId"), TEXT("p-2")));
	TestEqual(TEXT("projectOpen reads the project"), Fake.Count(TEXT("Project:p-2")), 1);
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-2"), TEXT("rev-5"), FString())));
	const TSharedPtr<FJsonObject> Opened = ReqResult(Scripts, TEXT("4"));
	TestTrue(TEXT("projectOpen answers the project"), Opened.IsValid() && Opened->GetObjectField(TEXT("project"))->GetStringField(TEXT("projectId")) == TEXT("p-2"));
	TestEqual(TEXT("and tells the worker page.project first"), ReqEvents(Scripts, TEXT("page.project")), 1);
	Ask(F, TEXT("studio.projectList"), TEXT("5"));
	Give(Fake.Lists, ReqList({}));
	const TSharedPtr<FJsonObject> After = ReqResult(Scripts, TEXT("5"));
	TestTrue(TEXT("the opened project is now the open one"), After.IsValid() && After->GetStringField(TEXT("currentProjectId")) == TEXT("p-2"));
	Ask(F, TEXT("studio.projectOpen"), TEXT("6"), ReqParams(TEXT("projectId"), TEXT("p-9")));
	Give(Fake.Projects, ReqInfo(FCrowdyStudioProject(), ECrowdyStudioResult::NotFound, TEXT("No project p-9 for this player.")));
	TestEqual(TEXT("a project that is not found is refused with the SDK's reason"), ReqRefusal(Scripts, TEXT("6")), FString(TEXT("PAGE_ERROR: No project p-9 for this player.")));

	Ask(F, TEXT("studio.projectCreate"), TEXT("7"));
	TestEqual(TEXT("projectCreate without a name is refused"), ReqRefusal(Scripts, TEXT("7")), FString(TEXT("PAGE_ERROR: studio.projectCreate needs a name.")));
	const TSharedRef<FJsonObject> Create = ReqParams(TEXT("name"), TEXT("  Lantern  "));
	Create->SetStringField(TEXT("template"), TEXT("client"));
	Ask(F, TEXT("studio.projectCreate"), TEXT("8"), Create);
	TestEqual(TEXT("projectCreate creates the template's kind under the trimmed name"), Fake.Count(TEXT("Create:Lantern:Client")), 1);
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-3"), TEXT("rev-1"))));
	const TSharedPtr<FJsonObject> Created = ReqResult(Scripts, TEXT("8"));
	TestTrue(TEXT("projectCreate answers the new project"), Created.IsValid() && Created->GetObjectField(TEXT("project"))->GetStringField(TEXT("projectId")) == TEXT("p-3"));
	TestEqual(TEXT("and opens it"), ReqEvents(Scripts, TEXT("page.project")), 2);
	Ask(F, TEXT("studio.projectCreate"), TEXT("9"), ReqParams(TEXT("name"), TEXT("Both")));
	TestEqual(TEXT("with no template both halves are made"), Fake.Count(TEXT("Create:Both:Paired")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsRuntimeTest, "Construct.StudioDock.Requests.RuntimeStatusAndLogs", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsRuntimeTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}

	Ask(F, TEXT("studio.runtimeStatus"), TEXT("1"));
	TestEqual(TEXT("runtimeStatus reads the project's status on the dock's grid"), Fake.Count(TEXT("Status:100:p-1")), 1);
	FCrowdyModProjectStatus Status;
	Status.Result = ECrowdyModResult::Success;
	Status.Targets = {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running), ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::BuildFailed)};
	Status.Targets[0].RunningRevision = TEXT("rev-1");
	Give(Fake.Statuses, Status);
	const TSharedPtr<FJsonObject> Runtime = ReqResult(Scripts, TEXT("1"));
	if (TestTrue(TEXT("runtimeStatus is answered"), Runtime.IsValid() && Runtime->GetArrayField(TEXT("runtime")).Num() == 2))
	{
		const TSharedPtr<FJsonObject> Server = Runtime->GetArrayField(TEXT("runtime"))[0]->AsObject();
		const TSharedPtr<FJsonObject> Client = Runtime->GetArrayField(TEXT("runtime"))[1]->AsObject();
		TestEqual(TEXT("the SERVER half running"), Server->GetStringField(TEXT("target")) + TEXT(":") + Server->GetStringField(TEXT("phase")), FString(TEXT("SERVER:RUNNING")));
		TestEqual(TEXT("at its running revision"), Server->GetStringField(TEXT("runningRevision")), FString(TEXT("rev-1")));
		TestEqual(TEXT("the CLIENT half failed to compile"), Client->GetStringField(TEXT("target")) + TEXT(":") + Client->GetStringField(TEXT("phase")), FString(TEXT("CLIENT:COMPILE_FAILED")));
		TestEqual(TEXT("with its module"), Client->GetStringField(TEXT("moduleName")), FString(TEXT("srv-client")));
	}
	Ask(F, TEXT("studio.runtimeStatus"), TEXT("2"));
	FCrowdyModProjectStatus Refused;
	Refused.Message = TEXT("The status could not be read.");
	Give(Fake.Statuses, Refused);
	TestEqual(TEXT("a failed status read is refused plainly"), ReqRefusal(Scripts, TEXT("2")), FString(TEXT("PAGE_ERROR: The status could not be read.")));

	const TSharedRef<FJsonObject> Two = MakeShared<FJsonObject>();
	Two->SetNumberField(TEXT("limit"), 2);
	Ask(F, TEXT("studio.runtimeLogs"), TEXT("3"), Two);
	TestEqual(TEXT("runtimeLogs reads the SERVER mod's logs, one more than asked"), Fake.Count(TEXT("Logs:100:srv:3")), 1);
	FCrowdyModLogs Logs;
	Logs.Result = ECrowdyModResult::Success;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FCrowdyModLogLine& Line = Logs.Lines.AddDefaulted_GetRef();
		Line.AtMs = 1763373600000 + Index;
		Line.Level = Index;
		Line.Text = FString::Printf(TEXT("line %d"), Index);
	}
	Give(Fake.ModLogs, Logs);
	const TSharedPtr<FJsonObject> Lines = ReqResult(Scripts, TEXT("3"));
	if (TestTrue(TEXT("runtimeLogs is answered"), Lines.IsValid()))
	{
		const TArray<TSharedPtr<FJsonValue>> Kept = Lines->GetArrayField(TEXT("lines"));
		TestEqual(TEXT("with the last lines asked for"), Kept.Num(), 2);
		TestEqual(TEXT("as time, module, level and text"), Kept.Num() == 2 ? Kept[1]->AsString() : FString(), FString(TEXT("2025-11-17T10:00:00.002Z srv info: line 2")));
		TestTrue(TEXT("and says more were held"), Lines->GetBoolField(TEXT("truncated")));
	}
	Ask(F, TEXT("studio.runtimeLogs"), TEXT("4"));
	TestEqual(TEXT("with no limit 40 are asked for"), Fake.Count(TEXT("Logs:100:srv:41")), 1);
	FCrowdyModLogs Missing;
	Missing.Result = ECrowdyModResult::NotFound;
	Give(Fake.ModLogs, Missing);
	const TSharedPtr<FJsonObject> None = ReqResult(Scripts, TEXT("4"));
	TestTrue(TEXT("a mod never deployed has no lines"), None.IsValid() && None->GetArrayField(TEXT("lines")).IsEmpty() && !None->GetBoolField(TEXT("truncated")));

	Ask(F, TEXT("studio.clientLogs"), TEXT("5"));
	FCrowdyModProjectStatus WithClient;
	WithClient.Result = ECrowdyModResult::Success;
	WithClient.Targets = {ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::Running, TEXT("m-1"))};
	Give(Fake.Statuses, WithClient);
	TestEqual(TEXT("clientLogs reads the CLIENT half's script log"), Fake.Count(TEXT("ScriptLog:100:m-1")), 1);
	FCrowdyModScriptLog Own;
	Own.Result = ECrowdyModScriptReadResult::Success;
	Own.Name = TEXT("srv-client");
	Own.AuthorId = 42;
	Own.DroppedLines = 3;
	FCrowdyModScriptLogLine& OwnLine = Own.Lines.AddDefaulted_GetRef();
	OwnLine.AtMs = 1763373600000;
	OwnLine.Level = 1;
	OwnLine.Text = TEXT("slow tick");
	Give(Fake.ScriptLogs, Own);
	const TSharedPtr<FJsonObject> ClientLines = ReqResult(Scripts, TEXT("5"));
	TestTrue(TEXT("the player's own script lines are answered"), ClientLines.IsValid() && ClientLines->GetArrayField(TEXT("lines")).Num() == 1
		&& ClientLines->GetArrayField(TEXT("lines"))[0]->AsString() == TEXT("2025-11-17T10:00:00.000Z srv-client warning: slow tick"));
	TestTrue(TEXT("and dropped lines count as more held"), ClientLines.IsValid() && ClientLines->GetBoolField(TEXT("truncated")));

	Ask(F, TEXT("studio.clientLogs"), TEXT("6"));
	Give(Fake.Statuses, WithClient);
	FCrowdyModScriptLog Other = Own;
	Other.AuthorId = 7;
	Give(Fake.ScriptLogs, Other);
	TestTrue(TEXT("another author's lines are refused"), ReqRefusal(Scripts, TEXT("6")).StartsWith(TEXT("PAGE_ERROR: These CLIENT logs are another author's")));

	Ask(F, TEXT("studio.clientLogs"), TEXT("7"));
	Give(Fake.Statuses, WithClient);
	FCrowdyModScriptLog Away;
	Away.Result = ECrowdyModScriptReadResult::NotInGrid;
	Give(Fake.ScriptLogs, Away);
	const TSharedPtr<FJsonObject> AwayLines = ReqResult(Scripts, TEXT("7"));
	TestTrue(TEXT("outside the grid there are no lines"), AwayLines.IsValid() && AwayLines->GetArrayField(TEXT("lines")).IsEmpty());

	Ask(F, TEXT("studio.clientLogs"), TEXT("8"));
	FCrowdyModProjectStatus ServerOnly;
	ServerOnly.Result = ECrowdyModResult::Success;
	ServerOnly.Targets = {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running)};
	Give(Fake.Statuses, ServerOnly);
	const TSharedPtr<FJsonObject> NoClient = ReqResult(Scripts, TEXT("8"));
	TestTrue(TEXT("a run with no CLIENT half deployed has no lines"), NoClient.IsValid() && NoClient->GetArrayField(TEXT("lines")).IsEmpty());
	TestEqual(TEXT("and reads no script log"), Fake.CountStarting(TEXT("ScriptLog:")), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsGridTest, "Construct.StudioDock.Requests.GridContextAndObserve", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsGridTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}

	Ask(F, TEXT("grid.context"), TEXT("1"));
	const TSharedPtr<FJsonObject> Nowhere = ReqResult(Scripts, TEXT("1"));
	TestTrue(TEXT("with no player in the world the grid is null"), Nowhere.IsValid() && Nowhere->HasTypedField<EJson::Null>(TEXT("grid")));
	TestEqual(TEXT("and no claim is looked up"), Fake.CountStarting(TEXT("Claim:")), 0);

	FReqAccess::SetPlayerChunk(*F.Dock, FInt64Vector(1, -2, 3));
	Ask(F, TEXT("grid.context"), TEXT("2"));
	TestEqual(TEXT("grid.context looks up the claim at the player's chunk"), Fake.Count(TEXT("Claim:1:-2:3")), 1);
	FCrowdyClaimSearch Search;
	Search.Result = ECrowdyClaimResult::Success;
	FCrowdyChunkClaimInfo& Claim = Search.Claims.AddDefaulted_GetRef();
	Claim.GridId = 100;
	Claim.ChunkX = 1;
	Claim.ChunkY = -2;
	Claim.ChunkZ = 3;
	Claim.OwnerUserId = 42;
	Claim.bOwnedByMe = true;
	Give(Fake.Claims, Search);
	TestEqual(TEXT("then the grid's channels"), Fake.Count(TEXT("Channels:100")), 1);
	FCrowdyGridChannels Channels;
	Channels.Result = ECrowdyClaimResult::Success;
	Channels.GridId = 100;
	FCrowdyChannel& Plaza = Channels.Channels.AddDefaulted_GetRef();
	Plaza.ChannelId = 55;
	Plaza.Name = TEXT("Plaza");
	Give(Fake.Channels, Channels);
	const TSharedPtr<FJsonObject> Context = ReqResult(Scripts, TEXT("2"));
	const TSharedPtr<FJsonObject>* Grid = nullptr;
	if (TestTrue(TEXT("grid.context answers the grid"), Context.IsValid() && Context->TryGetObjectField(TEXT("grid"), Grid)))
	{
		TestEqual(TEXT("its app"), (*Grid)->GetStringField(TEXT("appId")), FString(TEXT("9")));
		TestEqual(TEXT("its id"), (*Grid)->GetStringField(TEXT("gridId")), FString(TEXT("100")));
		TestEqual(TEXT("its low corner is the claimed chunk"), (*Grid)->GetObjectField(TEXT("low"))->GetStringField(TEXT("y")), FString(TEXT("-2")));
		TestEqual(TEXT("and so is its high"), (*Grid)->GetObjectField(TEXT("high"))->GetStringField(TEXT("z")), FString(TEXT("3")));
		TestTrue(TEXT("the player owns it"), (*Grid)->GetBoolField(TEXT("owned")));
		TestTrue(TEXT("its channel by id and name"), (*Grid)->GetArrayField(TEXT("channels")).Num() == 1
			&& (*Grid)->GetArrayField(TEXT("channels"))[0]->AsObject()->GetStringField(TEXT("groupId")) == TEXT("55"));
		TestTrue(TEXT("and no sessions"), (*Grid)->GetArrayField(TEXT("sessions")).IsEmpty());
	}

	Ask(F, TEXT("grid.context"), TEXT("3"));
	FCrowdyClaimSearch Elsewhere = Search;
	Elsewhere.Claims[0].GridId = 300;
	Give(Fake.Claims, Elsewhere);
	const TSharedPtr<FJsonObject> Other = ReqResult(Scripts, TEXT("3"));
	TestTrue(TEXT("a claim on another grid is not the dock's grid"), Other.IsValid() && Other->HasTypedField<EJson::Null>(TEXT("grid")));
	TestEqual(TEXT("and reads no channels"), Fake.CountStarting(TEXT("Channels:")), 1);
	Ask(F, TEXT("grid.context"), TEXT("4"));
	FCrowdyClaimSearch Failed;
	Failed.Message = TEXT("The claims could not be read.");
	Give(Fake.Claims, Failed);
	TestEqual(TEXT("a failed lookup is refused plainly"), ReqRefusal(Scripts, TEXT("4")), FString(TEXT("PAGE_ERROR: The claims could not be read.")));

	TArray<FString> Observed;
	bool bObservable = true;
	FReqAccess::ObserveWith(*F.Dock, [&Observed, &bObservable](const TOptional<FConstructStudioObservedGrid>& Seen) -> TSharedPtr<FJsonObject>
	{
		Observed.Add(Seen.IsSet() ? FString::Printf(TEXT("%lld:%lld:%lld:%lld:%d"), Seen->GridId, Seen->Low.X, Seen->Low.Y, Seen->High.Z, Seen->bOwned ? 1 : 0) : FString(TEXT("none")));
		if (!bObservable)
		{
			return nullptr;
		}
		const TSharedRef<FJsonObject> Observation = MakeShared<FJsonObject>();
		Observation->SetStringField(TEXT("contractVersion"), TEXT("crowdy.game-observation/1"));
		return Observation;
	});
	Ask(F, TEXT("game.observe"), TEXT("5"));
	Give(Fake.Claims, Search);
	TestEqual(TEXT("game.observe observes with the claimed grid the player stands in"), FString::Join(Observed, TEXT(",")), FString(TEXT("100:1:-2:3:1")));
	const TSharedPtr<FJsonObject> Seen = ReqResult(Scripts, TEXT("5"));
	TestTrue(TEXT("and answers the observation with when it was taken"), Seen.IsValid() && Seen->HasTypedField<EJson::Object>(TEXT("observation"))
		&& Seen->GetStringField(TEXT("capturedAt")).EndsWith(TEXT("Z")));
	FReqAccess::Advance(*F.Dock, static_cast<float>(UConstructStudioDock::ClaimReuseSeconds) + 0.5f);
	Ask(F, TEXT("game.observe"), TEXT("6"));
	Give(Fake.Claims, Elsewhere);
	TestEqual(TEXT("off the dock's grid it observes without one"), Observed.Last(), FString(TEXT("none")));
	TestTrue(TEXT("and still answers"), ReqResult(Scripts, TEXT("6")).IsValid());
	bObservable = false;
	FReqAccess::Advance(*F.Dock, static_cast<float>(UConstructStudioDock::ClaimReuseSeconds) + 0.5f);
	Ask(F, TEXT("game.observe"), TEXT("7"));
	Give(Fake.Claims, Search);
	TestEqual(TEXT("with nothing to observe it is refused"), ReqRefusal(Scripts, TEXT("7")), FString(TEXT("PAGE_ERROR: Nothing to observe right now: the player is not in the world.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsScreenshotTest, "Construct.StudioDock.Requests.ScreenshotAnswersOrRefuses", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsScreenshotTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	Ask(F, TEXT("studio.screenshot"), TEXT("1"), ReqParams(TEXT("label"), TEXT("the fountain")));
	if (!TestTrue(TEXT("the host is asked with the agent's label"), F.Listener->Lines.Contains(TEXT("Capture:the fountain")) && F.Listener->Captures.Num() == 1))
	{
		return false;
	}
	FConstructStudioCapture Capture;
	Capture.bCaptured = true;
	Capture.PngBase64 = TEXT("iVBORw0KGgo=");
	Capture.Width = 640;
	Capture.Height = 360;
	Capture.Caption = TEXT("the fountain");
	UConstructStudioDock::FinishCapture(F.Listener->Captures[0], Capture);
	const TSharedPtr<FJsonObject> Shot = ReqResult(Scripts, TEXT("1"));
	if (TestTrue(TEXT("the screenshot is answered"), Shot.IsValid()))
	{
		TestEqual(TEXT("with its bytes as base64 for the page to turn into binary"), Shot->GetStringField(TEXT("bytesBase64")), Capture.PngBase64);
		TestEqual(TEXT("as a PNG"), Shot->GetStringField(TEXT("mediaType")), FString(TEXT("image/png")));
		TestEqual(TEXT("named in order"), Shot->GetStringField(TEXT("name")), FString(TEXT("capture-0001.png")));
		TestEqual(TEXT("with its size"), Shot->GetNumberField(TEXT("width")), 640.0);
	}

	Ask(F, TEXT("studio.screenshot"), TEXT("2"));
	TestTrue(TEXT("with no label the host gets the dock's"), F.Listener->Lines.Contains(TEXT("Capture:Agent screenshot")));
	FConstructStudioCapture Refused;
	Refused.Refusal = TEXT("No frame is drawn yet.");
	if (F.Listener->Captures.Num() == 2)
	{
		UConstructStudioDock::FinishCapture(F.Listener->Captures[1], Refused);
	}
	TestEqual(TEXT("the host's refusal is passed on"), ReqRefusal(Scripts, TEXT("2")), FString(TEXT("PAGE_ERROR: No frame is drawn yet.")));

	F.Dock->SetHost(TScriptInterface<IConstructStudioDockHost>());
	Ask(F, TEXT("studio.screenshot"), TEXT("3"));
	TestEqual(TEXT("with no host it is refused at once"), ReqRefusal(Scripts, TEXT("3")), FString(TEXT("PAGE_ERROR: This game is not taking screenshots for the agent right now.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsGenerationTest, "Construct.StudioDock.Requests.AnswersAreOnceAndOnlyForTheirBoot", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsGenerationTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	const int64 First = FReqAccess::Generation(*F.Dock);
	Ask(F, TEXT("grid.programRun"), TEXT("p1"));
	TestTrue(TEXT("grid programs are not offered in Unreal"), ReqRefusal(Scripts, TEXT("p1")).StartsWith(TEXT("BRIDGE_REFUSED: ")));
	Ask(F, TEXT("draft.tree"), TEXT("p2"));
	TestTrue(TEXT("nor are the game editor's requests"), ReqRefusal(Scripts, TEXT("p2")).StartsWith(TEXT("BRIDGE_REFUSED: ")));

	Ask(F, TEXT("studio.projectList"), TEXT("same"));
	Ask(F, TEXT("studio.projectList"), TEXT("same"));
	TestEqual(TEXT("a second request with a held id asks nothing more"), Fake.Count(TEXT("ListProjects")), 2);
	Give(Fake.Lists, ReqList({}));
	TestEqual(TEXT("and the id is answered once"), ReqFramesFor(Scripts, TEXT("same")).Num(), 1);

	Ask(F, TEXT("studio.projectList"), TEXT("s1"));
	TestEqual(TEXT("a request waiting on the platform is held"), FReqAccess::HeldRequestCount(*F.Dock), 1);
	F.Dock->Close();
	TestEqual(TEXT("Close refuses it"), ReqRefusal(Scripts, TEXT("s1")), FString(TEXT("PAGE_ERROR: The Studio dock closed.")));
	if (!TestTrue(TEXT("the dock opens again with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	FReqAccess::AgentRequest(*F.Dock, First, TEXT("studio.projectList"), TEXT("s2"));
	TestEqual(TEXT("a request from the closed boot asks nothing"), Fake.Count(TEXT("ListProjects")), 4);
	Ask(F, TEXT("studio.projectList"), TEXT("s1"));
	Give(Fake.Lists, ReqList({ReqProject(TEXT("stale"), TEXT("rev-0"))}));
	TestEqual(TEXT("the closed boot's late answer reaches nobody, not even a new request with its id"), ReqFramesFor(Scripts, TEXT("s1")).Num(), 0);
	Give(Fake.Lists, ReqList({}));
	const TSharedPtr<FJsonObject> Fresh = ReqResult(Scripts, TEXT("s1"));
	TestTrue(TEXT("the new boot's answer is its own"), Fresh.IsValid() && Fresh->GetArrayField(TEXT("projects")).IsEmpty());

	for (int32 Index = 0; Index < UConstructStudioDock::MaxHeldRequests; ++Index)
	{
		Ask(F, TEXT("studio.projectList"), FString::Printf(TEXT("h%d"), Index));
	}
	TestEqual(TEXT("the dock holds up to its limit"), FReqAccess::HeldRequestCount(*F.Dock), UConstructStudioDock::MaxHeldRequests);
	Ask(F, TEXT("studio.projectList"), TEXT("over"));
	TestTrue(TEXT("one more is refused at once"), ReqRefusal(Scripts, TEXT("over")).StartsWith(TEXT("PAGE_ERROR: Too many requests")));
	TestEqual(TEXT("and asks nothing"), Fake.Count(TEXT("ListProjects")), 5 + UConstructStudioDock::MaxHeldRequests);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsDeployTest, "Construct.StudioDock.Requests.DeployReadsFreshThenAuthorizesAndRuns", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsDeployTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	Ask(F, TEXT("studio.draftTest"), TEXT("d1"));
	TestEqual(TEXT("a deploy request reads the project fresh"), Fake.Count(TEXT("Project:p-1")), 1);
	TestEqual(TEXT("and shows no prompt before it answers"), F.Listener->Prompts.Num(), 0);
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-7"))));
	if (!TestEqual(TEXT("then the prompt shows"), F.Listener->Prompts.Num(), 1))
	{
		return false;
	}
	const FConstructStudioDeployPrompt Prompt = F.Listener->Prompts[0];
	TestEqual(TEXT("at the revision just read"), Prompt.Revision, FString(TEXT("rev-7")));
	TestEqual(TEXT("on the dock's grid"), Prompt.GridId, int64(100));
	TestEqual(TEXT("with its label"), Prompt.GridLabel.ToString(), FString(TEXT("Chunk 1, 2, 3")));
	TestEqual(TEXT("for the project"), Prompt.ProjectId + TEXT(":") + Prompt.ProjectName, FString(TEXT("p-1:Fountain")));
	TestTrue(TEXT("reaching visitors with its CLIENT half"), Prompt.bReachesVisitors);
	TestEqual(TEXT("as a draft test"), ConstructStudioDockName(Prompt.Kind), FString(TEXT("DraftTest")));
	Ask(F, TEXT("studio.deployLive"), TEXT("busy"));
	TestTrue(TEXT("another deploy while the prompt is up is refused"), ReqRefusal(Scripts, TEXT("busy")).StartsWith(TEXT("PAGE_ERROR: A deploy is already waiting")));

	F.Dock->AnswerDeployPrompt(Prompt.PromptId, true);
	TestEqual(TEXT("Run asks the app's switch first"), Fake.Count(TEXT("Authorize:DraftTest")), 1);
	TestEqual(TEXT("and runs nothing before it answers"), Fake.CountStarting(TEXT("Run:")), 0);
	Give(Fake.Verdicts, ReqVerdict(true));
	TestEqual(TEXT("then runs the prompt's grid, project, revision and module names"), Fake.Count(TEXT("Run:100:p-1:rev-7:DraftTest:srv:srv-client")), 1);
	FCrowdyModRun Broken = ReqRun(ECrowdyStudioRunMode::DraftTest, {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::BuildFailed)});
	Broken.BuildLog = TEXT("error[E0425]: cannot find value `spin` in this scope");
	FCrowdyModDiagnostic& Diagnostic = Broken.Diagnostics.AddDefaulted_GetRef();
	Diagnostic.Path = TEXT("src/lib.rs");
	Diagnostic.Line = 3;
	Diagnostic.Column = 5;
	Diagnostic.Message = TEXT("cannot find value `spin` in this scope");
	Diagnostic.Code = TEXT("E0425");
	Give(Fake.Runs, Broken);
	TestEqual(TEXT("a crate that does not compile is a failed run"), F.SentCount(TEXT("RunProject:failed")), 1);
	const TSharedPtr<FJsonObject> Result = ReqResult(Scripts, TEXT("d1"));
	if (TestTrue(TEXT("the agent gets the build result"), Result.IsValid()))
	{
		TestFalse(TEXT("not ok"), Result->GetBoolField(TEXT("ok")));
		TestEqual(TEXT("as a draft"), Result->GetStringField(TEXT("mode")), FString(TEXT("draft")));
		TestFalse(TEXT("with a summary"), Result->GetStringField(TEXT("summary")).IsEmpty());
		TestTrue(TEXT("with the compiler's diagnostic"), Result->GetArrayField(TEXT("diagnostics")).Num() == 1
			&& Result->GetArrayField(TEXT("diagnostics"))[0]->AsObject()->GetStringField(TEXT("message")) == TEXT("cannot find value `spin` in this scope [E0425]"));
		TestTrue(TEXT("and its log"), Result->GetStringField(TEXT("buildLog")).Contains(TEXT("E0425")));
		TestTrue(TEXT("and the SERVER half's phase"), Result->GetArrayField(TEXT("runtime")).Num() == 1
			&& Result->GetArrayField(TEXT("runtime"))[0]->AsObject()->GetStringField(TEXT("phase")) == TEXT("COMPILE_FAILED"));
	}

	TestTrue(TEXT("a run that answered frees the slot"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("d2"), TEXT("rev-8")));
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {}, ECrowdyModResult::RevisionChanged));
	TestTrue(TEXT("a run the SDK refused is refused plainly"), ReqRefusal(Scripts, TEXT("d2")).StartsWith(TEXT("PAGE_ERROR: ")));

	TestTrue(TEXT("a SERVER-only run reaches the end"), AskAndRun(F, Fake, TEXT("studio.deployLive"), TEXT("d3"), TEXT("rev-9")));
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::Live, {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running)}));
	const TSharedPtr<FJsonObject> Live = ReqResult(Scripts, TEXT("d3"));
	TestTrue(TEXT("and is answered ok at once, as live"), Live.IsValid() && Live->GetBoolField(TEXT("ok")) && Live->GetStringField(TEXT("mode")) == TEXT("live")
		&& !Live->HasField(TEXT("screenshot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsDeclinedTest, "Construct.StudioDock.Requests.DeclinedOrRefusedDeploysAreRefused", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsDeclinedTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	Ask(F, TEXT("studio.draftTest"), TEXT("n1"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	F.Dock->AnswerDeployPrompt(F.Listener->Prompts.Last().PromptId, false);
	TestEqual(TEXT("Not now is refused to the agent"), ReqRefusal(Scripts, TEXT("n1")), FString(TEXT("PAGE_ERROR: The player declined the draft test. Ask them before testing again.")));

	Ask(F, TEXT("studio.deployLive"), TEXT("n2"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	FReqAccess::Advance(*F.Dock, 60.5f);
	TestTrue(TEXT("a prompt left 60 s is refused"), ReqRefusal(Scripts, TEXT("n2")).StartsWith(TEXT("PAGE_ERROR: The player did not answer")));

	Ask(F, TEXT("studio.draftTest"), TEXT("n3"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	F.Dock->SetGrid(200, FText::FromString(TEXT("Chunk 4, 5, 6")));
	TestTrue(TEXT("a prompt withdrawn by a move is refused"), ReqRefusal(Scripts, TEXT("n3")).StartsWith(TEXT("PAGE_ERROR: The deploy prompt was withdrawn")));

	Ask(F, TEXT("studio.deployLive"), TEXT("n4"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	F.Dock->AnswerDeployPrompt(F.Listener->Prompts.Last().PromptId, true);
	Give(Fake.Verdicts, ReqVerdict(false));
	TestEqual(TEXT("Allow Live Modding off is refused"), ReqRefusal(Scripts, TEXT("n4")), FString(TEXT("PAGE_ERROR: Allow Live Modding is off for this app, so nothing was built or deployed.")));
	TestEqual(TEXT("and runs nothing"), Fake.CountStarting(TEXT("Run:")), 0);

	Ask(F, TEXT("studio.draftTest"), TEXT("n5"));
	Give(Fake.Projects, ReqInfo(FCrowdyStudioProject(), ECrowdyStudioResult::NotFound, TEXT("The project is gone.")));
	TestEqual(TEXT("a fresh read that fails is refused with its reason"), ReqRefusal(Scripts, TEXT("n5")), FString(TEXT("PAGE_ERROR: The project is gone.")));
	TestEqual(TEXT("and raises no prompt"), F.Listener->Prompts.Num(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsClientWaitTest, "Construct.StudioDock.Requests.ClientHalfIsAwaitedForTwentySeconds", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsClientWaitTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	const FCrowdyModRunTarget Server = ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running);
	const FCrowdyModRunTarget Client = ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::Deployed, TEXT("m-1"), TEXT("ABC"));

	TestTrue(TEXT("a live deploy runs"), AskAndRun(F, Fake, TEXT("studio.deployLive"), TEXT("c1"), TEXT("rev-2")));
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::Live, {Server, Client}));
	TestEqual(TEXT("a deployed CLIENT half holds the answer"), ReqFramesFor(Scripts, TEXT("c1")).Num(), 0);
	Ask(F, TEXT("studio.draftTest"), TEXT("busy"));
	TestTrue(TEXT("and the slot"), ReqRefusal(Scripts, TEXT("busy")).StartsWith(TEXT("PAGE_ERROR: A deploy is already waiting")));
	const int32 Asked = F.Listener->Captures.Num();
	FReqAccess::ScriptStarted(*F.Dock, TEXT("m-1"), TEXT("OLD"));
	FReqAccess::ScriptStarted(*F.Dock, TEXT("m-2"), TEXT("abc"));
	TestEqual(TEXT("neither an older version nor another mod starting counts"), F.Listener->Captures.Num(), Asked);
	FReqAccess::ScriptStarted(*F.Dock, TEXT("m-1"), TEXT("abc"));
	if (!TestEqual(TEXT("the deployed version starting asks the host for a screenshot"), F.Listener->Captures.Num(), Asked + 1))
	{
		return false;
	}
	TestTrue(TEXT("labelled for the deploy"), F.Listener->Lines.Contains(TEXT("Capture:After live deploy")));
	FConstructStudioCapture Capture;
	Capture.bCaptured = true;
	Capture.PngBase64 = TEXT("iVBORw0KGgo=");
	Capture.Width = 320;
	Capture.Height = 180;
	UConstructStudioDock::FinishCapture(F.Listener->Captures.Last(), Capture);
	const TSharedPtr<FJsonObject> Started = ReqResult(Scripts, TEXT("c1"));
	TestTrue(TEXT("the answer carries the screenshot"), Started.IsValid() && Started->GetBoolField(TEXT("ok"))
		&& Started->HasTypedField<EJson::Object>(TEXT("screenshot")) && Started->GetObjectField(TEXT("screenshot"))->GetStringField(TEXT("bytesBase64")) == Capture.PngBase64);
	TestEqualSensitive(TEXT("with the CLIENT half running"), ReqPhase(Started, TEXT("CLIENT")), FString(TEXT("RUNNING")));
	TestEqualSensitive(TEXT("and says so"), Started.IsValid() ? Started->GetStringField(TEXT("summary")) : FString(), FString(TEXT("The live deploy is running.")));

	TestTrue(TEXT("a draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("c2"), TEXT("rev-3")));
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server, Client}));
	FReqAccess::Advance(*F.Dock, 19.f);
	TestEqual(TEXT("at 19 s the answer still waits"), ReqFramesFor(Scripts, TEXT("c2")).Num(), 0);
	FReqAccess::Advance(*F.Dock, 1.5f);
	const TSharedPtr<FJsonObject> NotStarted = ReqResult(Scripts, TEXT("c2"));
	if (TestTrue(TEXT("at 20 s it is answered"), NotStarted.IsValid()))
	{
		const TSharedPtr<FJsonObject> ClientRuntime = NotStarted->GetArrayField(TEXT("runtime"))[1]->AsObject();
		TestEqual(TEXT("with the CLIENT half not started"), ClientRuntime->GetStringField(TEXT("phase")), FString(TEXT("IDLE")));
		TestTrue(TEXT("saying so"), ClientRuntime->GetStringField(TEXT("message")).Contains(TEXT("did not see it start")));
		TestFalse(TEXT("and no screenshot"), NotStarted->HasField(TEXT("screenshot")));
		TestTrue(TEXT("a half not seen starting is not a failure, so the run stays ok"), NotStarted->GetBoolField(TEXT("ok")));
		TestFalse(TEXT("and the summary does not call it running"), NotStarted->GetStringField(TEXT("summary")).Contains(TEXT("is running")));
	}
	FReqAccess::ScriptStarted(*F.Dock, TEXT("m-1"), TEXT("abc"));
	TestEqual(TEXT("a start after the answer asks nothing"), F.Listener->Captures.Num(), Asked + 1);

	TestTrue(TEXT("a run whose CLIENT half already runs here"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("c3"), TEXT("rev-4")));
	FCrowdyModRunTarget Running = Client;
	Running.Phase = ECrowdyModRunPhase::Running;
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server, Running}));
	TestEqual(TEXT("is captured at once"), F.Listener->Captures.Num(), Asked + 2);
	F.Dock->SetHost(TScriptInterface<IConstructStudioDockHost>());
	UConstructStudioDock::FinishCapture(F.Listener->Captures.Last(), FConstructStudioCapture());
	const TSharedPtr<FJsonObject> Unshot = ReqResult(Scripts, TEXT("c3"));
	TestTrue(TEXT("and a missing screenshot is not a failed run"), Unshot.IsValid() && Unshot->GetBoolField(TEXT("ok")) && !Unshot->HasField(TEXT("screenshot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsLateRunTest, "Construct.StudioDock.Requests.RunOutlastingTheAgentIsNotAnswered", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsLateRunTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	TestTrue(TEXT("a draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("late"), TEXT("rev-2")));
	FReqAccess::Advance(*F.Dock, 239.f);
	TestEqual(TEXT("at 239 s the request is still held"), FReqAccess::HeldRequestCount(*F.Dock), 1);
	FReqAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("at 240 s the agent has stopped waiting"), FReqAccess::HeldRequestCount(*F.Dock), 0);
	Ask(F, TEXT("studio.draftTest"), TEXT("busy"));
	TestTrue(TEXT("the run still holds the slot"), ReqRefusal(Scripts, TEXT("busy")).StartsWith(TEXT("PAGE_ERROR: A deploy is already waiting")));
	const int32 Asked = F.Listener->Captures.Num();
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running),
		ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::Running, TEXT("m-1"))}));
	TestEqual(TEXT("the run finishes"), F.SentCount(TEXT("RunProject:ok")), 1);
	TestEqual(TEXT("and its late answer is not sent"), ReqFramesFor(Scripts, TEXT("late")).Num(), 0);
	TestEqual(TEXT("nor is a screenshot taken for nobody"), F.Listener->Captures.Num(), Asked);
	TestTrue(TEXT("then the slot is free"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("next"), TEXT("rev-3")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsRunIsOkTest, "Construct.StudioDock.Requests.RunIsOkOnlyWhenEveryHalfDeployed", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsRunIsOkTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	const FCrowdyModRunTarget Running = ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running);
	const FCrowdyModRunTarget Deployed = ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::Deployed);
	const FCrowdyModRunTarget BuildFailed = ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::BuildFailed);
	const FCrowdyModRunTarget DeployFailed = ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::DeployFailed);
	TestTrue(TEXT("running and deployed halves are ok"), ConstructStudioDockReplies::RunIsOk(ReqRun(ECrowdyStudioRunMode::Live, {Running, Deployed})));
	TestFalse(TEXT("a half that did not compile is not, though the run went to its end"), ConstructStudioDockReplies::RunIsOk(ReqRun(ECrowdyStudioRunMode::Live, {Running, BuildFailed})));
	TestFalse(TEXT("nor one that did not deploy"), ConstructStudioDockReplies::RunIsOk(ReqRun(ECrowdyStudioRunMode::Live, {DeployFailed})));
	TestFalse(TEXT("nor a run with no halves"), ConstructStudioDockReplies::RunIsOk(ReqRun(ECrowdyStudioRunMode::Live, {})));
	TestFalse(TEXT("nor a run that did not finish"), ConstructStudioDockReplies::RunIsOk(ReqRun(ECrowdyStudioRunMode::Live, {Running}, ECrowdyModResult::Failed)));
	TestTrue(TEXT("a deployed CLIENT half is awaited"), ConstructStudioDockReplies::DeployedClient(ReqRun(ECrowdyStudioRunMode::Live, {Running, Deployed})) != nullptr);
	TestTrue(TEXT("a failed one is not"), ConstructStudioDockReplies::DeployedClient(ReqRun(ECrowdyStudioRunMode::Live, {Running, BuildFailed})) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsSendPromptTest, "Construct.StudioDock.Requests.SendPromptOnlyOnceReady", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsSendPromptTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	TestFalse(TEXT("a closed dock sends no prompt"), F.Dock->SendPrompt(TEXT("Make the fountain spin.")));
	if (!TestTrue(TEXT("the dock's page joins while it boots"), ReadyJoined(F, Fake, Scripts, false)))
	{
		return false;
	}
	TestFalse(TEXT("a booting dock sends no prompt, though its page has joined"), F.Dock->SendPrompt(TEXT("Make the fountain spin.")));
	TestEqual(TEXT("and nothing reaches the page"), Scripts.Num(), 0);
	FReqAccess::Boot(*F.Dock, FReqAccess::Generation(*F.Dock), EConstructStudioDockState::Ready);
	TestTrue(TEXT("a Ready dock sends it"), F.Dock->SendPrompt(TEXT("Make the fountain spin.")));
	TestTrue(TEXT("as page.prompt, queued"), Scripts.Num() == 1 && Scripts[0].Contains(TEXT("\"event\":\"page.prompt\""))
		&& Scripts[0].Contains(TEXT("\"text\":\"Make the fountain spin.\"")) && Scripts[0].Contains(TEXT("\"mode\":\"queue\"")));
	F.Dock->Close();
	TestFalse(TEXT("and a closed one does not again"), F.Dock->SendPrompt(TEXT("Make the fountain spin.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsClientRunningTest, "Construct.StudioDock.Requests.ClientHalfRunningOrFailingIsAnsweredAtOnce", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsClientRunningTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	const FCrowdyModRunTarget Server = ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running);
	const FCrowdyModRunTarget Client = ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::Deployed, TEXT("m-1"), TEXT("ABC"));

	// An unchanged CLIENT crate is answered Deployed and is not restarted, so no start is ever heard for it.
	Fake.Running = {ReqScript(TEXT("m-1"), TEXT("abc"))};
	TestTrue(TEXT("a draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("a1"), TEXT("rev-2")));
	const int32 Asked = F.Listener->Captures.Num();
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server, Client}));
	if (!TestEqual(TEXT("a CLIENT half already running here, its digest in another case, is captured at once"), F.Listener->Captures.Num(), Asked + 1))
	{
		return false;
	}
	UConstructStudioDock::FinishCapture(F.Listener->Captures.Last(), ReqCapture());
	const TSharedPtr<FJsonObject> Found = ReqResult(Scripts, TEXT("a1"));
	TestTrue(TEXT("and answered ok with its screenshot"), Found.IsValid() && Found->GetBoolField(TEXT("ok")) && Found->HasTypedField<EJson::Object>(TEXT("screenshot")));
	TestEqualSensitive(TEXT("with the CLIENT half running"), ReqPhase(Found, TEXT("CLIENT")), FString(TEXT("RUNNING")));

	Fake.Running.Reset();
	TestTrue(TEXT("a second draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("a2"), TEXT("rev-3")));
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server, Client}));
	FReqAccess::Advance(*F.Dock, 19.f);
	TestEqual(TEXT("while it is not running the answer waits"), ReqFramesFor(Scripts, TEXT("a2")).Num(), 0);
	Fake.Running = {ReqScript(TEXT("m-1"), TEXT("abc"))};
	FReqAccess::Advance(*F.Dock, 1.5f);
	const TSharedPtr<FJsonObject> Late = ReqResult(Scripts, TEXT("a2"));
	TestTrue(TEXT("found running when the wait ends, it is answered ok"), Late.IsValid() && Late->GetBoolField(TEXT("ok")));
	TestEqualSensitive(TEXT("with the CLIENT half running"), ReqPhase(Late, TEXT("CLIENT")), FString(TEXT("RUNNING")));
	TestEqual(TEXT("and no screenshot, with no time left for one"), F.Listener->Captures.Num(), Asked + 1);

	Fake.Running.Reset();
	TestTrue(TEXT("a third draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("a3"), TEXT("rev-4")));
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server, Client}));
	FReqAccess::ScriptStopped(*F.Dock, TEXT("m-1"), TEXT("abc"), ECrowdyModScriptStop::Changed);
	FReqAccess::ScriptStopped(*F.Dock, TEXT("m-2"), TEXT("abc"), ECrowdyModScriptStop::Failed);
	TestEqual(TEXT("a stop that is not a failure of that mod reads nothing"), Fake.CountStarting(TEXT("ScriptStatus:")), 0);
	// The SDK reports a version that failed to fetch or compile without its digest, so the status read decides.
	FReqAccess::ScriptStopped(*F.Dock, TEXT("m-1"), FString(), ECrowdyModScriptStop::Failed);
	TestEqual(TEXT("that mod failing reads the grid's script status"), Fake.Count(TEXT("ScriptStatus:100")), 1);
	FCrowdyModScriptStatusList Older;
	Older.Result = ECrowdyModScriptReadResult::Success;
	FCrowdyModScriptStatus& OlderFailed = Older.Scripts.AddDefaulted_GetRef();
	OlderFailed.Script = ReqScript(TEXT("m-1"), TEXT("old"));
	OlderFailed.State = ECrowdyModScriptState::Erred;
	Give(Fake.ScriptStatuses, Older);
	TestEqual(TEXT("an older version's failure answers nothing"), ReqFramesFor(Scripts, TEXT("a3")).Num(), 0);
	FReqAccess::ScriptStopped(*F.Dock, TEXT("m-1"), FString(), ECrowdyModScriptStop::Failed);
	TestEqual(TEXT("a second failure reads the status again"), Fake.Count(TEXT("ScriptStatus:100")), 2);
	TestEqual(TEXT("and answers nothing before it answers"), ReqFramesFor(Scripts, TEXT("a3")).Num(), 0);
	FCrowdyModScriptStatusList Status;
	Status.Result = ECrowdyModScriptReadResult::Success;
	FCrowdyModScriptStatus& Failed = Status.Scripts.AddDefaulted_GetRef();
	Failed.Script = ReqScript(TEXT("m-1"), TEXT("abc"));
	Failed.State = ECrowdyModScriptState::Erred;
	Failed.Message = TEXT("The module failed its check.");
	Give(Fake.ScriptStatuses, Status);
	const TSharedPtr<FJsonObject> Erred = ReqResult(Scripts, TEXT("a3"));
	TestTrue(TEXT("then it is answered at once, not ok"), Erred.IsValid() && !Erred->GetBoolField(TEXT("ok")));
	TestEqualSensitive(TEXT("with the CLIENT half not started"), ReqPhase(Erred, TEXT("CLIENT")), FString(TEXT("IDLE")));
	const TSharedPtr<FJsonObject> ErredClient = ReqHalf(Erred, TEXT("CLIENT"));
	TestTrue(TEXT("for the reason the status gives"), ErredClient.IsValid() && ErredClient->GetStringField(TEXT("message")).Contains(TEXT("failed its check")));
	TestTrue(TEXT("which the summary names"), Erred.IsValid() && Erred->GetStringField(TEXT("summary")).Contains(TEXT("failed its check")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsCaptureLimitTest, "Construct.StudioDock.Requests.RunScreenshotIsWithinTheTwentySeconds", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsCaptureLimitTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	const FCrowdyModRunTarget Server = ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Running);
	const FCrowdyModRunTarget Client = ReqTarget(ECrowdyStudioTarget::Client, ECrowdyModRunPhase::Running, TEXT("m-1"), TEXT("abc"));
	TestTrue(TEXT("a live deploy runs"), AskAndRun(F, Fake, TEXT("studio.deployLive"), TEXT("t1"), TEXT("rev-2")));
	const int32 Asked = F.Listener->Captures.Num();
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::Live, {Server, Client}));
	if (!TestEqual(TEXT("the running CLIENT half is captured"), F.Listener->Captures.Num(), Asked + 1))
	{
		return false;
	}
	FReqAccess::Advance(*F.Dock, 19.f);
	TestEqual(TEXT("at 19 s the capture is still awaited"), ReqFramesFor(Scripts, TEXT("t1")).Num(), 0);
	TestTrue(TEXT("and the run is still being answered"), F.Dock->IsRunInFlightForChecks());
	FReqAccess::Advance(*F.Dock, 1.5f);
	const TSharedPtr<FJsonObject> Unshot = ReqResult(Scripts, TEXT("t1"));
	TestTrue(TEXT("at 20 s it is answered ok without a screenshot"), Unshot.IsValid() && Unshot->GetBoolField(TEXT("ok")) && !Unshot->HasField(TEXT("screenshot")));
	TestFalse(TEXT("and nothing is in flight"), F.Dock->IsRunInFlightForChecks());
	UConstructStudioDock::FinishCapture(F.Listener->Captures.Last(), ReqCapture());
	TestEqual(TEXT("the late capture answers nothing more"), ReqFramesFor(Scripts, TEXT("t1")).Num(), 1);
	TestTrue(TEXT("and the slot is free"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("t2"), TEXT("rev-3")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsServerModTest, "Construct.StudioDock.Requests.DeployedServerIsRunningOnlyWhenSwitchedOnAndNotBlocked", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsServerModTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	const FCrowdyModRunTarget Server = ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Deployed, TEXT("s-1"));

	TestTrue(TEXT("a draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("s1"), TEXT("rev-2")));
	Fake.GridMods.Mods = {ReqMod(TEXT("srv"), TEXT("s-1"), true)};
	const int32 Read = Fake.CountStarting(TEXT("Mods:"));
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server}));
	TestEqual(TEXT("a deployed SERVER half reads the grid's mods once"), Fake.CountStarting(TEXT("Mods:")), Read + 1);
	const TSharedPtr<FJsonObject> On = ReqResult(Scripts, TEXT("s1"));
	TestTrue(TEXT("switched on and not blocked, it is ok"), On.IsValid() && On->GetBoolField(TEXT("ok")));
	TestEqualSensitive(TEXT("and running, as the Studio says"), ReqPhase(On, TEXT("SERVER")), FString(TEXT("RUNNING")));
	TestEqualSensitive(TEXT("which the summary says"), On.IsValid() ? On->GetStringField(TEXT("summary")) : FString(), FString(TEXT("The draft test is running.")));

	Fake.GridMods.Mods.Reset();
	TestTrue(TEXT("a second draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("s2"), TEXT("rev-3")));
	Fake.GridMods.Mods = {ReqMod(TEXT("srv"), TEXT("s-1"), true, true)};
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server}));
	const TSharedPtr<FJsonObject> Blocked = ReqResult(Scripts, TEXT("s2"));
	TestTrue(TEXT("a blocked mod is not ok"), Blocked.IsValid() && !Blocked->GetBoolField(TEXT("ok")));
	TestEqualSensitive(TEXT("nor running"), ReqPhase(Blocked, TEXT("SERVER")), FString(TEXT("STOPPED")));
	TestTrue(TEXT("and the summary names the block"), Blocked.IsValid() && Blocked->GetStringField(TEXT("summary")).Contains(TEXT("blocked: the app's billing is paused"))
		&& !Blocked->GetStringField(TEXT("summary")).Contains(TEXT("is running")));

	Fake.GridMods.Mods.Reset();
	TestTrue(TEXT("a third draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("s3"), TEXT("rev-4")));
	Fake.GridMods.Mods = {ReqMod(TEXT("srv"), TEXT("s-1"), false)};
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server}));
	const TSharedPtr<FJsonObject> Off = ReqResult(Scripts, TEXT("s3"));
	TestTrue(TEXT("a switched-off mod is not ok, and says so"), Off.IsValid() && !Off->GetBoolField(TEXT("ok")) && Off->GetStringField(TEXT("summary")).Contains(TEXT("switched off")));

	Fake.GridMods.Mods.Reset();
	TestTrue(TEXT("a fourth draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("s4"), TEXT("rev-5")));
	Fake.bHoldMods = true;
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {Server}));
	TestEqual(TEXT("the answer waits for the mods"), ReqFramesFor(Scripts, TEXT("s4")).Num(), 0);
	TestTrue(TEXT("and the run is still being answered"), F.Dock->IsRunInFlightForChecks());
	Ask(F, TEXT("studio.draftTest"), TEXT("busy"));
	TestTrue(TEXT("so the slot is held"), ReqRefusal(Scripts, TEXT("busy")).StartsWith(TEXT("PAGE_ERROR: A deploy is already waiting")));
	FCrowdyModList Unread;
	Unread.Message = TEXT("The mods could not be read.");
	Give(Fake.ModLists, Unread);
	const TSharedPtr<FJsonObject> Unknown = ReqResult(Scripts, TEXT("s4"));
	TestTrue(TEXT("mods that cannot be read keep the run ok"), Unknown.IsValid() && Unknown->GetBoolField(TEXT("ok")));
	TestEqualSensitive(TEXT("but not running"), ReqPhase(Unknown, TEXT("SERVER")), FString(TEXT("STOPPED")));
	TestFalse(TEXT("nor said to be"), Unknown.IsValid() && Unknown->GetStringField(TEXT("summary")).Contains(TEXT("is running")));
	TestFalse(TEXT("and nothing is in flight"), F.Dock->IsRunInFlightForChecks());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsFailedRunTest, "Construct.StudioDock.Requests.FailedRunIsAFailedBuildResult", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsFailedRunTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	TestTrue(TEXT("a draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("f1"), TEXT("rev-2")));
	FCrowdyModRun Stopped = ReqRun(ECrowdyStudioRunMode::DraftTest, {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::DeployFailed)}, ECrowdyModResult::Failed);
	Stopped.Message = TEXT("The SERVER half could not be switched on.");
	Stopped.BuildLog = TEXT("Finished release target");
	FCrowdyModDiagnostic& Diagnostic = Stopped.Diagnostics.AddDefaulted_GetRef();
	Diagnostic.Path = TEXT("src/lib.rs");
	Diagnostic.Message = TEXT("unused variable");
	Diagnostic.Severity = ECrowdyModDiagnosticSeverity::Warning;
	Give(Fake.Runs, Stopped);
	const TSharedPtr<FJsonObject> Result = ReqResult(Scripts, TEXT("f1"));
	if (TestTrue(TEXT("a run that stopped after building is answered a BuildResult, not a refusal"), Result.IsValid()))
	{
		TestFalse(TEXT("not ok"), Result->GetBoolField(TEXT("ok")));
		TestEqualSensitive(TEXT("with the run's message as its summary"), Result->GetStringField(TEXT("summary")), Stopped.Message);
		TestTrue(TEXT("its build log"), Result->GetStringField(TEXT("buildLog")).Contains(TEXT("Finished release target")));
		TestEqual(TEXT("its diagnostics"), Result->GetArrayField(TEXT("diagnostics")).Num(), 1);
		TestEqualSensitive(TEXT("and each half's phase"), ReqPhase(Result, TEXT("SERVER")), FString(TEXT("ERROR")));
	}
	TestEqual(TEXT("the run is reported failed"), F.SentCount(TEXT("RunProject:failed")), 1);

	TestTrue(TEXT("a second draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("f2"), TEXT("rev-3")));
	FCrowdyModRun Quiet = ReqRun(ECrowdyStudioRunMode::DraftTest, {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Building)}, ECrowdyModResult::Failed);
	Give(Fake.Runs, Quiet);
	const TSharedPtr<FJsonObject> Wordless = ReqResult(Scripts, TEXT("f2"));
	TestTrue(TEXT("one with no message says it did not finish"), Wordless.IsValid() && Wordless->GetStringField(TEXT("summary")).StartsWith(TEXT("The draft test did not finish")));

	TestTrue(TEXT("a third draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("f3"), TEXT("rev-4")));
	FCrowdyModRun Busy = ReqRun(ECrowdyStudioRunMode::DraftTest, {}, ECrowdyModResult::Busy);
	Busy.Message = TEXT("Another run for this app is still going.");
	Give(Fake.Runs, Busy);
	TestEqual(TEXT("a run refused before it built anything is refused plainly"), ReqRefusal(Scripts, TEXT("f3")), FString(TEXT("PAGE_ERROR: Another run for this app is still going.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsStuckSlotTest, "Construct.StudioDock.Requests.DeployTheAgentStoppedWaitingForFreesTheSlot", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsStuckSlotTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	TestTrue(TEXT("a draft test runs"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("k1"), TEXT("rev-2")));
	Fake.bHoldMods = true;
	Give(Fake.Runs, ReqRun(ECrowdyStudioRunMode::DraftTest, {ReqTarget(ECrowdyStudioTarget::Server, ECrowdyModRunPhase::Deployed, TEXT("s-1"))}));
	TestTrue(TEXT("a mods read that never answers holds the answer"), F.Dock->IsRunInFlightForChecks());
	FReqAccess::Advance(*F.Dock, 239.f);
	TestEqual(TEXT("at 239 s the request is still held"), FReqAccess::HeldRequestCount(*F.Dock), 1);
	FReqAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("at 240 s the agent has stopped waiting"), FReqAccess::HeldRequestCount(*F.Dock), 0);
	TestFalse(TEXT("and the held answer is forgotten"), F.Dock->IsRunInFlightForChecks());
	Fake.bHoldMods = false;
	TestTrue(TEXT("so the slot is free"), AskAndRun(F, Fake, TEXT("studio.draftTest"), TEXT("k2"), TEXT("rev-3")));
	FCrowdyModList Late;
	Late.Result = ECrowdyModResult::Success;
	Late.Mods = {ReqMod(TEXT("srv"), TEXT("s-1"), true)};
	Give(Fake.ModLists, Late);
	TestEqual(TEXT("the late mods answer answers neither request"), ReqFramesFor(Scripts, TEXT("k1")).Num() + ReqFramesFor(Scripts, TEXT("k2")).Num(), 0);
	TestTrue(TEXT("and the new run is still the one in flight"), F.Dock->IsRunInFlightForChecks());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsReplacedModTest, "Construct.StudioDock.Requests.DeployThatWouldReplaceAnotherModIsRefused", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsReplacedModTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	// The player's own mod, deployed by an earlier game or the web Studio: nothing in this process records it.
	FCrowdyMod Own = ReqMod(TEXT("srv"), TEXT("s-9"), true);
	Own.OwnerUserId = 42;
	Fake.GridMods.Mods = {Own};
	Ask(F, TEXT("studio.draftTest"), TEXT("r1"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	TestEqual(TEXT("the player's own mod with the project's module name raises the prompt"), F.Listener->Prompts.Num(), 1);
	TestEqual(TEXT("with no status read to decide whose it is"), Fake.Count(TEXT("Status:100:p-1")), 0);
	F.Dock->AnswerDeployPrompt(F.Listener->Prompts.Last().PromptId, false);

	FCrowdyMod Foreign = Own;
	Foreign.OwnerUserId = 7;
	Fake.GridMods.Mods = {Foreign};
	Ask(F, TEXT("studio.draftTest"), TEXT("r2"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	TestTrue(TEXT("another player's mod is not replaced"), ReqRefusal(Scripts, TEXT("r2")).StartsWith(TEXT("PAGE_ERROR: This grid has a mod named srv that another player owns")));
	TestEqual(TEXT("no prompt shows"), F.Listener->Prompts.Num(), 1);
	TestEqual(TEXT("and the refusal is told"), F.SentCount(TEXT("studio.draftTest:refused: replaces a mod")), 1);

	FCrowdyMod Lamp = ReqMod(TEXT("lamp"), TEXT("c-1"), true);
	Lamp.OwnerUserId = 7;
	Fake.GridMods.Mods = {Lamp};
	FCrowdyStudioProject ClientOnly = ReqProject(TEXT("p-1"), TEXT("rev-1"), TEXT("lamp"));
	ClientOnly.ServerModule.Reset();
	ClientOnly.Kind = ECrowdyStudioProjectKind::Client;
	Ask(F, TEXT("studio.draftTest"), TEXT("r4"));
	Give(Fake.Projects, ReqInfo(ClientOnly));
	TestTrue(TEXT("a CLIENT-only project checks the mod named for its CLIENT module"), ReqRefusal(Scripts, TEXT("r4")).StartsWith(TEXT("PAGE_ERROR: This grid has a mod named lamp")));

	Fake.GridMods.Result = ECrowdyModResult::Failed;
	Fake.GridMods.Message = TEXT("The mods could not be read.");
	Ask(F, TEXT("studio.draftTest"), TEXT("r5"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	TestEqual(TEXT("mods that cannot be read offer no deploy"), ReqRefusal(Scripts, TEXT("r5")), FString(TEXT("PAGE_ERROR: The mods could not be read.")));
	TestEqual(TEXT("and nothing was authorized or run"), Fake.CountStarting(TEXT("Authorize:")) + Fake.CountStarting(TEXT("Run:")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsFailedMidDeployTest, "Construct.StudioDock.Requests.NoPromptOnADockThatFailedMidDeploy", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsFailedMidDeployTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	Fake.bHoldMods = true;
	Ask(F, TEXT("studio.draftTest"), TEXT("f1"));
	Give(Fake.Projects, ReqInfo(ReqProject(TEXT("p-1"), TEXT("rev-1"))));
	TestEqual(TEXT("the deploy waits on the grid's mods"), Fake.Count(TEXT("Mods:100")), 1);
	FReqAccess::FailNow(*F.Dock);
	Give(Fake.ModLists, Fake.GridMods);
	TestEqual(TEXT("the late mod list raises no prompt on the failed dock"), F.Listener->Prompts.Num(), 0);
	TestEqual(TEXT("and nothing is authorized or run"), Fake.CountStarting(TEXT("Authorize:")) + Fake.CountStarting(TEXT("Run:")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsObserveReuseTest, "Construct.StudioDock.Requests.ObserveReusesItsClaimLookupBriefly", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsObserveReuseTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	TArray<FString> Observed;
	FReqAccess::ObserveWith(*F.Dock, [&Observed](const TOptional<FConstructStudioObservedGrid>& Seen) -> TSharedPtr<FJsonObject>
	{
		Observed.Add(Seen.IsSet() ? LexToString(Seen->GridId) : FString(TEXT("none")));
		return MakeShared<FJsonObject>();
	});
	FCrowdyClaimSearch Search;
	Search.Result = ECrowdyClaimResult::Success;
	FCrowdyChunkClaimInfo& Claim = Search.Claims.AddDefaulted_GetRef();
	Claim.GridId = 100;
	Claim.ChunkX = 1;
	Claim.ChunkY = -2;
	Claim.ChunkZ = 3;
	Claim.bOwnedByMe = true;
	FReqAccess::SetPlayerChunk(*F.Dock, FInt64Vector(1, -2, 3));

	Ask(F, TEXT("game.observe"), TEXT("o1"));
	Give(Fake.Claims, Search);
	TestTrue(TEXT("the first observe looks the claim up"), Fake.CountStarting(TEXT("Claim:")) == 1 && ReqResult(Scripts, TEXT("o1")).IsValid());
	Ask(F, TEXT("game.observe"), TEXT("o2"));
	TestEqual(TEXT("a second one in the same chunk asks nothing"), Fake.CountStarting(TEXT("Claim:")), 1);
	TestTrue(TEXT("and answers with the same grid"), ReqResult(Scripts, TEXT("o2")).IsValid() && Observed.Last() == TEXT("100"));
	Ask(F, TEXT("grid.context"), TEXT("g1"));
	TestEqual(TEXT("grid.context always looks up"), Fake.CountStarting(TEXT("Claim:")), 2);
	Give(Fake.Claims, Search);

	FReqAccess::Advance(*F.Dock, static_cast<float>(UConstructStudioDock::ClaimReuseSeconds) + 0.5f);
	Ask(F, TEXT("game.observe"), TEXT("o3"));
	TestEqual(TEXT("after a few seconds it looks up again"), Fake.CountStarting(TEXT("Claim:")), 3);
	Give(Fake.Claims, Search);
	FReqAccess::SetPlayerChunk(*F.Dock, FInt64Vector(1, -2, 4));
	Ask(F, TEXT("game.observe"), TEXT("o4"));
	TestEqual(TEXT("so does a move to another chunk"), Fake.CountStarting(TEXT("Claim:")), 4);
	Give(Fake.Claims, Search);
	F.Dock->SetGrid(200, FText::FromString(TEXT("Chunk 1, -2, 4")));
	Ask(F, TEXT("game.observe"), TEXT("o5"));
	TestEqual(TEXT("and a move to another grid"), Fake.CountStarting(TEXT("Claim:")), 5);
	FCrowdyClaimSearch Failed;
	Failed.Message = TEXT("The claims could not be read.");
	Give(Fake.Claims, Failed);
	Ask(F, TEXT("game.observe"), TEXT("o6"));
	TestEqual(TEXT("a failed lookup is not reused"), Fake.CountStarting(TEXT("Claim:")), 6);
	Give(Fake.Claims, Search);

	F.Dock->Close();
	if (!TestTrue(TEXT("the dock opens again with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	F.Dock->SetGrid(200, FText::FromString(TEXT("Chunk 1, -2, 4")));
	FReqAccess::ObserveWith(*F.Dock, [](const TOptional<FConstructStudioObservedGrid>&) -> TSharedPtr<FJsonObject> { return MakeShared<FJsonObject>(); });
	Ask(F, TEXT("game.observe"), TEXT("o7"));
	TestEqual(TEXT("a new boot does not reuse the closed boot's lookup"), Fake.CountStarting(TEXT("Claim:")), 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsChangedFilesTest, "Construct.StudioDock.Requests.ChangedFilesAreReadableHalfFilesInPlainText", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsChangedFilesTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqFake Fake;
	TArray<FString> Scripts;
	FReqRig F;
	if (!TestTrue(TEXT("the dock reaches Ready with its page joined"), ReadyJoined(F, Fake, Scripts)))
	{
		return false;
	}
	struct FReqChange
	{
		FString Target;
		FString Path;
	};
	const FReqChange Dropped[] =
	{
		{TEXT("server"), TEXT("src/x.rs")}, {TEXT("REPO"), TEXT("src/y.rs")}, {TEXT("SERVER"), TEXT("../Cargo.toml")}, {TEXT("SERVER"), TEXT("src/a b.rs")},
		{TEXT("SERVER"), TEXT("src//a.rs")}, {TEXT("SERVER"), ReqWith(TEXT("src/"), 0x202E, TEXT("evil.rs"))}, {TEXT("SERVER"), TEXT("src/lib.rs.txt")},
		{TEXT("CLIENT"), TEXT("readme.md")}, {TEXT(""), TEXT("src/lib.rs")}
	};
	for (const FReqChange& Each : Dropped)
	{
		FReqAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), ReqFileChanged(Each.Target, Each.Path));
	}
	const TSharedRef<FJsonObject> Numbered = ReqFileChanged(TEXT("SERVER"), TEXT("src/lib.rs"));
	Numbered->SetNumberField(TEXT("target"), 1);
	FReqAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), Numbered);
	FReqAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("no file outside a half's readable files is listed"), FReqAccess::ChangedFileCount(*F.Dock), 0);
	TestEqual(TEXT("and none is read back"), Fake.Count(TEXT("Project:p-1")), 0);

	FReqAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), ReqFileChanged(TEXT("SERVER"), TEXT("src/lib.rs")));
	FReqAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), ReqFileChanged(TEXT("CLIENT"), TEXT("Cargo.toml")));
	FReqAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), ReqFileChanged(TEXT("SERVER"), TEXT("README.md")));
	FReqAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), ReqFileChanged(TEXT("CLIENT"), TEXT("src/a_b/C-d/e9.rs")));
	TestEqual(TEXT("each readable file of either half is listed"), FReqAccess::ChangedFileCount(*F.Dock), 4);

	Ask(F, TEXT("studio.draftTest"), TEXT("d1"));
	FCrowdyStudioProject Odd = ReqProject(TEXT("p-1"), TEXT("rev-2"));
	Odd.Name = ReqWith(TEXT("Foun"), 0x202E, TEXT("tain")) + ReqWith(TEXT(""), 0x200B, TEXT("")) + ReqWith(TEXT(""), 0x07, TEXT(""));
	Give(Fake.Projects, ReqInfo(Odd));
	if (!TestEqual(TEXT("the prompt shows"), F.Listener->Prompts.Num(), 1))
	{
		return false;
	}
	TestEqualSensitive(TEXT("with the project's name in plain text"), F.Listener->Prompts[0].ProjectName, FString(TEXT("Fountain")));
	TestEqualSensitive(TEXT("and each file under its half"), FString::Join(F.Listener->Prompts[0].ChangedFiles, TEXT(",")),
		FString(TEXT("SERVER/src/lib.rs,CLIENT/Cargo.toml,SERVER/README.md,CLIENT/src/a_b/C-d/e9.rs")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsPathRuleTest, "Construct.StudioDock.Requests.ReadablePathsAndPlainText", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsPathRuleTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	for (const TCHAR* Good : {TEXT("Cargo.toml"), TEXT("README.md"), TEXT("src/lib.rs"), TEXT("src/a_b/C-d/e9.rs")})
	{
		TestTrue(FString::Printf(TEXT("%s is readable"), Good), ConstructStudioDockReplies::IsReadablePath(Good));
	}
	for (const TCHAR* Bad : {TEXT("cargo.toml"), TEXT("Readme.md"), TEXT("src/.rs"), TEXT("src/a.RS"), TEXT("lib.rs"), TEXT("src//a.rs"), TEXT("src/a/.rs"),
		TEXT("src/../a.rs"), TEXT("src/a.b.rs"), TEXT("src\\a.rs"), TEXT("/src/a.rs"), TEXT("")})
	{
		TestFalse(FString::Printf(TEXT("%s is not"), Bad), ConstructStudioDockReplies::IsReadablePath(Bad));
	}
	const FString Marked = ReqWith(TEXT("a"), TEXT('\t'), TEXT("b")) + ReqWith(TEXT(""), 0x2066, TEXT("c")) + ReqWith(TEXT(""), 0xFEFF, TEXT(""))
		+ ReqWith(TEXT(""), 0xDB40, TEXT("")) + ReqWith(TEXT(""), 0xDC41, TEXT("")) + ReqWith(TEXT(""), 0xE9, TEXT(""))
		+ ReqWith(TEXT(""), 0xD83D, TEXT("")) + ReqWith(TEXT(""), 0xDE00, TEXT("!"));
	const FString Kept = ReqWith(TEXT("abc"), 0xE9, TEXT("")) + ReqWith(TEXT(""), 0xD83D, TEXT("")) + ReqWith(TEXT(""), 0xDE00, TEXT("!"));
	TestEqualSensitive(TEXT("controls, format marks and tag characters go; accents and emoji stay"), ConstructStudioDockReplies::PlainText(Marked), Kept);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRequestsWarningTextTest, "Construct.StudioDock.Requests.AgentWarningArrivesAsPlainTextOnOneLine", ConstructStudioDockRequestsTests::ReqFlags)
bool FConstructStudioDockRequestsWarningTextTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockRequestsTests;
	FReqRig F;
	F.Ready();
	const auto Warn = [&F](const FString& Message)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("message"), Message);
		F.Listener->Lines.Reset();
		FReqAccess::WorkerEvent(*F.Dock, TEXT("worker.warning"), Payload);
		return F.Listener->Lines.FilterByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("Warning:"), ESearchCase::CaseSensitive); });
	};

	const FString Marked = ReqWith(TEXT("Disk"), 0x202E, TEXT(" full\r\nRun")) + ReqWith(TEXT(""), 0x2028, TEXT("now")) + ReqWith(TEXT(""), 0x07, TEXT(""))
		+ ReqWith(TEXT(""), 0x200B, TEXT("!"));
	const TArray<FString> Plain = Warn(Marked);
	if (TestEqual(TEXT("a marked warning reaches the panel"), Plain.Num(), 1))
	{
		TestEqualSensitive(TEXT("with bidi and control characters gone and line breaks flattened"), Plain[0], FString(TEXT("Warning:Disk full  Run now!")));
	}
	TestEqual(TEXT("a warning of hidden characters only is dropped"), Warn(ReqWith(TEXT(""), 0x202E, TEXT("")) + ReqWith(TEXT(""), 0x07, TEXT(""))).Num(), 0);
	const TArray<FString> Long = Warn(ReqWith(TEXT(""), 0x2066, *FString::ChrN(600, TEXT('w'))));
	if (TestEqual(TEXT("a long warning arrives"), Long.Num(), 1))
	{
		TestEqual(TEXT("cut to 500 visible characters"), Long[0].Len(), FString(TEXT("Warning:")).Len() + 500);
	}
	return true;
}

#endif
