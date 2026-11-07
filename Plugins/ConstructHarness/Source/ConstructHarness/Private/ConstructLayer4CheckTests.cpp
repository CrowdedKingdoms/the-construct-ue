#include "ConstructLayer4Check.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer4ProbeAnswerTest, "Construct.Harness.Layer4Probe.AnswerKeepsIdsExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer4ProbeAnswerTest::RunTest(const FString& Parameters)
{
	// Above 2^53, so a JSON number would round it.
	const int64 LargeId = 9007199254740993;

	FConstructHubClaim Claim;
	Claim.GridId = LargeId;
	Claim.Chunk.X = -7;
	Claim.OwnerUserId = LargeId + 2;
	Claim.ClaimedAt = 5;

	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetObjectField(TEXT("Claim"), AConstructLayer4CheckProbe::ClaimToJson(Claim));
	const FString Line = AConstructLayer4CheckProbe::AnswerToJson(ECrowdyServerCallOutcome::Success, FString(), false, Reply);

	TestTrue(TEXT("The grid id stays exact, as a string"), Line.Contains(TEXT("\"GridId\":\"9007199254740993\"")));
	TestTrue(TEXT("The owner id stays exact, as a string"), Line.Contains(TEXT("\"OwnerUserId\":\"9007199254740995\"")));
	TestTrue(TEXT("A chunk coordinate keeps its sign"), Line.Contains(TEXT("\"X\":\"-7\"")));
	TestTrue(TEXT("The outcome is named, not numbered"), Line.Contains(TEXT("\"outcome\":\"Success\"")));
	TestFalse(TEXT("The answer is one line"), Line.Contains(TEXT("\n")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer4ProbeRefusalTest, "Construct.Harness.Layer4Probe.RefusalCarriesNoReply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer4ProbeRefusalTest::RunTest(const FString& Parameters)
{
	// What a Blueprint's Failed branch would pass if it wired the Call node's outputs: its last success.
	FConstructHubClaim Stale;
	Stale.GridId = 41;
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetBoolField(TEXT("bRecorded"), true);
	Reply->SetObjectField(TEXT("Claim"), AConstructLayer4CheckProbe::ClaimToJson(Stale));

	const FString Line = AConstructLayer4CheckProbe::AnswerToJson(ECrowdyServerCallOutcome::Denied, TEXT("only the owner"), true, Reply);

	TestTrue(TEXT("The outcome is carried"), Line.Contains(TEXT("\"outcome\":\"Denied\"")));
	TestTrue(TEXT("The reason is carried"), Line.Contains(TEXT("\"reason\":\"only the owner\"")));
	TestTrue(TEXT("Retryable is carried"), Line.Contains(TEXT("\"retryable\":true")));
	TestTrue(TEXT("A refusal's reply is empty"), Line.Contains(TEXT("\"reply\":{}")));
	TestFalse(TEXT("A stale success never reaches the results"), Line.Contains(TEXT("bRecorded")));
	return true;
}

#endif
