#include "ConstructLayer7Check.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer7ClaimPathTest, "Construct.Harness.Layer7.ClaimPathFollowsThePad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer7ClaimPathTest::RunTest(const FString& Parameters)
{
	using Path = EConstructLayer7ClaimPath;
	TestTrue(TEXT("A free chunk is claimed through the pad"), UConstructLayer7CheckSubsystem::ChooseClaimPath(true, 0, false) == Path::Pad);
	TestTrue(TEXT("Another player's claim takes the direct path"), UConstructLayer7CheckSubsystem::ChooseClaimPath(true, 41, false) == Path::Direct);
	TestTrue(TEXT("The player's own claim has nothing to claim"), UConstructLayer7CheckSubsystem::ChooseClaimPath(true, 41, true) == Path::None);
	TestTrue(TEXT("An unknown chunk has nothing to claim"), UConstructLayer7CheckSubsystem::ChooseClaimPath(false, 0, false) == Path::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer7ControlLinesTest, "Construct.Harness.Layer7.ControlLinesCountOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer7ControlLinesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("wait is no instruction"), UConstructLayer7CheckSubsystem::CompleteLines(TEXT("wait\n"), 0).Num(), 0);
	TestEqual(TEXT("A line with no newline waits for a later read"), UConstructLayer7CheckSubsystem::CompleteLines(TEXT("claim"), 0).Num(), 0);
	const TArray<FString> First = UConstructLayer7CheckSubsystem::CompleteLines(TEXT("claim\r\n"), 0);
	TestTrue(TEXT("The first instruction is index 0, trimmed"), First.Num() == 1 && First[0] == TEXT("claim"));
	const TArray<FString> Later = UConstructLayer7CheckSubsystem::CompleteLines(TEXT("claim\nrelease\nlo"), 1);
	TestTrue(TEXT("An instruction already acted on is not read again"), Later.Num() == 1 && Later[0] == TEXT("release"));
	TestEqual(TEXT("Nothing past the last acted on"), UConstructLayer7CheckSubsystem::CompleteLines(TEXT("claim\nrelease\n"), 2).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer7ResultsTest, "Construct.Harness.Layer7.ResultsKeepIdsExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer7ResultsTest::RunTest(const FString& Parameters)
{
	// Above 2^53, so a JSON number would round it.
	const int64 LargeId = 9007199254740993;

	const FString Shown = UConstructLayer7CheckSubsystem::ShownJson(LargeId, LargeId + 2, true);
	TestTrue(TEXT("The shown line names its event"), Shown.Contains(TEXT("\"event\":\"shown\"")));
	TestTrue(TEXT("The grid id stays exact, as a string"), Shown.Contains(TEXT("\"grid\":\"9007199254740993\"")));
	TestTrue(TEXT("The owner id stays exact, as a string"), Shown.Contains(TEXT("\"owner\":\"9007199254740995\"")));
	TestTrue(TEXT("Mine is a boolean"), Shown.Contains(TEXT("\"mine\":true")));
	TestFalse(TEXT("A results line is one line"), Shown.Contains(TEXT("\n")));

	const FString Named = UConstructLayer7CheckSubsystem::NamedJson(LargeId, TEXT("Walker \"A\""));
	TestTrue(TEXT("The named line keeps the owner exact"), Named.Contains(TEXT("\"owner\":\"9007199254740993\"")));
	TestTrue(TEXT("The name is escaped, not cut"), Named.Contains(TEXT("\"name\":\"Walker \\\"A\\\"\"")));

	const FString Record = UConstructLayer7CheckSubsystem::RecordJson(LargeId, ECrowdyServerCallOutcome::ServerError, TEXT("this grid is not your claim on that chunk"));
	TestTrue(TEXT("The outcome is named, not numbered"), Record.Contains(TEXT("\"outcome\":\"ServerError\"")));
	TestTrue(TEXT("The refusal text is carried whole"), Record.Contains(TEXT("\"reason\":\"this grid is not your claim on that chunk\"")));
	TestTrue(TEXT("The recorded grid stays exact"), Record.Contains(TEXT("\"grid\":\"9007199254740993\"")));
	return true;
}

#endif
