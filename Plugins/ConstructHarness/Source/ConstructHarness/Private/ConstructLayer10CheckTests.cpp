#include "ConstructLayer10Check.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using FLayer10Check = UConstructLayer10CheckSubsystem;

	FInt64Vector Layer10ChunkOf(const FVector& Spot, const double ChunkSize)
	{
		return FInt64Vector(FMath::FloorToInt64(Spot.X / ChunkSize), FMath::FloorToInt64(Spot.Y / ChunkSize),
		                    FMath::FloorToInt64(Spot.Z / ChunkSize));
	}

	FCrowdyModScriptGrid Layer10Box(const int64 GridId, const FInt64Vector& Low, const FInt64Vector& High)
	{
		FCrowdyModScriptGrid Grid;
		Grid.GridId = GridId;
		Grid.LowChunkX = Low.X;
		Grid.LowChunkY = Low.Y;
		Grid.LowChunkZ = Low.Z;
		Grid.HighChunkX = High.X;
		Grid.HighChunkY = High.Y;
		Grid.HighChunkZ = High.Z;
		return Grid;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer10MarkerTest, "Construct.Harness.Layer10.HudTextMatchesTheMarkerExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer10MarkerTest::RunTest(const FString& Parameters)
{
	const FString Marker = FLayer10Check::MarkerFor(TEXT("0123456789ab"));
	TestEqualSensitive(TEXT("The marker is the script's HUD text"), Marker, FString(TEXT("layer10 marker 0123456789ab")));
	TestTrue(TEXT("The marker matches itself"), FLayer10Check::IsMarkerText(TEXT("layer10 marker 0123456789ab"), Marker));
	TestFalse(TEXT("Case counts"), FLayer10Check::IsMarkerText(TEXT("layer10 marker 0123456789AB"), Marker));
	TestFalse(TEXT("A trailing space is other text"), FLayer10Check::IsMarkerText(TEXT("layer10 marker 0123456789ab "), Marker));
	TestFalse(TEXT("Another run's marker is other text"), FLayer10Check::IsMarkerText(TEXT("layer10 marker 0123456789ac"), Marker));
	TestFalse(TEXT("Empty text is other text"), FLayer10Check::IsMarkerText(FString(), Marker));
	TestFalse(TEXT("No marker matches nothing"), FLayer10Check::IsMarkerText(FString(), FString()));

	TestTrue(TEXT("Twelve lowercase hex digits are a run id"), FLayer10Check::IsValidRunId(TEXT("0123456789ab")));
	TestFalse(TEXT("Uppercase hex is not"), FLayer10Check::IsValidRunId(TEXT("0123456789AB")));
	TestFalse(TEXT("Eleven digits are not"), FLayer10Check::IsValidRunId(TEXT("0123456789a")));
	TestFalse(TEXT("Thirteen digits are not"), FLayer10Check::IsValidRunId(TEXT("0123456789abc")));
	TestFalse(TEXT("A letter past f is not"), FLayer10Check::IsValidRunId(TEXT("0123456789ag")));
	TestFalse(TEXT("Empty is not"), FLayer10Check::IsValidRunId(FString()));

	UConstructLayer10CheckHost* Host = NewObject<UConstructLayer10CheckHost>(GetTransientPackage());
	Host->Marker = Marker;
	FCrowdyModScript Script;
	Script.Name = TEXT("layer10-check");
	ICrowdyModScriptHost::Execute_ShowHud(Host, Script, TEXT("layer10 marker 0123456789ab"));
	ICrowdyModScriptHost::Execute_ShowHud(Host, Script, TEXT("hello"));
	ICrowdyModScriptHost::Execute_ShowHud(Host, Script, FString());
	TestEqual(TEXT("The host counts the marker once"), Host->HudMarker, 1);
	TestEqual(TEXT("The host counts the rest as other"), Host->HudOther, 2);

	ICrowdyModScriptHost::Execute_DrawOverlay(Host, Script, TArray<FCrowdyModScriptShape>());
	ICrowdyModScriptHost::Execute_ClearScript(Host, Script);
	TestEqual(TEXT("The host counts overlays"), Host->Overlays, 1);
	TestEqual(TEXT("The host counts clears"), Host->Clears, 1);

	FString Refusal;
	TestFalse(TEXT("The host refuses every write"), ICrowdyModScriptHost::Execute_SetVoxel(Host, Script, FCrowdyVoxelWrite(), Refusal));
	TestEqualSensitive(TEXT("The write's refusal names the check"), Refusal, FString(TEXT("not offered by the check")));
	FCrowdyModScriptChunk Chunk;
	TestFalse(TEXT("The host reads no chunk"), ICrowdyModScriptHost::Execute_ReadChunk(Host, 39, -64, 0, Chunk));
	TArray<FCrowdyModScriptPlayer> Players;
	Players.AddDefaulted();
	ICrowdyModScriptHost::Execute_ListPlayers(Host, FCrowdyModScriptGrid(), Players);
	TestEqual(TEXT("The host lists no players"), Players.Num(), 0);
	FCrowdyModScriptClicks Clicks;
	Clicks.Buttons = 1;
	Clicks.Clicks.AddDefaulted();
	ICrowdyModScriptHost::Execute_DrainClicks(Host, Clicks);
	TestEqual(TEXT("The host holds no buttons"), Clicks.Buttons, 0);
	TestEqual(TEXT("The host drains no clicks"), Clicks.Clicks.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer10SummaryTest, "Construct.Harness.Layer10.SummaryLineFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer10SummaryTest::RunTest(const FString& Parameters)
{
	FConstructLayer10Counts Counts;
	Counts.Started = 1;
	Counts.Stopped = 2;
	Counts.HudMarker = 3;
	Counts.HudOther = 4;
	Counts.Overlays = 5;
	Counts.Clears = 6;
	Counts.Errors = 7;
	Counts.Prompts = 8;
	TestEqualSensitive(TEXT("Every count in order"), FLayer10Check::FormatSummaryLine(16.66, Counts),
	          FString(TEXT("measured 20 s: longest frame 16.7 ms, started 1, stopped 2, hud marker 3, hud other 4, overlays 5, clears 6, ")
	                  TEXT("errors 7, prompts 8")));
	TestEqualSensitive(TEXT("Nothing counted"), FLayer10Check::FormatSummaryLine(0.0, FConstructLayer10Counts()),
	          FString(TEXT("measured 20 s: longest frame 0.0 ms, started 0, stopped 0, hud marker 0, hud other 0, overlays 0, clears 0, ")
	                  TEXT("errors 0, prompts 0")));
	TestEqualSensitive(TEXT("A long frame keeps one decimal"), FLayer10Check::FormatSummaryLine(1234.56, FConstructLayer10Counts()),
	          FString(TEXT("measured 20 s: longest frame 1234.6 ms, started 0, stopped 0, hud marker 0, hud other 0, overlays 0, clears 0, ")
	                  TEXT("errors 0, prompts 0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer10PromptTest, "Construct.Harness.Layer10.PromptLineFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer10PromptTest::RunTest(const FString& Parameters)
{
	FCrowdyModScriptPrompt Prompt;
	Prompt.PromptId = 4;
	Prompt.AuthorId = 99;
	Prompt.Scripts = {TEXT("layer10-check")};
	Prompt.Calls = {TEXT("hud_set")};
	Prompt.Groups = {ECrowdyModScriptGroup::Present};
	TestEqualSensitive(TEXT("The test script's prompt"), FLayer10Check::FormatPromptLine(Prompt),
	          FString(TEXT("prompt: scripts 1, calls hud_set, groups Present, not offered -")));

	Prompt.Scripts = {TEXT("one"), TEXT("two")};
	Prompt.Calls = {TEXT("hud_set"), TEXT("voxel_set"), TEXT("input_key")};
	Prompt.Groups = {ECrowdyModScriptGroup::WorldWrite, ECrowdyModScriptGroup::Present};
	Prompt.NotOffered = {TEXT("input_key")};
	TestEqualSensitive(TEXT("Lists join without spaces, groups by enum name"), FLayer10Check::FormatPromptLine(Prompt),
	          FString(TEXT("prompt: scripts 2, calls hud_set,voxel_set,input_key, groups WorldWrite,Present, not offered input_key")));

	TestEqualSensitive(TEXT("Empty lists read as a dash"), FLayer10Check::FormatPromptLine(FCrowdyModScriptPrompt()),
	          FString(TEXT("prompt: scripts 0, calls -, groups -, not offered -")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer10FramesTest, "Construct.Harness.Layer10.FrameTrackerKeepsTheLongestGap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer10FramesTest::RunTest(const FString& Parameters)
{
	FConstructLayer10FrameTracker Frames;
	TestEqual(TEXT("Nothing measured"), Frames.GetLongestMs(), 0.0);
	Frames.Add(100.0);
	TestEqual(TEXT("One sample is no gap"), Frames.GetLongestMs(), 0.0);
	Frames.Add(100.016);
	TestEqual(TEXT("The first gap"), Frames.GetLongestMs(), 16.0, 1.0e-6);
	Frames.Add(100.116);
	TestEqual(TEXT("A longer gap replaces it"), Frames.GetLongestMs(), 100.0, 1.0e-6);
	Frames.Add(100.120);
	TestEqual(TEXT("A shorter gap does not"), Frames.GetLongestMs(), 100.0, 1.0e-6);

	Frames.Reset();
	TestEqual(TEXT("Reset forgets the longest"), Frames.GetLongestMs(), 0.0);
	Frames.Add(500.0);
	TestEqual(TEXT("Reset forgets the last sample too"), Frames.GetLongestMs(), 0.0);
	Frames.Add(500.005);
	TestEqual(TEXT("Gaps count from the sample after the reset"), Frames.GetLongestMs(), 5.0, 1.0e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer10NamesTest, "Construct.Harness.Layer10.RunAndStopNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer10NamesTest::RunTest(const FString& Parameters)
{
	for (const EConstructLayer10Run Run : {EConstructLayer10Run::Control, EConstructLayer10Run::Declined, EConstructLayer10Run::Accepted})
	{
		EConstructLayer10Run Parsed = EConstructLayer10Run::Control;
		TestTrue(FString::Printf(TEXT("%s parses"), FLayer10Check::RunName(Run)), FLayer10Check::ParseRun(FLayer10Check::RunName(Run), Parsed));
		TestTrue(FString::Printf(TEXT("%s round-trips"), FLayer10Check::RunName(Run)), Parsed == Run);
	}
	EConstructLayer10Run Parsed = EConstructLayer10Run::Control;
	TestEqualSensitive(TEXT("The declined run's name"), FString(FLayer10Check::RunName(EConstructLayer10Run::Declined)), FString(TEXT("declined")));
	TestFalse(TEXT("Case counts"), FLayer10Check::ParseRun(TEXT("Accepted"), Parsed));
	TestFalse(TEXT("Empty is no run"), FLayer10Check::ParseRun(FString(), Parsed));
	TestFalse(TEXT("Another word is no run"), FLayer10Check::ParseRun(TEXT("both"), Parsed));

	TestEqualSensitive(TEXT("A circuit stop reads as its enum name"), FLayer10Check::StopReasonName(ECrowdyModScriptStop::CircuitOpen),
	          FString(TEXT("CircuitOpen")));
	TestEqualSensitive(TEXT("A left grid reads as its enum name"), FLayer10Check::StopReasonName(ECrowdyModScriptStop::LeftGrid),
	          FString(TEXT("LeftGrid")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer10SpotTest, "Construct.Harness.Layer10.SpotAndGridCoverChunkM",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer10SpotTest::RunTest(const FString& Parameters)
{
	const FInt64Vector M(FLayer10Check::CheckChunkX, FLayer10Check::CheckChunkY, FLayer10Check::CheckChunkZ);
	TestTrue(TEXT("M is chunk 39 -64 0"), M == FInt64Vector(39, -64, 0));
	for (const double ChunkSize : {1000.0, 1600.0, 3200.0})
	{
		const FVector Spot = FLayer10Check::SpotFor(ChunkSize);
		TestTrue(FString::Printf(TEXT("The spot is in M at chunk size %.0f"), ChunkSize), Layer10ChunkOf(Spot, ChunkSize) == M);
	}
	TestEqual(TEXT("At 1,600 units M's centre"), FLayer10Check::SpotFor(1600.0), FVector(63200.0, -101600.0, 800.0));

	TestTrue(TEXT("A one-chunk grid over M"), FLayer10Check::GridContainsCheckChunk(Layer10Box(7, M, M)));
	TestTrue(TEXT("A wider grid over M"), FLayer10Check::GridContainsCheckChunk(Layer10Box(7, FInt64Vector(38, -65, -1), FInt64Vector(40, -63, 1))));
	TestFalse(TEXT("No grid"), FLayer10Check::GridContainsCheckChunk(Layer10Box(0, M, M)));
	TestFalse(TEXT("A grid beside M"), FLayer10Check::GridContainsCheckChunk(Layer10Box(7, FInt64Vector(40, -64, 0), FInt64Vector(40, -64, 0))));
	TestFalse(TEXT("A grid above M"), FLayer10Check::GridContainsCheckChunk(Layer10Box(7, FInt64Vector(39, -64, 1), FInt64Vector(39, -64, 1))));
	return true;
}

#endif
