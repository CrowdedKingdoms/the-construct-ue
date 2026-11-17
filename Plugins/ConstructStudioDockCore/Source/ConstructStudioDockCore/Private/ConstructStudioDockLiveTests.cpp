#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioDock.h"
#include "ConstructStudioDockBridge.h"
#include "ConstructStudioDockFrames.h"
#include "ConstructStudioDockPlatform.h"
#include "ConstructStudioDockScheme.h"
#include "ConstructStudioDockTestAccess.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ConstructStudioDockLiveTests
{
	constexpr EAutomationTestFlags LiveFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using FAccess = FConstructStudioDockTestAccess;
	using FRig = FConstructStudioDockTestRig;

	template <typename T>
	using TAnswers = TArray<TFunction<void(const T&)>>;

	/** Gives Value to the oldest held answer; false when none is held. */
	template <typename T>
	bool Answer(TAnswers<T>& Held, const T& Value)
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

	/** Records each SDK call the dock makes as a line and holds its answer for the test. Declare it before the rig it serves. */
	struct FFakePlatform
	{
		TSharedRef<FConstructStudioDockPlatform> Calls = MakeShared<FConstructStudioDockPlatform>();
		TArray<FString> Log;
		TAnswers<FCrowdyStudioAgentBoot> Boots;
		TAnswers<FCrowdyStudioConsent> Consents;
		TAnswers<FCrowdyStudioModelUsage> Usages;
		TAnswers<FCrowdyStudioLiveModding> Verdicts;
		TAnswers<FCrowdyModRun> Runs;
		TAnswers<FCrowdyStudioProjectList> Lists;
		TAnswers<FCrowdyStudioProjectInfo> Projects;

		FFakePlatform()
		{
			Calls->GetAgentBoot = [this](TFunction<void(const FCrowdyStudioAgentBoot&)> OnDone)
			{
				Log.Add(TEXT("Boot"));
				Boots.Add(MoveTemp(OnDone));
			};
			Calls->GetProviderConsent = [this](TFunction<void(const FCrowdyStudioConsent&)> OnDone)
			{
				Log.Add(TEXT("Consent"));
				Consents.Add(MoveTemp(OnDone));
			};
			Calls->RecordProviderConsent = [this](TFunction<void(const FCrowdyStudioConsent&)> OnDone)
			{
				Log.Add(TEXT("RecordConsent"));
				Consents.Add(MoveTemp(OnDone));
			};
			Calls->GetConsentNotice = []()
			{
				return FText::FromString(TEXT("The provider notice"));
			};
			Calls->GetModelUsage = [this](TFunction<void(const FCrowdyStudioModelUsage&)> OnDone)
			{
				Log.Add(TEXT("Usage"));
				Usages.Add(MoveTemp(OnDone));
			};
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
			Calls->ListProjects = [this](TFunction<void(const FCrowdyStudioProjectList&)> OnDone)
			{
				Log.Add(TEXT("ListProjects"));
				Lists.Add(MoveTemp(OnDone));
			};
			Calls->GetProject = [this](const FString& ProjectId, TFunction<void(const FCrowdyStudioProjectInfo&)> OnDone)
			{
				Log.Add(TEXT("Project:") + ProjectId);
				Projects.Add(MoveTemp(OnDone));
			};
		}

		FFakePlatform(const FFakePlatform&) = delete;
		FFakePlatform& operator=(const FFakePlatform&) = delete;

		int32 Count(const FString& Line) const
		{
			return Log.FilterByPredicate([&Line](const FString& Each) { return Each.Equals(Line, ESearchCase::CaseSensitive); }).Num();
		}

		int32 CountStarting(const FString& Prefix) const
		{
			return Log.FilterByPredicate([&Prefix](const FString& Each) { return Each.StartsWith(Prefix, ESearchCase::CaseSensitive); }).Num();
		}
	};

	FCrowdyStudioAgentBoot MakeBoot(bool bWithModel = true, ECrowdyStudioResult Result = ECrowdyStudioResult::Success)
	{
		FCrowdyStudioAgentBoot Boot;
		Boot.Result = Result;
		Boot.AppId = 9;
		Boot.GraphQLUrl = TEXT("https://api.example.test/graphql");
		Boot.ModelBaseUrl = TEXT("https://api.example.test");
		Boot.ConnectOrigins = {TEXT("https://api.example.test")};
		Boot.StudioOrigin = TEXT("https://studio.example.test");
		Boot.WalletUrl = TEXT("https://studio.example.test/wallet");
		if (bWithModel)
		{
			FCrowdyStudioModel Model;
			Model.ModelId = TEXT("model-1");
			Boot.Models.Add(Model);
		}
		return Boot;
	}

	FCrowdyStudioConsent MakeConsent(bool bConsented, ECrowdyStudioResult Result = ECrowdyStudioResult::Success)
	{
		FCrowdyStudioConsent Consent;
		Consent.Result = Result;
		Consent.bConsented = bConsented;
		return Consent;
	}

	FCrowdyStudioProject MakeProject(const FString& Revision, const FString& ServerModule = TEXT("srv"), const FString& ClientModule = TEXT("srv-client"))
	{
		FCrowdyStudioProject Made;
		Made.ProjectId = TEXT("p-1");
		Made.Name = TEXT("Fountain");
		Made.Revision = Revision;
		Made.Kind = ECrowdyStudioProjectKind::Paired;
		Made.ServerModule = ServerModule;
		Made.ClientModule = ClientModule;
		return Made;
	}

	FCrowdyStudioProjectList MakeList(const TArray<FCrowdyStudioProject>& Projects, ECrowdyStudioResult Result = ECrowdyStudioResult::Success)
	{
		FCrowdyStudioProjectList List;
		List.Result = Result;
		List.Projects = Projects;
		return List;
	}

	FCrowdyStudioProjectInfo MakeInfo(const FCrowdyStudioProject& Project, ECrowdyStudioResult Result = ECrowdyStudioResult::Success)
	{
		FCrowdyStudioProjectInfo Info;
		Info.Result = Result;
		Info.Project = Project;
		return Info;
	}

	FCrowdyStudioLiveModding MakeVerdict(bool bAllowed, ECrowdyStudioResult Result = ECrowdyStudioResult::Success)
	{
		FCrowdyStudioLiveModding Verdict;
		Verdict.Result = Result;
		Verdict.bAllowed = bAllowed;
		return Verdict;
	}

	FCrowdyModRun MakeRun(ECrowdyModResult Result)
	{
		FCrowdyModRun Run;
		Run.Result = Result;
		return Run;
	}

	FCrowdyStudioModelUsage MakeUsage(int64 Requests)
	{
		FCrowdyStudioModelUsage Usage;
		Usage.Result = ECrowdyStudioResult::Success;
		Usage.Payer = ECrowdyStudioPayer::Player;
		Usage.TodayRequests = Requests;
		Usage.TodayChargeMicroUsd = 250000;
		Usage.DayLimitMicroUsd = 50000000;
		return Usage;
	}

	FString LiveStateOf(const FRig& Rig)
	{
		return ConstructStudioDockName(Rig.Dock->GetState());
	}

	int32 ClosedLines(const FRig& Rig)
	{
		return Rig.Listener->Lines.FilterByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("State:Closed"), ESearchCase::CaseSensitive); }).Num();
	}

	/** Boots the dock through the fake with consent given and project p-1 at rev-1, then reaches Ready. */
	bool ReadyLive(FRig& Rig, FFakePlatform& Fake)
	{
		FAccess::UsePlatform(*Rig.Dock, Fake.Calls);
		Rig.Open();
		Answer(Fake.Boots, MakeBoot());
		Answer(Fake.Consents, MakeConsent(true));
		Answer(Fake.Lists, MakeList({MakeProject(TEXT("rev-1"))}));
		FAccess::Boot(*Rig.Dock, FAccess::Generation(*Rig.Dock), EConstructStudioDockState::Ready);
		return Rig.Dock->GetState() == EConstructStudioDockState::Ready && FAccess::Bridge(*Rig.Dock) != nullptr;
	}

	int64 RequestPrompt(FRig& Rig, EConstructStudioDeployKind Kind = EConstructStudioDeployKind::DraftTest)
	{
		const int32 Before = Rig.Listener->Prompts.Num();
		FAccess::Request(*Rig.Dock, Kind);
		return Rig.Listener->Prompts.Num() > Before ? Rig.Listener->Prompts.Last().PromptId : 0;
	}

	TSharedRef<FJsonObject> FileChanged(const FString& Target, const FString& Path)
	{
		const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("target"), Target);
		Payload->SetStringField(TEXT("path"), Path);
		return Payload;
	}

	/** Brings Rig's dock to Ready with a prompt up, runs Cause, and checks it closed once, withdrew the prompt and let go of the page. */
	void ExpectClosesOnce(FAutomationTestBase& Test, const FString& Rule, FRig& Rig, FFakePlatform& Fake, TFunctionRef<void()> Cause, const FString& Reason)
	{
		if (!Test.TestTrue(Rule + TEXT(": the dock reaches Ready"), ReadyLive(Rig, Fake)))
		{
			return;
		}
		const int64 PromptId = RequestPrompt(Rig);
		const TStrongObjectPtr<UConstructStudioDockBridge> Bridge(FAccess::Bridge(*Rig.Dock));
		// On the dock's origin a bridge still bound would answer the boot material, not an error.
		FAccess::SetMainFrame(*Rig.Dock, ConstructStudioDockScheme::PageUrl);
		Cause();
		FAccess::Advance(*Rig.Dock, 0.016f);
		Test.TestEqual(Rule + TEXT(": the dock is Closed"), LiveStateOf(Rig), FString(TEXT("Closed")));
		Test.TestEqual(Rule + TEXT(": with its reason"), ConstructStudioDockName(Rig.Dock->GetReason()), Reason);
		Test.TestEqual(Rule + TEXT(": it closed once"), ClosedLines(Rig), 1);
		Test.TestTrue(Rule + TEXT(": the prompt was withdrawn once"), PromptId != 0 && Rig.Listener->WithdrawnIds == TArray<int64>({PromptId}));
		Test.TestTrue(Rule + TEXT(": the page is released"), FAccess::Bridge(*Rig.Dock) == nullptr);
		Test.TestEqual(Rule + TEXT(": the old page's bridge answers closed"), Bridge.IsValid() ? Bridge->Boot() : FString(), ConstructStudioDockFrames::ErrorReply(TEXT("closed")));
	}

	FString LivePageNotice(const FString& Nonce, const FString& Event)
	{
		return FString::Printf(TEXT("{\"v\":5,\"n\":\"%s\",\"from\":\"page\",\"t\":\"dock\",\"event\":\"%s\",\"payload\":{}}"), *Nonce, *Event);
	}
}

using namespace ConstructStudioDockLiveTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveConsentTest, "Construct.StudioDock.Live.ConsentWaitsThenCarriesTheBootOn", LiveFlags)
bool FConstructStudioDockLiveConsentTest::RunTest(const FString& Parameters)
{
	FFakePlatform Fake;
	FRig F;
	FAccess::UsePlatform(*F.Dock, Fake.Calls);
	TestEqual(TEXT("Open boots"), ConstructStudioDockName(F.Open()), FString(TEXT("None")));
	TestTrue(TEXT("by reading the boot material"), Fake.Log == TArray<FString>({TEXT("Boot")}));
	Answer(Fake.Boots, MakeBoot());
	TestEqual(TEXT("then the player's consent"), Fake.Count(TEXT("Consent")), 1);
	TestEqual(TEXT("still Booting"), LiveStateOf(F), FString(TEXT("Booting")));
	Answer(Fake.Consents, MakeConsent(false));
	TestEqual(TEXT("no consent waits in NeedsConsent"), LiveStateOf(F), FString(TEXT("NeedsConsent")));
	TestEqual(TEXT("with its reason"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("NeedsConsent")));
	TestEqual(TEXT("carrying the SDK's notice"), F.Dock->GetConsentNotice().ToString(), FString(TEXT("The provider notice")));
	TestEqual(TEXT("nothing more is read before consent"), Fake.Log.Num(), 2);
	FAccess::Advance(*F.Dock, 120.f);
	TestEqual(TEXT("the player may read the notice for as long as they like"), LiveStateOf(F), FString(TEXT("NeedsConsent")));
	F.Dock->GiveProviderConsent();
	TestEqual(TEXT("consent carries the boot on"), LiveStateOf(F), FString(TEXT("Booting")));
	TestEqual(TEXT("and is recorded with the SDK"), Fake.Count(TEXT("RecordConsent")), 1);
	F.Dock->GiveProviderConsent();
	TestEqual(TEXT("a second consent while booting records nothing"), Fake.Count(TEXT("RecordConsent")), 1);
	Answer(Fake.Consents, MakeConsent(true));
	TestEqual(TEXT("then the project is read"), Fake.Count(TEXT("ListProjects")), 1);
	TestTrue(TEXT("and the page waits for it"), FAccess::Bridge(*F.Dock) == nullptr);
	Answer(Fake.Lists, MakeList({MakeProject(TEXT("rev-1"))}));
	TestTrue(TEXT("the read project lets the page load"), FAccess::Bridge(*F.Dock) != nullptr);
	FAccess::Boot(*F.Dock, FAccess::Generation(*F.Dock), EConstructStudioDockState::Ready);
	TestEqual(TEXT("the harness's boot reaches Ready"), F.Lines(),
		FString(TEXT("State:Booting:None | State:NeedsConsent:NeedsConsent | State:Booting:None | State:Ready:None")));
	F.Dock->Close();

	FFakePlatform Given;
	FRig G;
	FAccess::UsePlatform(*G.Dock, Given.Calls);
	G.Dock->Open(100, FText::FromString(TEXT("Chunk 1, 2, 3")), TEXT("p-9"));
	Answer(Given.Boots, MakeBoot());
	Answer(Given.Consents, MakeConsent(true));
	TestEqual(TEXT("a project given to Open is read by its id"), Given.Count(TEXT("Project:p-9")), 1);
	TestEqual(TEXT("and the list is not read"), Given.Count(TEXT("ListProjects")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveFailuresTest, "Construct.StudioDock.Live.NoModelAndUnavailableFail", LiveFlags)
bool FConstructStudioDockLiveFailuresTest::RunTest(const FString& Parameters)
{
	{
		FFakePlatform Fake;
		FRig F;
		FAccess::UsePlatform(*F.Dock, Fake.Calls);
		F.Open();
		Answer(Fake.Boots, MakeBoot(false));
		TestEqual(TEXT("no model fails"), LiveStateOf(F), FString(TEXT("Failed")));
		TestEqual(TEXT("with NoModel"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("NoModel")));
		TestEqual(TEXT("and asks for no consent"), Fake.Count(TEXT("Consent")), 0);
	}
	{
		FFakePlatform Fake;
		FRig F;
		FAccess::UsePlatform(*F.Dock, Fake.Calls);
		F.Open();
		FCrowdyStudioAgentBoot Refused = MakeBoot(true, ECrowdyStudioResult::Refused);
		Refused.Message = TEXT("The app allows the Studio agent no model, so it cannot start.");
		Answer(Fake.Boots, Refused);
		TestEqual(TEXT("a refused boot read fails Unavailable"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("Unavailable")));
		TestEqual(TEXT("and shows the SDK's reason"), F.Dock->GetReasonText().ToString(), Refused.Message);
	}
	{
		FFakePlatform Fake;
		FRig F;
		FAccess::UsePlatform(*F.Dock, Fake.Calls);
		F.Open();
		Answer(Fake.Boots, MakeBoot());
		FCrowdyStudioConsent Refused = MakeConsent(true, ECrowdyStudioResult::Refused);
		Refused.Message = TEXT("The consent record could not be read for this app.");
		Answer(Fake.Consents, Refused);
		TestEqual(TEXT("a refused consent read fails Unavailable"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("Unavailable")));
		TestEqual(TEXT("and shows the SDK's reason"), F.Dock->GetReasonText().ToString(), Refused.Message);
		TestEqual(TEXT("and reads no project"), Fake.Count(TEXT("ListProjects")), 0);
	}
	{
		FFakePlatform Fake;
		FRig F;
		FAccess::UsePlatform(*F.Dock, Fake.Calls);
		F.Dock->Open(100, FText::FromString(TEXT("Chunk 1, 2, 3")), TEXT("p-9"));
		Answer(Fake.Boots, MakeBoot());
		Answer(Fake.Consents, MakeConsent(true));
		FCrowdyStudioProjectInfo Missing = MakeInfo(FCrowdyStudioProject(), ECrowdyStudioResult::NotFound);
		Missing.Message = TEXT("No project p-9 for this player.");
		Answer(Fake.Projects, Missing);
		TestEqual(TEXT("a project that is not found fails Unavailable"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("Unavailable")));
		TestEqual(TEXT("and shows the SDK's reason"), F.Dock->GetReasonText().ToString(), Missing.Message);
		TestTrue(TEXT("and loads no page"), FAccess::Bridge(*F.Dock) == nullptr);
	}
	{
		FFakePlatform Fake;
		FRig F;
		FAccess::UsePlatform(*F.Dock, Fake.Calls);
		F.Open();
		Answer(Fake.Boots, MakeBoot());
		Answer(Fake.Consents, MakeConsent(true));
		Answer(Fake.Lists, MakeList({}, ECrowdyStudioResult::Failed));
		TestEqual(TEXT("a failed project read fails Unavailable"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("Unavailable")));
		TestTrue(TEXT("and loads no page"), FAccess::Bridge(*F.Dock) == nullptr);
	}
	{
		FFakePlatform Fake;
		FRig F;
		FAccess::UsePlatform(*F.Dock, Fake.Calls);
		F.Open();
		Answer(Fake.Boots, MakeBoot());
		Answer(Fake.Consents, MakeConsent(true));
		Answer(Fake.Lists, MakeList({}));
		TestTrue(TEXT("a player with no project still gets the page"), FAccess::Bridge(*F.Dock) != nullptr);
		TestEqual(TEXT("still Booting until the harness answers"), LiveStateOf(F), FString(TEXT("Booting")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveTimeoutTest, "Construct.StudioDock.Live.BootTimesOutAfterSixtySeconds", LiveFlags)
bool FConstructStudioDockLiveTimeoutTest::RunTest(const FString& Parameters)
{
	FFakePlatform Fake;
	FRig F;
	FAccess::UsePlatform(*F.Dock, Fake.Calls);
	F.Open();
	FAccess::Advance(*F.Dock, 59.f);
	TestEqual(TEXT("a live boot is not the inert one"), LiveStateOf(F), FString(TEXT("Booting")));
	FAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("at 60 s it fails"), LiveStateOf(F), FString(TEXT("Failed")));
	TestEqual(TEXT("with BootTimedOut"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("BootTimedOut")));
	Answer(Fake.Boots, MakeBoot());
	TestEqual(TEXT("the boot material arriving late changes nothing"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("BootTimedOut")));
	TestEqual(TEXT("and reads nothing more"), Fake.Count(TEXT("Consent")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveCloseRulesTest, "Construct.StudioDock.Live.EachSelfCloseClosesOnce", LiveFlags)
bool FConstructStudioDockLiveCloseRulesTest::RunTest(const FString& Parameters)
{
	{
		FFakePlatform Fake;
		FRig F;
		ExpectClosesOnce(*this, TEXT("sign-out"), F, Fake, [&F]() { FAccess::SignIn(*F.Dock, 0); FAccess::Advance(*F.Dock, 0.016f); }, TEXT("SignedOut"));
	}
	{
		FFakePlatform Fake;
		FRig F;
		ExpectClosesOnce(*this, TEXT("sign-out told by the token"), F, Fake, [this, &F]()
		{
			FAccess::SignIn(*F.Dock, 0);
			FAccess::TokenChanged(*F.Dock, 42, FString());
			TestEqual(TEXT("the token's sign-out closes at once"), LiveStateOf(F), FString(TEXT("Closed")));
		}, TEXT("SignedOut"));
	}
	{
		FFakePlatform Fake;
		FRig F;
		ExpectClosesOnce(*this, TEXT("another player"), F, Fake, [&F]() { FAccess::SignIn(*F.Dock, 43); FAccess::Advance(*F.Dock, 0.016f); }, TEXT("UserChanged"));
	}
	{
		FFakePlatform Fake;
		FRig F;
		ExpectClosesOnce(*this, TEXT("end of app access"), F, Fake, [&F]() { FAccess::EndAccess(*F.Dock); FAccess::Advance(*F.Dock, 0.016f); }, TEXT("AccessEnded"));
	}
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
		FFakePlatform Fake;
		FRig F(true, World);
		ExpectClosesOnce(*this, TEXT("world teardown"), F, Fake, [World]() { World->DestroyWorld(false); }, TEXT("WorldEnded"));
	}
	{
		FFakePlatform Fake;
		FRig F;
		ExpectClosesOnce(*this, TEXT("Slate release"), F, Fake, [&F]() { F.Dock->ReleaseSlateResources(true); }, TEXT("Closed"));
	}
	{
		FFakePlatform Fake;
		FRig F;
		if (TestTrue(TEXT("destruction: the dock reaches Ready"), ReadyLive(F, Fake)))
		{
			RequestPrompt(F);
			const TStrongObjectPtr<UConstructStudioDockBridge> Bridge(FAccess::Bridge(*F.Dock));
			FAccess::SetMainFrame(*F.Dock, ConstructStudioDockScheme::PageUrl);
			F.Dock->ConditionalBeginDestroy();
			TestTrue(TEXT("destruction: the page is released"), FAccess::Bridge(*F.Dock) == nullptr);
			TestEqual(TEXT("destruction: the old page's bridge answers closed"), Bridge.IsValid() ? Bridge->Boot() : FString(), ConstructStudioDockFrames::ErrorReply(TEXT("closed")));
			FRig Next;
			TestEqual(TEXT("destruction: another dock may open"), ConstructStudioDockName(Next.Open()), FString(TEXT("None")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveGenerationTest, "Construct.StudioDock.Live.LateAnswersFromAnOlderBootAreDropped", LiveFlags)
bool FConstructStudioDockLiveGenerationTest::RunTest(const FString& Parameters)
{
	{
		FFakePlatform Fake;
		FRig F;
		FAccess::UsePlatform(*F.Dock, Fake.Calls);
		F.Open();
		F.Dock->Close();
		F.Open();
		TestEqual(TEXT("each Open reads the boot"), Fake.Count(TEXT("Boot")), 2);
		Answer(Fake.Boots, MakeBoot());
		TestEqual(TEXT("the closed boot's material is dropped"), Fake.Count(TEXT("Consent")), 0);
		Answer(Fake.Boots, MakeBoot());
		TestEqual(TEXT("the current boot's is taken"), Fake.Count(TEXT("Consent")), 1);
		F.Dock->Close();
		F.Open();
		Answer(Fake.Consents, MakeConsent(true));
		TestEqual(TEXT("the closed boot's consent is dropped"), Fake.Count(TEXT("ListProjects")), 0);
		TestEqual(TEXT("and the new boot is still reading"), LiveStateOf(F), FString(TEXT("Booting")));
	}
	{
		FFakePlatform Fake;
		FRig F;
		if (!TestTrue(TEXT("the dock reaches Ready"), ReadyLive(F, Fake)))
		{
			return false;
		}
		F.Dock->AnswerDeployPrompt(RequestPrompt(F), true);
		TestEqual(TEXT("Run asks the app's switch"), Fake.CountStarting(TEXT("Authorize:")), 1);
		F.Dock->Close();
		F.Open();
		Answer(Fake.Verdicts, MakeVerdict(true));
		TestEqual(TEXT("the closed boot's verdict starts no run"), Fake.CountStarting(TEXT("Run:")), 0);
		Answer(Fake.Usages, MakeUsage(5));
		TestFalse(TEXT("the closed boot's usage is dropped"), F.Listener->Saw(TEXT("Usage:")));
	}
	{
		FFakePlatform Fake;
		FRig F;
		if (!TestTrue(TEXT("the dock reaches Ready again"), ReadyLive(F, Fake)))
		{
			return false;
		}
		F.Dock->AnswerDeployPrompt(RequestPrompt(F), true);
		Answer(Fake.Verdicts, MakeVerdict(true));
		F.Dock->Close();
		F.Open();
		Answer(Fake.Runs, MakeRun(ECrowdyModResult::Success));
		TestEqual(TEXT("the closed boot's run answer reaches no one"), F.SentCount(TEXT("RunProject:ok")), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveRunTest, "Construct.StudioDock.Live.RunAuthorizesThenRunsThePromptsOwnRevisionAndModules", LiveFlags)
bool FConstructStudioDockLiveRunTest::RunTest(const FString& Parameters)
{
	FFakePlatform Fake;
	FRig F;
	if (!TestTrue(TEXT("the dock reaches Ready"), ReadyLive(F, Fake)))
	{
		return false;
	}
	const int64 PromptId = RequestPrompt(F, EConstructStudioDeployKind::Live);
	if (!TestTrue(TEXT("a prompt is up"), PromptId != 0))
	{
		return false;
	}
	TestEqual(TEXT("at the read revision"), F.Listener->Prompts.Last().Revision, FString(TEXT("rev-1")));
	TestTrue(TEXT("a CLIENT half reaches visitors"), F.Listener->Prompts.Last().bReachesVisitors);

	FAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), FileChanged(TEXT("server"), TEXT("Cargo.toml")));
	FAccess::Advance(*F.Dock, 1.5f);
	Answer(Fake.Projects, MakeInfo(MakeProject(TEXT("rev-1"), TEXT("renamed"), TEXT("renamed-client"))));
	TestEqual(TEXT("a read at the same revision keeps the prompt"), F.Listener->WithdrawnIds.Num(), 0);

	F.Dock->AnswerDeployPrompt(PromptId, true);
	TestEqual(TEXT("Run asks the app's switch for the prompt's kind first"), Fake.Count(TEXT("Authorize:Live")), 1);
	TestEqual(TEXT("and runs nothing before it answers"), Fake.CountStarting(TEXT("Run:")), 0);
	Answer(Fake.Verdicts, MakeVerdict(true));
	TestEqual(TEXT("an allowed run uses the prompt's grid, project, revision and module names"), Fake.Count(TEXT("Run:100:p-1:rev-1:Live:srv:srv-client")), 1);
	TestEqual(TEXT("the slot is held while it runs"), RequestPrompt(F), int64(0));
	Answer(Fake.Runs, MakeRun(ECrowdyModResult::Success));
	TestEqual(TEXT("the run's answer frees the slot"), F.SentCount(TEXT("RunProject:ok")), 1);

	F.Dock->AnswerDeployPrompt(RequestPrompt(F), true);
	Answer(Fake.Verdicts, MakeVerdict(false));
	TestEqual(TEXT("a switch that says no runs nothing"), Fake.CountStarting(TEXT("Run:")), 1);
	TestEqual(TEXT("and is answered as a failed run"), F.SentCount(TEXT("RunProject:failed")), 1);

	F.Dock->AnswerDeployPrompt(RequestPrompt(F), true);
	Answer(Fake.Verdicts, MakeVerdict(true, ECrowdyStudioResult::Failed));
	TestEqual(TEXT("a switch that could not be read runs nothing"), Fake.CountStarting(TEXT("Run:")), 1);
	TestEqual(TEXT("and is answered as a failed run too"), F.SentCount(TEXT("RunProject:failed")), 2);
	TestTrue(TEXT("then a new prompt shows"), RequestPrompt(F) != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveRevisionTest, "Construct.StudioDock.Live.SavedRevisionWithdrawsThePrompt", LiveFlags)
bool FConstructStudioDockLiveRevisionTest::RunTest(const FString& Parameters)
{
	FFakePlatform Fake;
	FRig F;
	if (!TestTrue(TEXT("the dock reaches Ready"), ReadyLive(F, Fake)))
	{
		return false;
	}
	const int64 PromptId = RequestPrompt(F);
	FAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), FileChanged(TEXT("server"), TEXT("src/lib.rs")));
	TestEqual(TEXT("a saved file is not read back at once"), Fake.Count(TEXT("Project:p-1")), 0);
	FAccess::Advance(*F.Dock, 1.f);
	FAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), FileChanged(TEXT("client"), TEXT("src/lib.rs")));
	FAccess::Advance(*F.Dock, 1.f);
	TestEqual(TEXT("nor while the saves keep coming"), Fake.Count(TEXT("Project:p-1")), 0);
	TestEqual(TEXT("both files are listed as changed"), FAccess::ChangedFileCount(*F.Dock), 2);
	FAccess::Advance(*F.Dock, 0.6f);
	TestEqual(TEXT("1.5 s after the last save the project is read once"), Fake.Count(TEXT("Project:p-1")), 1);
	Answer(Fake.Projects, MakeInfo(MakeProject(TEXT("rev-2"))));
	TestTrue(TEXT("a new revision withdraws the prompt"), F.Listener->WithdrawnIds == TArray<int64>({PromptId}));
	TestEqual(TEXT("and refuses it to the agent"), F.SentCount(TEXT("AnswerDeployPrompt:Refused")), 1);
	F.Dock->AnswerDeployPrompt(PromptId, true);
	TestEqual(TEXT("Run on the withdrawn prompt does nothing"), Fake.CountStarting(TEXT("Authorize:")), 0);

	FAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), FileChanged(TEXT("server"), TEXT("src/other.rs")));
	TestEqual(TEXT("a request while a save is unread shows no prompt"), RequestPrompt(F), int64(0));
	TestEqual(TEXT("and is refused as no project"), F.SentCount(TEXT("studio.draftTest:refused: no project")), 1);
	FAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("the save is read back"), Fake.Count(TEXT("Project:p-1")), 2);
	TestEqual(TEXT("a request while that read is in flight shows no prompt"), RequestPrompt(F), int64(0));
	TestEqual(TEXT("and is refused too"), F.SentCount(TEXT("studio.draftTest:refused: no project")), 2);
	FAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), FileChanged(TEXT("client"), TEXT("src/other.rs")));
	FAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("one read at a time"), Fake.Count(TEXT("Project:p-1")), 2);
	Answer(Fake.Projects, MakeInfo(MakeProject(TEXT("rev-3"))));
	FAccess::Advance(*F.Dock, 0.016f);
	TestEqual(TEXT("the save made during a read is read after it"), Fake.Count(TEXT("Project:p-1")), 3);
	Answer(Fake.Projects, MakeInfo(MakeProject(TEXT("rev-4"))));
	RequestPrompt(F);
	TestEqual(TEXT("the agent's next prompt carries the read revision"), F.Listener->Prompts.Last().Revision, FString(TEXT("rev-4")));
	TestEqual(TEXT("and every changed file"), F.Listener->Prompts.Last().ChangedFiles.Num(), 4);

	FAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), MakeShared<FJsonObject>());
	FAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("an event naming no file reads nothing"), Fake.Count(TEXT("Project:p-1")), 3);
	FAccess::WorkerEvent(*F.Dock, TEXT("worker.fileChanged"), FileChanged(TEXT("server"), TEXT("src/third.rs")));
	FAccess::Advance(*F.Dock, 1.5f);
	Answer(Fake.Projects, MakeInfo(FCrowdyStudioProject(), ECrowdyStudioResult::Failed));
	TestEqual(TEXT("a failed read keeps the prompt"), F.Listener->WithdrawnIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveDeclineTest, "Construct.StudioDock.Live.PromptDeclinesAfterSixtySeconds", LiveFlags)
bool FConstructStudioDockLiveDeclineTest::RunTest(const FString& Parameters)
{
	FFakePlatform Fake;
	FRig F;
	if (!TestTrue(TEXT("the dock reaches Ready"), ReadyLive(F, Fake)))
	{
		return false;
	}
	const int64 PromptId = RequestPrompt(F);
	FAccess::Advance(*F.Dock, 59.f);
	TestEqual(TEXT("at 59 s the prompt is still up"), F.Listener->WithdrawnIds.Num(), 0);
	FAccess::Advance(*F.Dock, 1.5f);
	TestTrue(TEXT("at 60 s it withdraws itself"), F.Listener->WithdrawnIds == TArray<int64>({PromptId}));
	TestEqual(TEXT("and declines to the agent"), F.SentCount(TEXT("AnswerDeployPrompt:NotNow")), 1);
	F.Dock->AnswerDeployPrompt(PromptId, true);
	TestEqual(TEXT("a late Run asks nothing"), Fake.CountStarting(TEXT("Authorize:")), 0);
	TestEqual(TEXT("and runs nothing"), Fake.CountStarting(TEXT("Run:")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveUsageTest, "Construct.StudioDock.Live.UsageReadEveryThirtySecondsOnlyWhileOpen", LiveFlags)
bool FConstructStudioDockLiveUsageTest::RunTest(const FString& Parameters)
{
	const FConstructStudioUsage Shown = FConstructStudioDockPlatform::ToUsage(MakeUsage(3));
	TestEqual(TEXT("the payer in lowercase"), Shown.Payer, FString(TEXT("player")));
	TestEqual(TEXT("today's requests"), Shown.TodayRequests, int64(3));
	TestEqual(TEXT("the charge in dollars"), Shown.TodayCharge, FString(TEXT("0.25")));
	TestEqual(TEXT("the limit in dollars"), Shown.DayLimit, FString(TEXT("50.00")));
	TestEqual(TEXT("a millionth keeps its places"), FConstructStudioDockPlatform::DecimalUsd(123), FString(TEXT("0.000123")));
	FCrowdyStudioModelUsage ZeroLimit = MakeUsage(0);
	ZeroLimit.DayLimitMicroUsd = 0;
	TestEqual(TEXT("a zero ceiling shows as zero dollars"), FConstructStudioDockPlatform::ToUsage(ZeroLimit).DayLimit, FString(TEXT("0.00")));

	FFakePlatform Fake;
	FRig F;
	FAccess::UsePlatform(*F.Dock, Fake.Calls);
	F.Open();
	Answer(Fake.Boots, MakeBoot());
	Answer(Fake.Consents, MakeConsent(false));
	FAccess::Advance(*F.Dock, 45.f);
	TestEqual(TEXT("no usage is read before Ready"), Fake.Count(TEXT("Usage")), 0);
	F.Dock->GiveProviderConsent();
	Answer(Fake.Consents, MakeConsent(true));
	Answer(Fake.Lists, MakeList({MakeProject(TEXT("rev-1"))}));
	FAccess::Boot(*F.Dock, FAccess::Generation(*F.Dock), EConstructStudioDockState::Ready);
	TestEqual(TEXT("Ready reads usage at once"), Fake.Count(TEXT("Usage")), 1);
	Answer(Fake.Usages, MakeUsage(3));
	TestTrue(TEXT("and reports it"), F.Listener->Lines.Contains(TEXT("Usage:3")));
	FAccess::Advance(*F.Dock, 29.f);
	TestEqual(TEXT("not again before 30 s"), Fake.Count(TEXT("Usage")), 1);
	FAccess::Advance(*F.Dock, 1.5f);
	TestEqual(TEXT("again at 30 s"), Fake.Count(TEXT("Usage")), 2);
	FAccess::Advance(*F.Dock, 30.f);
	TestEqual(TEXT("no second read while one is unanswered"), Fake.Count(TEXT("Usage")), 2);
	Answer(Fake.Usages, MakeUsage(5));
	TestTrue(TEXT("the late answer is reported"), F.Listener->Lines.Contains(TEXT("Usage:5")));
	FAccess::Advance(*F.Dock, 0.016f);
	TestEqual(TEXT("and the next tick reads again"), Fake.Count(TEXT("Usage")), 3);
	F.Dock->Close();
	FAccess::Advance(*F.Dock, 60.f);
	TestEqual(TEXT("never once closed"), Fake.Count(TEXT("Usage")), 3);
	Answer(Fake.Usages, MakeUsage(4));
	TestFalse(TEXT("and an answer after Close is dropped"), F.Listener->Lines.Contains(TEXT("Usage:4")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLivePageEventsTest, "Construct.StudioDock.Live.SharedScreenshotAndGridReachThePage", LiveFlags)
bool FConstructStudioDockLivePageEventsTest::RunTest(const FString& Parameters)
{
	FFakePlatform Fake;
	FRig F;
	if (!TestTrue(TEXT("the dock reaches Ready"), ReadyLive(F, Fake)))
	{
		return false;
	}
	TArray<FString> Scripts;
	FAccess::SetMainFrame(*F.Dock, ConstructStudioDockScheme::PageUrl);
	FAccess::CaptureScripts(*F.Dock, [&Scripts](const FString& Script) { Scripts.Add(Script); });
	UConstructStudioDockBridge* Bridge = FAccess::Bridge(*F.Dock);
	TSharedPtr<FJsonObject> Boot;
	FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Bridge->Boot()), Boot);
	if (!TestTrue(TEXT("the page boots"), Boot.IsValid() && Boot->HasTypedField<EJson::String>(TEXT("nonce"))))
	{
		return false;
	}
	Bridge->Send(LivePageNotice(Boot->GetStringField(TEXT("nonce")), TEXT("joined")));
	TestEqual(TEXT("joining says hello"), Scripts.Num(), 1);

	F.Dock->ShareScreenshot();
	if (TestEqual(TEXT("the player's share asks the host"), F.Listener->Captures.Num(), 1))
	{
		FConstructStudioCapture Capture;
		Capture.bCaptured = true;
		Capture.PngBase64 = TEXT("iVBORw0KGgo=");
		Capture.Width = 640;
		Capture.Height = 360;
		Capture.Caption = TEXT("Player screenshot");
		UConstructStudioDock::FinishCapture(F.Listener->Captures[0], Capture);
	}
	TestTrue(TEXT("the shot reaches the page as page.capture with its bytes"), Scripts.Num() == 2 && Scripts[1].Contains(TEXT("\"event\":\"page.capture\""))
		&& Scripts[1].Contains(TEXT("\"bytesBase64\":\"iVBORw0KGgo=\"")) && Scripts[1].Contains(TEXT("\"mediaType\":\"image/png\"")));

	F.Dock->SetGrid(200, FText::FromString(TEXT("Chunk 4, 5, 6")));
	TestTrue(TEXT("a new grid reaches the page as its context"), Scripts.Num() == 3 && Scripts[2].Contains(TEXT("\"event\":\"page.context\""))
		&& Scripts[2].Contains(TEXT("\"gridId\":\"200\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockLiveOpeningGridTest, "Construct.StudioDock.Live.GreetedReadySendsTheOpeningGrid", LiveFlags)
bool FConstructStudioDockLiveOpeningGridTest::RunTest(const FString& Parameters)
{
	FFakePlatform Fake;
	FRig F;
	if (!TestTrue(TEXT("the dock reaches Ready"), ReadyLive(F, Fake)))
	{
		return false;
	}
	TArray<FString> Scripts;
	FAccess::SetMainFrame(*F.Dock, ConstructStudioDockScheme::PageUrl);
	FAccess::CaptureScripts(*F.Dock, [&Scripts](const FString& Script) { Scripts.Add(Script); });
	TSharedPtr<FJsonObject> Boot;
	FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(FAccess::Bridge(*F.Dock)->Boot()), Boot);
	if (!TestTrue(TEXT("the page boots"), Boot.IsValid() && Boot->HasTypedField<EJson::String>(TEXT("nonce"))))
	{
		return false;
	}
	FAccess::Bridge(*F.Dock)->Send(LivePageNotice(Boot->GetStringField(TEXT("nonce")), TEXT("joined")));
	TestEqual(TEXT("joining says hello"), Scripts.Num(), 1);

	FAccess::WorkerEvent(*F.Dock, TEXT("worker.ready"), MakeShared<FJsonObject>());
	TestEqual(TEXT("the worker's first ready gets two frames"), Scripts.Num(), 3);
	TestTrue(TEXT("page.hello first"), Scripts.Num() == 3 && Scripts[1].Contains(TEXT("\"event\":\"page.hello\"")));
	TestTrue(TEXT("then page.context with the grid the dock opened on"), Scripts.Num() == 3 && Scripts[2].Contains(TEXT("\"event\":\"page.context\""))
		&& Scripts[2].Contains(TEXT("\"gridId\":\"100\"")) && Scripts[2].Contains(TEXT("\"label\":\"Chunk 1, 2, 3\"")));
	FAccess::WorkerEvent(*F.Dock, TEXT("worker.ready"), MakeShared<FJsonObject>());
	TestEqual(TEXT("the ready that answers that hello sends neither"), Scripts.Num(), 3);
	return true;
}

#endif
