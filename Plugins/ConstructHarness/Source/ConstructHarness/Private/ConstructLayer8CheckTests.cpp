#include "ConstructLayer8Check.h"

#include "ConstructModPanel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer8ControlLinesTest, "Construct.Harness.Layer8.ControlLinesCountOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer8ControlLinesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("wait is no instruction"), UConstructLayer8CheckSubsystem::CompleteLines(TEXT("wait\n"), 0).Num(), 0);
	TestEqual(TEXT("A line with no newline waits for a later read"), UConstructLayer8CheckSubsystem::CompleteLines(TEXT("claim"), 0).Num(), 0);
	const TArray<FString> First = UConstructLayer8CheckSubsystem::CompleteLines(TEXT("claim\r\n"), 0);
	TestTrue(TEXT("The first instruction is index 0, trimmed"), First.Num() == 1 && First[0] == TEXT("claim"));
	const TArray<FString> Later = UConstructLayer8CheckSubsystem::CompleteLines(TEXT("claim\nbuild\ndep"), 1);
	TestTrue(TEXT("An instruction already acted on is not read again"), Later.Num() == 1 && Later[0] == TEXT("build"));
	TestEqual(TEXT("Nothing past the last acted on"), UConstructLayer8CheckSubsystem::CompleteLines(TEXT("claim\nbuild\n"), 2).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer8ReadyTest, "Construct.Harness.Layer8.InstructionsWaitForTheirState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer8ReadyTest::RunTest(const FString& Parameters)
{
	using EState = EConstructModPanelState;
	TestTrue(TEXT("Build waits for No mod"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("build"), EState::NoMod));
	TestFalse(TEXT("Build never rebuilds over a failed build"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("build"), EState::BuildFailed));
	TestFalse(TEXT("Build waits out Loading"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("build"), EState::Loading));
	TestTrue(TEXT("Deploy needs a build"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("deploy"), EState::Built));
	TestFalse(TEXT("Deploy waits out an error"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("deploy"), EState::Error));
	TestTrue(TEXT("On needs the mod off"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("on"), EState::Off));
	TestFalse(TEXT("On is not offered while on"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("on"), EState::On));
	TestTrue(TEXT("Off switches an enabled mod off"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("off"), EState::On));
	TestTrue(TEXT("Off switches a blocked mod off"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("off"), EState::Blocked));
	TestTrue(TEXT("The log reads with the mod off"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("log"), EState::Off));
	TestFalse(TEXT("The log is not offered with no mod"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("log"), EState::NoMod));
	TestTrue(TEXT("Delete takes an off mod"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("delete"), EState::Off));
	TestFalse(TEXT("Delete never takes a mod still on"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("delete"), EState::On));
	TestTrue(TEXT("Claim needs no panel"), UConstructLayer8CheckSubsystem::IsReadyFor(TEXT("claim"), EState::Loading));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer8SpinnerCellsTest, "Construct.Harness.Layer8.SpinnerCellsAreTheStarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer8SpinnerCellsTest::RunTest(const FString& Parameters)
{
	const TArray<FIntVector> Cells = UConstructLayer8CheckSubsystem::SpinnerCells();
	TestEqual(TEXT("The post, the hub and four arm cells"), Cells.Num(), 6);
	TestTrue(TEXT("The post comes first"), Cells.Num() == 6 && Cells[0] == FIntVector(8, 0, 8));
	TestTrue(TEXT("Then the hub"), Cells.Num() == 6 && Cells[1] == FIntVector(8, 1, 8));
	for (int32 Index = 2; Index < Cells.Num(); ++Index)
	{
		const FIntVector Offset = Cells[Index] - FIntVector(8, 1, 8);
		TestEqual(TEXT("Each arm cell is one step from the hub"), FMath::Abs(Offset.X) + FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z), 1);
		TestEqual(TEXT("The arm turns level with the hub"), Offset.Y, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer8LogLinesTest, "Construct.Harness.Layer8.LogLinesReadAsTheJudgeParses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer8LogLinesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A switch on"), UConstructLayer8CheckSubsystem::EnabledLine(ECrowdyModResult::Success, true, false), FString(TEXT("enabled Success 1 running 0")));
	TestEqual(TEXT("A refused switch"), UConstructLayer8CheckSubsystem::EnabledLine(ECrowdyModResult::Denied, false, false), FString(TEXT("enabled Denied 0 running 0")));
	TestEqual(TEXT("A deploy"), UConstructLayer8CheckSubsystem::DeployLine(ECrowdyModResult::Success, 3, false), FString(TEXT("deploy Success version 3 enabled 0")));
	TestEqual(TEXT("An erased cell"), UConstructLayer8CheckSubsystem::CubeLine(FIntVector(9, 1, 8), false, 0, FVector::ZeroVector), FString(TEXT("cube 9 1 8 missing")));
	TestEqual(TEXT("A drawn cell, in world units"), UConstructLayer8CheckSubsystem::CubeLine(FIntVector(8, 0, 8), true, 2, FVector(61650.0, -102350.0, 850.0)),
		FString(TEXT("cube 8 0 8 type 2 at 61650.00 -102350.00 850.00")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer8ResultsTest, "Construct.Harness.Layer8.ResultsKeepIdsExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer8ResultsTest::RunTest(const FString& Parameters)
{
	// Above 2^53, so a JSON number would round it.
	const int64 LargeId = 9007199254740993;

	const FString Claim = UConstructLayer8CheckSubsystem::ClaimJson(ECrowdyClaimResult::Success, LargeId);
	TestTrue(TEXT("The claim line names its event and result"), Claim.Contains(TEXT("\"event\":\"claim\"")) && Claim.Contains(TEXT("\"result\":\"Success\"")));
	TestTrue(TEXT("The grid id stays exact, as a string"), Claim.Contains(TEXT("\"grid\":\"9007199254740993\"")));
	TestFalse(TEXT("A results line is one line"), Claim.Contains(TEXT("\n")));

	TArray<FConstructModFileHash> Files;
	FConstructModFileHash& Cargo = Files.AddDefaulted_GetRef();
	Cargo.Path = TEXT("Cargo.toml");
	Cargo.Sha1 = TEXT("0123456789abcdef0123456789abcdef01234567");
	FConstructModFileHash& Lib = Files.AddDefaulted_GetRef();
	Lib.Path = TEXT("src/lib.rs");
	Lib.Sha1 = TEXT("fedcba9876543210fedcba9876543210fedcba98");
	const FString Build = UConstructLayer8CheckSubsystem::BuildJson(ECrowdyModResult::Success, ECrowdyModBuildStatus::Succeeded, TEXT("b-1"), TEXT("b-2"), Files);
	TestTrue(TEXT("The status is named"), Build.Contains(TEXT("\"status\":\"Succeeded\"")));
	TestTrue(TEXT("Both build ids are carried apart"), Build.Contains(TEXT("\"doneBuildId\":\"b-1\"")) && Build.Contains(TEXT("\"buildId\":\"b-2\"")));
	TestTrue(TEXT("Each file keeps its path and hash"), Build.Contains(TEXT("\"path\":\"src/lib.rs\"")) && Build.Contains(TEXT("\"sha1\":\"fedcba9876543210fedcba9876543210fedcba98\"")));
	TestTrue(TEXT("Files keep their order"), Build.Find(TEXT("Cargo.toml")) < Build.Find(TEXT("src/lib.rs")));

	const FString Reload = UConstructLayer8CheckSubsystem::ReloadJson(ECrowdyVoxelResult::Ok, TEXT("old"), TEXT("abc"), { FIntVector(8, 0, 8), FIntVector(9, 1, 8) }, { FIntVector(8, 0, 8) });
	TestTrue(TEXT("The reload result is named"), Reload.Contains(TEXT("\"result\":\"Ok\"")));
	TestTrue(TEXT("The digest before the reload is carried"), Reload.Contains(TEXT("\"before\":\"old\"")));
	TestTrue(TEXT("The digest is carried"), Reload.Contains(TEXT("\"sha1\":\"abc\"")));
	TestTrue(TEXT("Changed cells read as x y z"), Reload.Contains(TEXT("\"changed\":[\"8 0 8\",\"9 1 8\"]")));
	TestTrue(TEXT("Redrawn cells read as x y z"), Reload.Contains(TEXT("\"redrawn\":[\"8 0 8\"]")));
	return true;
}

#endif
