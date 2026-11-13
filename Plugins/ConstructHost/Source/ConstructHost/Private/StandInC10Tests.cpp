#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "StandInC10.h"
#include "StandInC10TestHost.h"
#include "UObject/StrongObjectPtr.h"

namespace StandInC10Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FFixture
	{
		TStrongObjectPtr<UStandInC10> StandIn;
		TStrongObjectPtr<UStandInC10TestHost> Host;

		FFixture()
			: StandIn(NewObject<UStandInC10>(GetTransientPackage()))
			, Host(NewObject<UStandInC10TestHost>(GetTransientPackage()))
		{
			StandIn->SetHost(TScriptInterface<ICrowdyModScriptHost>(Host.Get()));
			StandIn->OnModScriptPrompt.AddDynamic(Host.Get(), &UStandInC10TestHost::OnPrompt);
			StandIn->OnModScriptPromptWithdrawn.AddDynamic(Host.Get(), &UStandInC10TestHost::OnPromptWithdrawn);
			StandIn->OnModScriptStarted.AddDynamic(Host.Get(), &UStandInC10TestHost::OnStarted);
			StandIn->OnModScriptStopped.AddDynamic(Host.Get(), &UStandInC10TestHost::OnStopped);
			StandIn->OnModScriptError.AddDynamic(Host.Get(), &UStandInC10TestHost::OnError);
		}

		FString Lines() const
		{
			return FString::Join(Host->Lines, TEXT(" | "));
		}
	};

	const TCHAR* const Timed = TEXT(R"json([
		{ "atMs": 0, "event": "OnModScriptStarted", "args": { "Script": { "ModId": "g", "Name": "Greeter" } } },
		{ "atMs": 100, "call": "ShowHud", "args": { "Script": { "ModId": "g" }, "Text": "Hello" } },
		{ "atMs": 200, "call": "ShowHud", "args": { "Script": { "ModId": "g" }, "Text": "Again" } }
	])json");

	const TCHAR* const Consent = TEXT(R"json([
		{ "atMs": 0, "event": "OnModScriptPrompt", "args": { "Prompt": { "PromptId": 7, "Scripts": ["Greeter"], "Groups": ["Present"] } } },
		{ "waitFor": "AnswerPrompt", "branches": {
			"Run": [
				{ "atMs": 0, "event": "OnModScriptStarted", "args": { "Script": { "ModId": "g" } } },
				{ "atMs": 500, "call": "ShowHud", "args": { "Script": { "ModId": "g" }, "Text": "Late" } }
			],
			"NotNow": [ { "atMs": 0, "call": "ShowHud", "args": { "Script": { "ModId": "n" }, "Text": "declined" } } ]
		} },
		{ "atMs": 0, "call": "ShowHud", "args": { "Script": { "ModId": "never" }, "Text": "unreachable" } }
	])json");

	const TCHAR* const Offsets = TEXT(R"json([
		{ "atMs": 0, "call": "ReadChunk", "args": { "ChunkX": 0, "ChunkY": 0, "ChunkZ": 0 } },
		{ "atMs": 0, "call": "SetVoxel", "args": { "Script": { "ModId": "p" }, "Write": { "ChunkX": 0, "ChunkY": 0, "ChunkZ": 0, "Voxel": { "Position": { "X": 1, "Y": 2, "Z": 3 }, "Type": 5 } } } },
		{ "atMs": 0, "call": "DrawOverlay", "args": { "Script": { "ModId": "o" }, "Shapes": [ { "Id": "s", "Kind": "Sphere", "Location": { "X": 1, "Y": 2, "Z": 3 } } ] } },
		{ "atMs": 0, "call": "ListPlayers", "args": {} }
	])json");

	const TCHAR* const Revokes = TEXT(R"json([
		{ "atMs": 0, "event": "OnModScriptStarted", "args": { "Script": { "ModId": "a" } } },
		{ "atMs": 0, "event": "OnModScriptStarted", "args": { "Script": { "ModId": "b" } } },
		{ "waitFor": "Revoke", "branches": {
			"b": [ { "atMs": 0, "event": "OnModScriptStopped", "args": { "Script": { "ModId": "b" }, "Reason": "Revoked" } } ],
			"Any": [ { "atMs": 0, "event": "OnModScriptStopped", "args": { "Script": { "ModId": "a" }, "Reason": "Revoked" } } ]
		} }
	])json");

	const TCHAR* const Leaving = TEXT(R"json([
		{ "atMs": 0, "event": "OnModScriptStarted", "args": { "Script": { "ModId": "g" } } },
		{ "waitFor": "LeaveGrid", "branches": { "Any": [ { "atMs": 0, "call": "ShowHud", "args": { "Script": { "ModId": "g" }, "Text": "bye" } } ] } }
	])json");
}

using namespace StandInC10Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10TimingTest, "Construct.StandIn.C10.ReplaysStepsAtTheirTimes", Flags)
bool FStandInC10TimingTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TestTrue(TEXT("the timeline parses"), F.StandIn->SelectTimelineTextForStandIn(Timed));
	F.StandIn->EnterGrid(10, 20, 0);
	TestEqual(TEXT("nothing replays before the first tick"), F.Host->Lines.Num(), 0);
	F.StandIn->Tick(0.05f);
	TestEqual(TEXT("only the start is due at 50 ms"), F.Lines(), FString(TEXT("Started:g#1")));
	F.StandIn->Tick(0.06f);
	TestEqual(TEXT("the first HUD line is due at 110 ms"), F.Lines(), FString(TEXT("Started:g#1 | ShowHud:g:Hello")));
	F.StandIn->Tick(0.1f);
	TestEqual(TEXT("the second HUD line is due at 210 ms"), F.Host->Lines.Num(), 3);
	TestEqual(TEXT("the started script is listed"), F.StandIn->GetRunning().Num(), 1);
	TestTrue(TEXT("the stand-in reports itself available"), F.StandIn->IsAvailable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10CheckpointTest, "Construct.StandIn.C10.CheckpointTakesTheAnsweredBranch", Flags)
bool FStandInC10CheckpointTest::RunTest(const FString& Parameters)
{
	{
		FFixture F;
		F.StandIn->SelectTimelineTextForStandIn(Consent);
		F.StandIn->EnterGrid(0, 0, 0);
		F.StandIn->Tick(30.f);
		TestEqual(TEXT("the checkpoint holds after the prompt"), F.Lines(), FString(TEXT("Prompt:7")));
		F.StandIn->Revoke(TEXT("g"));
		F.StandIn->ForgetAuthor(1001);
		F.StandIn->AnswerPrompt(8, true);
		TestEqual(TEXT("other calls and another prompt's answer are ignored"), F.Lines(), FString(TEXT("Prompt:7")));
		F.StandIn->AnswerPrompt(7, true);
		TestEqual(TEXT("Run replays its branch at once"), F.Lines(), FString(TEXT("Prompt:7 | Started:g#1")));
		F.StandIn->Tick(0.4f);
		TestEqual(TEXT("branch times count from the answer, not the start"), F.Host->Lines.Num(), 2);
		F.StandIn->Tick(0.2f);
		TestEqual(TEXT("the branch's later step is due 500 ms after the answer"), F.Lines(), FString(TEXT("Prompt:7 | Started:g#1 | ShowHud:g:Late")));
		F.StandIn->Tick(5.f);
		TestEqual(TEXT("the branch replaces the rest of the timeline"), F.Host->Lines.Num(), 3);
	}
	{
		FFixture F;
		F.StandIn->SelectTimelineTextForStandIn(Consent);
		F.StandIn->EnterGrid(0, 0, 0);
		F.StandIn->Tick(1.f);
		F.StandIn->AnswerPrompt(7, false);
		TestEqual(TEXT("Not now replays its own branch"), F.Lines(), FString(TEXT("Prompt:7 | ShowHud:n:declined")));
		TestEqual(TEXT("nothing started"), F.StandIn->GetRunning().Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10ReentrantTest, "Construct.StandIn.C10.AnswerFromInsideThePromptIsTaken", Flags)
bool FStandInC10ReentrantTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.Host->AnswerFrom = F.StandIn.Get();
	F.StandIn->SelectTimelineTextForStandIn(Consent);
	F.StandIn->EnterGrid(0, 0, 0);
	F.StandIn->Tick(0.01f);
	TestEqual(TEXT("an answer given inside the prompt event takes its branch"), F.Lines(), FString(TEXT("Prompt:7 | Started:g#1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10OffsetsTest, "Construct.StandIn.C10.CoordinatesAreOffsetsFromTheEnteredChunk", Flags)
bool FStandInC10OffsetsTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.StandIn->SelectTimelineTextForStandIn(Offsets);
	F.StandIn->EnterGrid(10, 20, -1);
	F.StandIn->Tick(0.01f);
	const FCrowdyModScriptGrid Grid = F.StandIn->GetGrid();
	TestTrue(TEXT("the grid reaches two chunks around the entered chunk"),
		Grid.GridId == 1 && Grid.LowChunkX == 8 && Grid.HighChunkX == 12 && Grid.HighChunkY == 22 && Grid.LowChunkZ == -3);
	TestEqual(TEXT("chunk reads, writes and shapes move with the entered chunk"), F.Lines(),
		FString(TEXT("ReadChunk:10,20,-1 | SetVoxel:p:10,20,-1:5 | DrawOverlay:o:s Sphere 161,322,-13 | ListPlayers:1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10VisitTest, "Construct.StandIn.C10.MovingInsideTheGridKeepsTheReplay", Flags)
bool FStandInC10VisitTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.StandIn->SelectTimelineTextForStandIn(Timed);
	F.StandIn->EnterGrid(0, 0, 0);
	F.StandIn->Tick(0.01f);
	F.StandIn->EnterGrid(2, -2, 1);
	F.StandIn->Tick(0.14f);
	TestEqual(TEXT("a chunk inside the grid neither stops nor restarts"), F.Lines(), FString(TEXT("Started:g#1 | ShowHud:g:Hello")));
	F.StandIn->EnterGrid(3, 0, 0);
	F.StandIn->Tick(0.01f);
	TestEqual(TEXT("a chunk outside stops the old visit and starts a new generation"), F.Lines(),
		FString(TEXT("Started:g#1 | ShowHud:g:Hello | ClearScript:g | Stopped:g:LeftGrid | Started:g#2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10LeaveTest, "Construct.StandIn.C10.LeavingStopsWhatRunsAndWithdrawsPrompts", Flags)
bool FStandInC10LeaveTest::RunTest(const FString& Parameters)
{
	{
		FFixture F;
		F.StandIn->SelectTimelineTextForStandIn(Timed);
		F.StandIn->EnterGrid(0, 0, 0);
		F.StandIn->Tick(0.01f);
		F.StandIn->LeaveGrid();
		TestEqual(TEXT("leaving clears and stops the running script"), F.Lines(), FString(TEXT("Started:g#1 | ClearScript:g | Stopped:g:LeftGrid")));
		TestEqual(TEXT("nothing is listed"), F.StandIn->GetRunning().Num(), 0);
		TestEqual(TEXT("the grid is empty"), F.StandIn->GetGrid().GridId, int64(0));
		F.StandIn->Tick(5.f);
		TestEqual(TEXT("the replay ended"), F.Host->Lines.Num(), 3);
	}
	{
		FFixture F;
		F.StandIn->SelectTimelineTextForStandIn(Consent);
		F.StandIn->EnterGrid(0, 0, 0);
		F.StandIn->Tick(0.01f);
		F.StandIn->LeaveGrid();
		F.StandIn->AnswerPrompt(7, true);
		TestEqual(TEXT("leaving withdraws the open prompt and a late answer does nothing"), F.Lines(), FString(TEXT("Prompt:7 | Withdrawn:7")));
	}
	{
		FFixture F;
		F.StandIn->SelectTimelineTextForStandIn(Leaving);
		F.StandIn->EnterGrid(0, 0, 0);
		F.StandIn->Tick(0.01f);
		F.StandIn->LeaveGrid();
		TestEqual(TEXT("a LeaveGrid checkpoint replays its branch, then the grid is left"), F.Lines(),
			FString(TEXT("Started:g#1 | ShowHud:g:bye | ClearScript:g | Stopped:g:LeftGrid")));
		TestEqual(TEXT("the grid is empty after the branch"), F.StandIn->GetGrid().GridId, int64(0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10RevokeTest, "Construct.StandIn.C10.RevokeTakesTheRevokedModsBranch", Flags)
bool FStandInC10RevokeTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.StandIn->SelectTimelineTextForStandIn(Revokes);
	F.StandIn->EnterGrid(0, 0, 0);
	F.StandIn->Tick(0.01f);
	F.StandIn->Revoke(TEXT("b"));
	TestEqual(TEXT("the branch named for the mod replays"), F.Lines(), FString(TEXT("Started:a#1 | Started:b#1 | Stopped:b:Revoked")));
	const TArray<FCrowdyModScript> Running = F.StandIn->GetRunning();
	TestTrue(TEXT("only the revoked script left the list"), Running.Num() == 1 && Running[0].ModId == TEXT("a"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10HostTest, "Construct.StandIn.C10.ClearedHostGetsNoCalls", Flags)
bool FStandInC10HostTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TStrongObjectPtr<UStandInC10TestHost> Other(NewObject<UStandInC10TestHost>(GetTransientPackage()));
	F.StandIn->ClearHost(TScriptInterface<ICrowdyModScriptHost>(Other.Get()));
	F.StandIn->SelectTimelineTextForStandIn(Timed);
	F.StandIn->EnterGrid(0, 0, 0);
	F.StandIn->Tick(0.15f);
	TestEqual(TEXT("clearing another host keeps this one"), F.Lines(), FString(TEXT("Started:g#1 | ShowHud:g:Hello")));
	F.StandIn->ClearHost(TScriptInterface<ICrowdyModScriptHost>(F.Host.Get()));
	F.StandIn->Tick(0.1f);
	TestEqual(TEXT("a cleared host gets no call"), F.Host->Lines.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10MalformedTest, "Construct.StandIn.C10.RefusesAMalformedTimeline", Flags)
bool FStandInC10MalformedTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(TEXT("the timeline is not a JSON array"));
	AddExpectedErrorPlain(TEXT("waitFor Answer is not AnswerPrompt"));
	AddExpectedErrorPlain(TEXT("AnswerPrompt branches are named Run or NotNow"));
	AddExpectedErrorPlain(TEXT("Nope is not a known host call"));
	AddExpectedErrorPlain(TEXT("ShowHud needs a numeric atMs"));
	AddExpectedErrorPlain(TEXT("call ShowHud lacks an argument"));
	AddExpectedErrorPlain(TEXT("event OnModScriptStopped lacks an argument"));
	FFixture F;
	TestFalse(TEXT("text that is not an array is refused"), F.StandIn->SelectTimelineTextForStandIn(TEXT("{}")));
	TestFalse(TEXT("an unknown checkpoint is refused"), F.StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "waitFor": "Answer", "branches": {} } ])json")));
	TestFalse(TEXT("a misnamed answer branch is refused"), F.StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "waitFor": "AnswerPrompt", "branches": { "Not now": [] } } ])json")));
	TestFalse(TEXT("an unknown host call is refused"), F.StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "atMs": 0, "call": "Nope" } ])json")));
	TestFalse(TEXT("a string time is refused"), F.StandIn->SelectTimelineTextForStandIn(TEXT(R"json([ { "atMs": "100", "call": "ShowHud" } ])json")));
	F.StandIn->EnterGrid(0, 0, 0);
	F.StandIn->Tick(1.f);
	TestEqual(TEXT("a refused timeline leaves nothing to replay"), F.Host->Lines.Num(), 0);
	F.StandIn->LeaveGrid();

	TestTrue(TEXT("a well-formed timeline with bad arguments still loads"), F.StandIn->SelectTimelineTextForStandIn(TEXT(R"json([
		{ "atMs": 0, "call": "ShowHud", "args": { "Script": { "ModId": "g" } } },
		{ "atMs": 0, "event": "OnModScriptStopped", "args": { "Script": { "ModId": "g" }, "Reason": "Bored" } }
	])json")));
	F.StandIn->EnterGrid(0, 0, 0);
	F.StandIn->Tick(0.01f);
	TestEqual(TEXT("neither step reached the host"), F.Host->Lines.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC10FixturesTest, "Construct.StandIn.C10.ShippedTimelinesReplay", Flags)
bool FStandInC10FixturesTest::RunTest(const FString& Parameters)
{
	struct FExpected
	{
		const TCHAR* Name;
		bool bRun;
		int32 Lines;
		const TCHAR* Contains;
	};
	const FExpected Cases[] = {
		{ TEXT("greeter"), true, 61, TEXT("ShowHud:standin-greeter:Hello from the greeter (60)") },
		{ TEXT("clicker"), true, 302, TEXT("ShowHud:standin-clicker:Clicker: click anywhere") },
		{ TEXT("painter"), true, 11, TEXT("SetVoxel:standin-painter:3,4,0:1") },
		{ TEXT("overlay"), true, 201, TEXT("DrawOverlay:standin-overlay:box Box 48,72,4:sphere Sphere 56,72,6") },
		{ TEXT("consent"), true, 32, TEXT("Started:standin-greeter#1") },
		{ TEXT("consent"), false, 1, TEXT("Prompt:1") },
		{ TEXT("revoke"), true, 12, TEXT("Error:standin-greeter-two:RATE_LIMITED") },
		{ TEXT("misbehaver"), true, 106, TEXT("Stopped:standin-misbehaver:CircuitOpen") },
	};
	for (const FExpected& Case : Cases)
	{
		FFixture F;
		if (!TestTrue(FString::Printf(TEXT("%s loads"), Case.Name), F.StandIn->SelectTimelineForStandIn(Case.Name)))
		{
			continue;
		}
		F.StandIn->EnterGrid(3, 4, 0);
		for (int32 Tick = 0; Tick < 1200; ++Tick)
		{
			F.StandIn->Tick(0.1f);
			F.StandIn->AnswerPrompt(1, Case.bRun);
			F.StandIn->Revoke(TEXT("standin-greeter-one"));
			F.StandIn->ForgetAuthor(1001);
		}
		TestEqual(FString::Printf(TEXT("%s (run %d) replays every step"), Case.Name, Case.bRun), F.Host->Lines.Num(), Case.Lines);
		TestTrue(FString::Printf(TEXT("%s (run %d) has %s"), Case.Name, Case.bRun, Case.Contains), F.Host->Lines.Contains(Case.Contains));
		if (FCString::Strcmp(Case.Name, TEXT("revoke")) == 0)
		{
			TestTrue(TEXT("revoke stops greeter one by its own branch"), F.Host->Lines.Contains(TEXT("Stopped:standin-greeter-one:Revoked")));
		}
	}
	return true;
}

#endif
