#include "ConstructLayer11Check.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using FLayer11Check = UConstructLayer11CheckSubsystem;
	using FLayer11Signal = FConstructLayer11Signal;

	FInt64Vector Layer11ChunkOf(const FVector& Spot, const double ChunkSize)
	{
		return FInt64Vector(FMath::FloorToInt64(Spot.X / ChunkSize), FMath::FloorToInt64(Spot.Y / ChunkSize),
		                    FMath::FloorToInt64(Spot.Z / ChunkSize));
	}

	TSharedPtr<FJsonObject> Layer11Parse(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Json), Object);
		return Object;
	}

	FString Layer11KeysOf(const FJsonObject& Object)
	{
		TArray<FString> Keys;
		for (const auto& Pair : Object.Values)
		{
			Keys.Add(FString(Pair.Key));
		}
		Keys.Sort();
		return FString::Join(Keys, TEXT(","));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer11IdsTest, "Construct.Harness.Layer11.GridAndProjectIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer11IdsTest::RunTest(const FString& Parameters)
{
	int64 GridId = 0;
	TestTrue(TEXT("Digits are a grid id"), FLayer11Check::IsValidGridId(TEXT("1234567890123"), GridId));
	TestEqual(TEXT("The grid id is read as int64"), GridId, int64(1234567890123));
	TestTrue(TEXT("The largest int64 is a grid id"), FLayer11Check::IsValidGridId(TEXT("9223372036854775807"), GridId));
	TestFalse(TEXT("Past int64 is not"), FLayer11Check::IsValidGridId(TEXT("9223372036854775808"), GridId));
	TestFalse(TEXT("Zero is not"), FLayer11Check::IsValidGridId(TEXT("0"), GridId));
	TestFalse(TEXT("A leading zero is not"), FLayer11Check::IsValidGridId(TEXT("042"), GridId));
	TestFalse(TEXT("A sign is not"), FLayer11Check::IsValidGridId(TEXT("-5"), GridId));
	TestFalse(TEXT("Trailing text is not"), FLayer11Check::IsValidGridId(TEXT("12ab"), GridId));
	TestFalse(TEXT("Empty is not"), FLayer11Check::IsValidGridId(FString(), GridId));

	TestTrue(TEXT("A UUID is a project id"), FLayer11Check::IsValidProjectId(TEXT("3f2b8c1e-9a4d-4e7f-b6a1-0c2d3e4f5a6b")));
	TestTrue(TEXT("Plain digits are a project id"), FLayer11Check::IsValidProjectId(TEXT("42")));
	TestFalse(TEXT("A space is not"), FLayer11Check::IsValidProjectId(TEXT("3f2b 8c1e")));
	TestFalse(TEXT("A quote is not"), FLayer11Check::IsValidProjectId(TEXT("3f2b\"")));
	TestFalse(TEXT("Sixty-five characters are not"), FLayer11Check::IsValidProjectId(FString::ChrN(65, TEXT('a'))));
	TestFalse(TEXT("Empty is not"), FLayer11Check::IsValidProjectId(FString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer11SignalTest, "Construct.Harness.Layer11.SignalParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer11SignalTest::RunTest(const FString& Parameters)
{
	FLayer11Signal Signal;
	TestTrue(TEXT("A turn"), FLayer11Check::ParseSignal(TEXT("{\"seq\":1,\"kind\":\"turn\",\"text\":\"hello\"}"), 0, Signal));
	TestEqual(TEXT("Its seq"), Signal.Seq, int64(1));
	TestTrue(TEXT("Its kind"), Signal.Kind == FLayer11Signal::EKind::Turn);
	TestEqualSensitive(TEXT("Its text"), Signal.Text, FString(TEXT("hello")));

	TestTrue(TEXT("A draft"), FLayer11Check::ParseSignal(TEXT("{\"seq\":3,\"kind\":\"draft\",\"text\":\"ask\"}"), 2, Signal));
	TestTrue(TEXT("Its kind"), Signal.Kind == FLayer11Signal::EKind::Draft);
	TestTrue(TEXT("A close needs no text"), FLayer11Check::ParseSignal(TEXT("{\"seq\":4,\"kind\":\"close\"}"), 3, Signal));
	TestTrue(TEXT("Its kind"), Signal.Kind == FLayer11Signal::EKind::Close);

	FLayer11Signal Untouched;
	Untouched.Seq = 99;
	TestFalse(TEXT("The seq already handled"), FLayer11Check::ParseSignal(TEXT("{\"seq\":2,\"kind\":\"turn\",\"text\":\"x\"}"), 2, Untouched));
	TestFalse(TEXT("An older seq"), FLayer11Check::ParseSignal(TEXT("{\"seq\":1,\"kind\":\"turn\",\"text\":\"x\"}"), 2, Untouched));
	TestFalse(TEXT("A fractional seq"), FLayer11Check::ParseSignal(TEXT("{\"seq\":2.5,\"kind\":\"turn\",\"text\":\"x\"}"), 0, Untouched));
	TestFalse(TEXT("A seq as text"), FLayer11Check::ParseSignal(TEXT("{\"seq\":\"2\",\"kind\":\"turn\",\"text\":\"x\"}"), 0, Untouched));
	TestFalse(TEXT("An unknown kind"), FLayer11Check::ParseSignal(TEXT("{\"seq\":2,\"kind\":\"deploy\",\"text\":\"x\"}"), 0, Untouched));
	TestFalse(TEXT("Kinds are case-sensitive"), FLayer11Check::ParseSignal(TEXT("{\"seq\":2,\"kind\":\"Turn\",\"text\":\"x\"}"), 0, Untouched));
	TestFalse(TEXT("A turn without text"), FLayer11Check::ParseSignal(TEXT("{\"seq\":2,\"kind\":\"turn\"}"), 0, Untouched));
	TestFalse(TEXT("A turn of spaces"), FLayer11Check::ParseSignal(TEXT("{\"seq\":2,\"kind\":\"turn\",\"text\":\"   \"}"), 0, Untouched));
	const FString TooLong = FString::Printf(TEXT("{\"seq\":2,\"kind\":\"turn\",\"text\":\"%s\"}"), *FString::ChrN(FLayer11Check::MaxPromptChars + 1, TEXT('a')));
	TestFalse(TEXT("A text past the limit"), FLayer11Check::ParseSignal(TooLong, 0, Untouched));
	TestFalse(TEXT("Half a file"), FLayer11Check::ParseSignal(TEXT("{\"seq\":2,\"kind\":\"tu"), 0, Untouched));
	TestFalse(TEXT("An empty file"), FLayer11Check::ParseSignal(FString(), 0, Untouched));
	TestEqual(TEXT("A refused signal leaves the output alone"), Untouched.Seq, int64(99));

	TestEqualSensitive(TEXT("Kind names"), FString(FLayer11Check::SignalKindName(FLayer11Signal::EKind::Draft)), FString(TEXT("draft")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer11StepTest, "Construct.Harness.Layer11.StepForEachDockState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer11StepTest::RunTest(const FString& Parameters)
{
	using EState = EConstructStudioDockState;
	using EReason = EConstructStudioDockReason;
	using EStep = EConstructLayer11Step;
	TestTrue(TEXT("Booting waits"), FLayer11Check::StepFor(EState::Booting, EReason::None, false, false) == EStep::Wait);
	TestTrue(TEXT("Ready is ready"), FLayer11Check::StepFor(EState::Ready, EReason::None, false, false) == EStep::Ready);
	TestTrue(TEXT("Consent when the check may"), FLayer11Check::StepFor(EState::NeedsConsent, EReason::NeedsConsent, true, false) == EStep::Consent);
	TestTrue(TEXT("Not run when it may not"), FLayer11Check::StepFor(EState::NeedsConsent, EReason::NeedsConsent, false, false) == EStep::NotRun);
	TestTrue(TEXT("Consent is given once, then the check waits"), FLayer11Check::StepFor(EState::NeedsConsent, EReason::NeedsConsent, true, true) == EStep::Wait);
	TestTrue(TEXT("No model is not run"), FLayer11Check::StepFor(EState::Failed, EReason::NoModel, true, false) == EStep::NotRun);
	TestTrue(TEXT("A page not built is not run"), FLayer11Check::StepFor(EState::Failed, EReason::NotBuiltYet, true, false) == EStep::NotRun);
	TestTrue(TEXT("A harness failure fails"), FLayer11Check::StepFor(EState::Failed, EReason::HarnessFailed, true, false) == EStep::Failed);
	TestTrue(TEXT("A boot timeout fails"), FLayer11Check::StepFor(EState::Failed, EReason::BootTimedOut, true, false) == EStep::Failed);
	TestTrue(TEXT("A failed read fails"), FLayer11Check::StepFor(EState::Failed, EReason::Unavailable, true, false) == EStep::Failed);
	TestTrue(TEXT("Closing itself fails"), FLayer11Check::StepFor(EState::Closed, EReason::SignedOut, true, false) == EStep::Failed);

	TestEqualSensitive(TEXT("The consent reason"), FLayer11Check::NotRunReason(EState::NeedsConsent, EReason::NeedsConsent),
	                   FString(TEXT("provider consent is not recorded and the check may not record it")));
	TestEqualSensitive(TEXT("The model reason"), FLayer11Check::NotRunReason(EState::Failed, EReason::NoModel), FString(TEXT("the dock found no model for the agent")));
	TestEqualSensitive(TEXT("States read as enum names"), FLayer11Check::StateName(EState::NeedsConsent), FString(TEXT("NeedsConsent")));
	TestEqualSensitive(TEXT("Reasons read as enum names"), FLayer11Check::ReasonName(EReason::BootTimedOut), FString(TEXT("BootTimedOut")));
	TestEqualSensitive(TEXT("Deploy kinds read as enum names"), FLayer11Check::DeployKindName(EConstructStudioDeployKind::DraftTest), FString(TEXT("DraftTest")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer11ResultTest, "Construct.Harness.Layer11.ResultFileHoldsOnlyNamesAndTimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer11ResultTest::RunTest(const FString& Parameters)
{
	FConstructLayer11Result Result;
	Result.Outcome = TEXT("done");
	Result.OpenedAt = 3.0;
	Result.ReadyAt = 9.5;
	Result.Captures = 2;
	Result.bConsentGiven = true;
	Result.States.Add({TEXT("Booting"), TEXT("None"), 3.0});
	Result.States.Add({TEXT("Ready"), TEXT("None"), 9.5});
	FConstructLayer11Result::FPrompt& Prompt = Result.Prompts.AddDefaulted_GetRef();
	Prompt.Seq = 1;
	Prompt.Kind = TEXT("turn");
	Prompt.bSent = true;
	Prompt.At = 12.0;
	FConstructLayer11Result::FDeploy& Deploy = Result.DeployPrompts.AddDefaulted_GetRef();
	Deploy.Kind = TEXT("DraftTest");
	Deploy.Answer = TEXT("NotNow");
	Deploy.bGridMatches = true;
	Deploy.bProjectMatches = true;
	Deploy.ChangedFiles = 1;

	const TSharedPtr<FJsonObject> Root = Layer11Parse(FLayer11Check::ResultToJson(Result));
	if (!TestTrue(TEXT("The result is one JSON object"), Root.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Exactly the result's keys"), Layer11KeysOf(*Root),
	          FString(TEXT("captures,consentGiven,deployPrompts,openedAt,outcome,problem,prompts,readyAt,states,withdrawn")));
	TestEqualSensitive(TEXT("The outcome"), Root->GetStringField(TEXT("outcome")), FString(TEXT("done")));
	TestEqual(TEXT("Ready's time"), Root->GetNumberField(TEXT("readyAt")), 9.5);
	TestEqual(TEXT("Two states"), Root->GetArrayField(TEXT("states")).Num(), 2);

	const TArray<TSharedPtr<FJsonValue>>& Prompts = Root->GetArrayField(TEXT("prompts"));
	if (TestEqual(TEXT("One prompt"), Prompts.Num(), 1))
	{
		TestEqual(TEXT("A prompt carries no text"), Layer11KeysOf(*Prompts[0]->AsObject()), FString(TEXT("at,kind,sent,seq")));
	}
	const TArray<TSharedPtr<FJsonValue>>& Deploys = Root->GetArrayField(TEXT("deployPrompts"));
	if (TestEqual(TEXT("One deploy prompt"), Deploys.Num(), 1))
	{
		const TSharedPtr<FJsonObject> Entry = Deploys[0]->AsObject();
		TestEqualSensitive(TEXT("Answered Not now"), Entry->GetStringField(TEXT("answer")), FString(TEXT("NotNow")));
		TestEqual(TEXT("A deploy prompt carries no project name, label or file path"), Layer11KeysOf(*Entry),
		          FString(TEXT("answer,at,changedFiles,gridMatches,kind,projectMatches,reachesVisitors,runInFlight,watched")));
		TestFalse(TEXT("No run was seen"), Entry->GetBoolField(TEXT("runInFlight")));
		TestFalse(TEXT("The watch has not ended"), Entry->GetBoolField(TEXT("watched")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer11WatchTest, "Construct.Harness.Layer11.NotNowIsWatchedForARun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer11WatchTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("The watch lasts 30 s"), FLayer11Check::RunWatchSeconds, 30.0);
	FConstructLayer11Result::FDeploy Quiet;
	Quiet.At = 100.0;
	TestFalse(TEXT("Nothing in flight early changes nothing"), FLayer11Check::WatchDeploy(Quiet, 110.0, false));
	TestFalse(TEXT("At 29.9 s the watch goes on"), FLayer11Check::WatchDeploy(Quiet, 129.9, false));
	TestFalse(TEXT("So it is not watched yet"), Quiet.bWatched);
	TestTrue(TEXT("At 30 s the watch ends"), FLayer11Check::WatchDeploy(Quiet, 130.0, false));
	TestTrue(TEXT("Watched"), Quiet.bWatched);
	TestFalse(TEXT("With no run seen"), Quiet.bRunInFlight);
	TestFalse(TEXT("An ended watch never changes"), FLayer11Check::WatchDeploy(Quiet, 131.0, true));
	TestFalse(TEXT("Not even for a later run"), Quiet.bRunInFlight);

	FConstructLayer11Result::FDeploy Ran;
	Ran.At = 100.0;
	TestTrue(TEXT("A run in flight during the watch is noted"), FLayer11Check::WatchDeploy(Ran, 105.0, true));
	TestTrue(TEXT("As a run seen"), Ran.bRunInFlight);
	TestFalse(TEXT("Seen again it changes nothing"), FLayer11Check::WatchDeploy(Ran, 106.0, true));
	TestTrue(TEXT("The watch still ends"), FLayer11Check::WatchDeploy(Ran, 131.0, false));
	TestTrue(TEXT("And keeps the run it saw"), Ran.bRunInFlight && Ran.bWatched);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer11CaptureTest, "Construct.Harness.Layer11.HostCaptureIsASmallSolidPng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer11CaptureTest::RunTest(const FString& Parameters)
{
	const FConstructStudioCapture Capture = UConstructLayer11CheckHost::SolidCapture(TEXT("view"));
	TestTrue(TEXT("Captured"), Capture.bCaptured);
	TestTrue(TEXT("No refusal"), Capture.Refusal.IsEmpty());
	TestEqual(TEXT("Width"), Capture.Width, UConstructLayer11CheckHost::ImageWidth);
	TestEqual(TEXT("Height"), Capture.Height, UConstructLayer11CheckHost::ImageHeight);
	TestEqualSensitive(TEXT("The label is the caption"), Capture.Caption, FString(TEXT("view")));
	TArray<uint8> Png;
	TestTrue(TEXT("The image is base64"), FBase64::Decode(Capture.PngBase64, Png));
	const uint8 Signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	TestTrue(TEXT("The bytes are a PNG"), Png.Num() > 8 && FMemory::Memcmp(Png.GetData(), Signature, sizeof(Signature)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer11SpotTest, "Construct.Harness.Layer11.SpotAndLabelNameChunkS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer11SpotTest::RunTest(const FString& Parameters)
{
	const FInt64Vector S(FLayer11Check::CheckChunkX, FLayer11Check::CheckChunkY, FLayer11Check::CheckChunkZ);
	TestTrue(TEXT("S is chunk 40 -64 0"), S == FInt64Vector(40, -64, 0));
	for (const double ChunkSize : {1000.0, 1600.0, 3200.0})
	{
		TestTrue(FString::Printf(TEXT("The spot is in S at chunk size %.0f"), ChunkSize), Layer11ChunkOf(FLayer11Check::SpotFor(ChunkSize), ChunkSize) == S);
	}
	TestEqual(TEXT("At 1,600 units S's centre"), FLayer11Check::SpotFor(1600.0), FVector(64800.0, -101600.0, 800.0));
	TestEqualSensitive(TEXT("The label names the chunk"), FLayer11Check::GridLabelFor(40, -64, 0).ToString(), FString(TEXT("Chunk 40, -64, 0")));
	return true;
}

#endif
