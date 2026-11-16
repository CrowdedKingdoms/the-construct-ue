#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioDockTestAccess.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "StandInC11.h"
#include "UObject/StrongObjectPtr.h"

namespace StandInC11Tests
{
	constexpr EAutomationTestFlags StandInFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using FDockAccess = FConstructStudioDockTestAccess;

	const TCHAR* const Fixtures[] = { TEXT("consent"), TEXT("ready"), TEXT("deploy"), TEXT("revision"), TEXT("failure"), TEXT("capture"), TEXT("warnings") };

	/** A rig whose dock replays the named fixture; the stand-in selects nothing again when it goes. */
	struct FReplay
	{
		FConstructStudioDockTestRig Rig;
		TStrongObjectPtr<UStandInC11> StandIn;

		explicit FReplay(const TCHAR* Fixture)
			: StandIn(NewObject<UStandInC11>(GetTransientPackage()))
		{
			StandIn->SelectTimelineForStandIn(FName(Fixture));
			Rig.Open();
		}

		~FReplay()
		{
			StandIn->SelectTimelineTextForStandIn(TEXT("[]"));
		}

		/** Ticks the stand-in and the dock together in 100 ms steps, giving consent whenever the dock asks for it. */
		void Run(float Seconds, bool bConsent = false)
		{
			const int32 Count = FMath::RoundToInt(Seconds * 10.f);
			for (int32 Step = 0; Step < Count; ++Step)
			{
				StandIn->Tick(0.1f);
				FDockAccess::Advance(*Rig.Dock, 0.1f);
				if (bConsent && Rig.Dock->GetState() == EConstructStudioDockState::NeedsConsent)
				{
					Rig.Dock->GiveProviderConsent();
				}
			}
		}

		FString State() const
		{
			return ConstructStudioDockName(Rig.Dock->GetState());
		}

		int64 LastPrompt() const
		{
			return Rig.Listener->Prompts.IsEmpty() ? 0 : Rig.Listener->Prompts.Last().PromptId;
		}
	};
}

using namespace StandInC11Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11FixturesTest, "Construct.StudioDock.StandIn.EveryFixtureParses", StandInFlags)
bool FStandInC11FixturesTest::RunTest(const FString& Parameters)
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(FPaths::ProjectContentDir() / TEXT("StandIn/C11/*.json")), true, false);
	for (const TCHAR* Fixture : Fixtures)
	{
		TestTrue(*FString::Printf(TEXT("%s.json ships"), Fixture),Files.Contains(FString(Fixture) + TEXT(".json")));
	}
	TStrongObjectPtr<UStandInC11> StandIn(NewObject<UStandInC11>(GetTransientPackage()));
	for (const FString& File : Files)
	{
		TestTrue(*FString::Printf(TEXT("%s parses"), *File),StandIn->SelectTimelineForStandIn(FName(*FPaths::GetBaseFilename(File))));
	}
	StandIn->SelectTimelineTextForStandIn(TEXT("[]"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11UnknownStepTest, "Construct.StudioDock.StandIn.UnknownStepIsReported", StandInFlags)
bool FStandInC11UnknownStepTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("OnTeleport is not a known event"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("waitFor Teleport is not"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("AnswerDeployPrompt branches are named Run or NotNow"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("studio.projectOpen needs args.ProjectId as a JSON string"), EAutomationExpectedErrorFlags::Contains, 1);
	TStrongObjectPtr<UStandInC11> StandIn(NewObject<UStandInC11>(GetTransientPackage()));
	TestFalse(TEXT("a numeric project id is refused"), StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "atMs": 0, "event": "studio.projectOpen", "args": { "ProjectId": 504, "ProjectName": "P", "Revision": "rev-1" } } ])json")));
	TestFalse(TEXT("an unknown event is refused"), StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "atMs": 0, "event": "OnTeleport", "args": {} } ])json")));
	TestFalse(TEXT("an unknown checkpoint is refused"), StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "waitFor": "Teleport", "branches": {} } ])json")));
	TestFalse(TEXT("an unknown answer branch is refused"), StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "waitFor": "AnswerDeployPrompt", "branches": { "Maybe": [] } } ])json")));
	FConstructStudioDockTestRig Rig;
	Rig.Open();
	FDockAccess::Advance(*Rig.Dock, 0.016f);
	TestEqual(TEXT("with nothing selected the dock stays inert"), ConstructStudioDockName(Rig.Dock->GetReason()), FString(TEXT("NotBuiltYet")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11ConsentTest, "Construct.StudioDock.StandIn.ConsentWaitsForThePlayer", StandInFlags)
bool FStandInC11ConsentTest::RunTest(const FString& Parameters)
{
	FReplay R(TEXT("consent"));
	R.Run(1.f);
	TestEqual(TEXT("the boot asks for consent"), R.State(), FString(TEXT("NeedsConsent")));
	TestFalse(TEXT("with the platform's notice"), R.Rig.Dock->GetConsentNotice().IsEmpty());
	R.Run(30.f);
	TestEqual(TEXT("and waits however long"), R.State(), FString(TEXT("NeedsConsent")));
	R.Rig.Dock->GiveProviderConsent();
	TestEqual(TEXT("consent goes back to Booting"), R.State(), FString(TEXT("Booting")));
	R.Run(1.4f);
	TestEqual(TEXT("branch times count from the answer"), R.State(), FString(TEXT("Booting")));
	R.Run(0.2f);
	TestEqual(TEXT("then Ready"), R.State(), FString(TEXT("Ready")));
	TestTrue(TEXT("with a usage line"), R.Rig.Listener->Saw(TEXT("Usage:")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11DeployTest, "Construct.StudioDock.StandIn.DeployTakesTheAnsweredBranch", StandInFlags)
bool FStandInC11DeployTest::RunTest(const FString& Parameters)
{
	{
		FReplay R(TEXT("deploy"));
		R.Run(5.1f);
		if (!TestEqual(TEXT("a draft-test prompt shows"), R.Rig.Listener->Prompts.Num(), 1))
		{
			return false;
		}
		const FConstructStudioDeployPrompt First = R.Rig.Listener->Prompts[0];
		TestEqual(TEXT("at the last saved revision"), First.Revision, FString(TEXT("rev-3")));
		TestEqual(TEXT("with both changed files"), First.ChangedFiles.Num(), 2);
		TestTrue(TEXT("reaching visitors"), First.bReachesVisitors);
		R.Rig.Dock->AnswerDeployPrompt(First.PromptId, true);
		R.Run(4.1f);
		TestEqual(TEXT("Run is answered by the run"), R.Rig.SentCount(TEXT("RunProject:ok")), 1);
		R.Run(7.1f);
		if (!TestEqual(TEXT("then a live prompt"), R.Rig.Listener->Prompts.Num(), 2))
		{
			return false;
		}
		TestEqual(TEXT("of kind Live"), ConstructStudioDockName(R.Rig.Listener->Prompts[1].Kind), FString(TEXT("Live")));
		R.Run(61.f);
		TestTrue(TEXT("left alone it withdraws itself"), R.Rig.Listener->WithdrawnIds.Contains(R.LastPrompt()));
		TestTrue(TEXT("and the Not now branch replays"), R.Rig.Listener->Saw(TEXT("Warning:Stand-in: the live deploy")));
	}
	{
		FReplay R(TEXT("deploy"));
		R.Run(5.1f);
		R.Rig.Dock->AnswerDeployPrompt(R.LastPrompt(), false);
		R.Run(2.1f);
		TestEqual(TEXT("Not now goes straight to the live prompt"), R.Rig.Listener->Prompts.Num(), 2);
		TestEqual(TEXT("and nothing ran"), R.Rig.SentCount(TEXT("AnswerDeployPrompt:Run")), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11RevisionTest, "Construct.StudioDock.StandIn.RevisionChangeReplacesThePrompt", StandInFlags)
bool FStandInC11RevisionTest::RunTest(const FString& Parameters)
{
	FReplay R(TEXT("revision"));
	R.Run(3.1f);
	const int64 First = R.LastPrompt();
	TestTrue(TEXT("a prompt shows"), First > 0);
	R.Run(2.f);
	TestTrue(TEXT("a save withdraws it"), R.Rig.Listener->WithdrawnIds.Contains(First));
	R.Run(1.f);
	TestTrue(TEXT("the agent asks again as a new prompt"), R.LastPrompt() > First);
	TestEqual(TEXT("for the new revision"), R.Rig.Listener->Prompts.Last().Revision, FString(TEXT("rev-2")));
	R.Rig.Dock->AnswerDeployPrompt(R.LastPrompt(), true);
	R.Run(3.1f);
	TestEqual(TEXT("Run on the replacement runs"), R.Rig.SentCount(TEXT("RunProject:ok")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11FailureTest, "Construct.StudioDock.StandIn.FailureKeepsItsReason", StandInFlags)
bool FStandInC11FailureTest::RunTest(const FString& Parameters)
{
	FReplay R(TEXT("failure"));
	R.Run(2.9f);
	TestEqual(TEXT("the replayed boot does not fail as inert"), R.State(), FString(TEXT("Booting")));
	R.Run(0.2f);
	TestEqual(TEXT("it fails"), R.State(), FString(TEXT("Failed")));
	TestEqual(TEXT("with the recorded reason"), ConstructStudioDockName(R.Rig.Dock->GetReason()), FString(TEXT("NoModel")));
	R.Rig.Dock->Close();
	TestEqual(TEXT("and Close returns it to Closed"), R.State(), FString(TEXT("Closed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11CaptureTest, "Construct.StudioDock.StandIn.CaptureReachesTheHost", StandInFlags)
bool FStandInC11CaptureTest::RunTest(const FString& Parameters)
{
	FReplay R(TEXT("capture"));
	R.Run(3.1f);
	TestTrue(TEXT("the registered host is asked"), R.Rig.Listener->Lines.Contains(TEXT("Capture:Stand-in: what the player sees")));
	if (!TestEqual(TEXT("one capture is pending"), R.Rig.Listener->Captures.Num(), 1))
	{
		return false;
	}
	FConstructStudioCapture Capture;
	Capture.bCaptured = true;
	Capture.Width = 1280;
	Capture.Height = 720;
	UConstructStudioDock::FinishCapture(R.Rig.Listener->Captures[0], Capture);
	TestEqual(TEXT("the answer arrives"), R.Rig.SentCount(TEXT("studio.screenshot:captured 1280x720")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC11EveryEventTest, "Construct.StudioDock.StandIn.DrivesEveryEvent", StandInFlags)
bool FStandInC11EveryEventTest::RunTest(const FString& Parameters)
{
	TSet<FString> Seen;
	for (const TCHAR* Fixture : Fixtures)
	{
		FReplay R(Fixture);
		R.Run(320.f, true);
		for (const FString& Line : R.Rig.Listener->Lines)
		{
			FString Kind;
			Seen.Add(Line.Split(TEXT(":"), &Kind, nullptr) ? Kind : Line);
		}
		if (FCString::Strcmp(Fixture, TEXT("warnings")) == 0)
		{
			R.Rig.Dock->Close();
			R.Run(1.f);
			TestFalse(TEXT("a step from the closed boot is dropped"), R.Rig.Listener->Saw(TEXT("Warning:Stand-in: this warning belongs")));
		}
	}
	const TCHAR* const Events[] = { TEXT("State"), TEXT("Prompt"), TEXT("Withdrawn"), TEXT("Usage"), TEXT("Warning"), TEXT("Focus"), TEXT("CloseRequested"), TEXT("Capture") };
	for (const TCHAR* Event : Events)
	{
		TestTrue(*FString::Printf(TEXT("the stand-in drives %s"), Event),Seen.Contains(Event));
	}
	return true;
}

#endif
