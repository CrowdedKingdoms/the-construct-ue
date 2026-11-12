#include "StandInC9.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "StandInC9TestTypes.h"

namespace
{
struct FStandInC9Fixture
{
	UGameInstance* Outer = nullptr;
	UStandInC9* StandIn = nullptr;
	UStandInC9TestListener* Listener = nullptr;

	FStandInC9Fixture()
	{
		Outer = NewObject<UGameInstance>(GetTransientPackage());
		StandIn = NewObject<UStandInC9>(Outer);
		StandIn->InitializeForTest();
		Listener = NewObject<UStandInC9TestListener>(GetTransientPackage());
		StandIn->OnCameraStateChanged.AddDynamic(Listener, &UStandInC9TestListener::OnCameraState);
		StandIn->OnFeedChanged.AddDynamic(Listener, &UStandInC9TestListener::OnFeed);
	}

	~FStandInC9Fixture()
	{
		StandIn->Deinitialize();
	}

	APawn* MakePawn() const { return NewObject<APawn>(GetTransientPackage()); }

	static void Pump() { FTSTicker::GetCoreTicker().Tick(0.f); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9CameraTest, "Construct.Webcam.StandIn.CameraStates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9CameraTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	TestEqual(TEXT("Starts Off"), F.StandIn->GetCameraState(), EConstructCameraState::Off);
	TestNull(TEXT("No preview while Off"), F.StandIn->GetLocalPreview());
	F.StandIn->SetCameraOn(true);
	TestEqual(TEXT("Starting at once"), F.StandIn->GetCameraState(), EConstructCameraState::Starting);
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("On after a tick"), F.StandIn->GetCameraState(), EConstructCameraState::On);
	TestNotNull(TEXT("Preview while On"), F.StandIn->GetLocalPreview());
	F.StandIn->SetCameraOn(false);
	TestEqual(TEXT("Off again"), F.StandIn->GetCameraState(), EConstructCameraState::Off);
	TestNull(TEXT("No preview after Off"), F.StandIn->GetLocalPreview());
	TestEqual(TEXT("Three events"), F.Listener->States.Num(), 3);
	if (F.Listener->States.Num() == 3)
	{
		TestEqual(TEXT("Event 1"), F.Listener->States[0], EConstructCameraState::Starting);
		TestEqual(TEXT("Event 2"), F.Listener->States[1], EConstructCameraState::On);
		TestEqual(TEXT("Event 3"), F.Listener->States[2], EConstructCameraState::Off);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9BlockTest, "Construct.Webcam.StandIn.BlockReason",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9BlockTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	F.StandIn->SetFixtureCameraBlock(EConstructCameraBlockedReason::SendingOff);
	F.StandIn->SetCameraOn(true);
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("A block keeps the camera Off"), F.StandIn->GetCameraState(), EConstructCameraState::Off);
	TestEqual(TEXT("With its reason"), F.StandIn->GetBlockedReason(), EConstructCameraBlockedReason::SendingOff);

	const int32 Events = F.Listener->States.Num();
	F.StandIn->SetFixtureCameraBlock(EConstructCameraBlockedReason::NoCamera);
	TestEqual(TEXT("No camera applies while Off"), F.StandIn->GetCameraState(), EConstructCameraState::Unavailable);
	TestEqual(TEXT("With its reason"), F.StandIn->GetBlockedReason(), EConstructCameraBlockedReason::NoCamera);
	TestEqual(TEXT("And fires the event"), F.Listener->States.Num(), Events + 1);
	F.StandIn->SetCameraOn(false);
	TestEqual(TEXT("Turning off keeps Unavailable"), F.StandIn->GetCameraState(), EConstructCameraState::Unavailable);
	F.StandIn->SetCameraOn(true);
	TestEqual(TEXT("Turning on stays Unavailable"), F.StandIn->GetCameraState(), EConstructCameraState::Unavailable);

	F.StandIn->SetFixtureCameraBlock(EConstructCameraBlockedReason::None);
	TestEqual(TEXT("None restores Off"), F.StandIn->GetCameraState(), EConstructCameraState::Off);
	F.StandIn->SetCameraOn(true);
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("Clearing the block restores default behaviour"), F.StandIn->GetCameraState(),
	          EConstructCameraState::On);

	const int32 Before = F.Listener->States.Num();
	F.StandIn->SetFixtureCameraBlock(EConstructCameraBlockedReason::AppDoesNotPermit);
	TestEqual(TEXT("A block while On stops the camera"), F.StandIn->GetCameraState(), EConstructCameraState::Off);
	TestEqual(TEXT("And fires the event"), F.Listener->States.Num(), Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9RecordingTest, "Construct.Webcam.StandIn.Recording",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9RecordingTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	TestFalse(TEXT("Not recorded by default"), F.StandIn->IsBeingRecorded());
	F.StandIn->SetFixtureRecording(true);
	TestTrue(TEXT("Recorded"), F.StandIn->IsBeingRecorded());
	TestEqual(TEXT("Event fired"), F.Listener->States.Num(), 1);
	F.StandIn->SetFixtureRecording(false);
	TestFalse(TEXT("Not recorded"), F.StandIn->IsBeingRecorded());
	TestEqual(TEXT("Second event fired"), F.Listener->States.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9FeedTest, "Construct.Webcam.StandIn.FeedInfo",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9FeedTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	APawn* A = F.MakePawn();
	APawn* B = F.MakePawn();
	F.StandIn->AddFixtureFeed(A);
	TestTrue(TEXT("Empty while Show Video is off"), F.StandIn->GetFeedInfo(A).SenderId.IsEmpty());
	F.StandIn->SetShowVideo(true);
	const FConstructWebcamFeed Info = F.StandIn->GetFeedInfo(A);
	TestEqual(TEXT("A 32 character sender id"), Info.SenderId.Len(), 32);
	TestNotNull(TEXT("A texture"), Info.Texture.Get());
	TestTrue(TEXT("Empty for another pawn"), F.StandIn->GetFeedInfo(B).SenderId.IsEmpty());
	TestNull(TEXT("No texture for another pawn"), F.StandIn->GetFeedInfo(B).Texture.Get());
	TestEqual(TEXT("Show Video on fires for the feed"), F.Listener->FeedPawns.Num(), 1);

	F.StandIn->AdvanceFixtureFrameForTest();
	TestEqual(TEXT("A new frame fires"), F.Listener->FeedPawns.Num(), 2);

	F.StandIn->AddFixtureFeed(B);
	F.Listener->FeedPawns.Reset();
	F.StandIn->SetShowVideo(false);
	TestEqual(TEXT("Show Video off fires for each feed"), F.Listener->FeedPawns.Num(), 2);
	TestTrue(TEXT("Cleared"), F.StandIn->GetFeedInfo(A).SenderId.IsEmpty());
	TestTrue(TEXT("Cleared too"), F.StandIn->GetFeedInfo(B).SenderId.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9ExpiryTest, "Construct.Webcam.StandIn.ExpiryAndGeneration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9ExpiryTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	APawn* A = F.MakePawn();
	F.StandIn->SetShowVideo(true);
	F.StandIn->AddFixtureFeed(A);
	const FString Id = F.StandIn->GetFeedInfo(A).SenderId;
	TestFalse(TEXT("Feed present"), Id.IsEmpty());

	F.Listener->FeedPawns.Reset();
	F.StandIn->ExpireFixtureFeed(A);
	TestTrue(TEXT("Expired feed is empty"), F.StandIn->GetFeedInfo(A).SenderId.IsEmpty());
	TestEqual(TEXT("Expiry fires"), F.Listener->FeedPawns.Num(), 1);
	F.StandIn->AdvanceFixtureFrameForTest();
	TestTrue(TEXT("A new frame does not bring an expired feed back"), F.StandIn->GetFeedInfo(A).SenderId.IsEmpty());
	TestEqual(TEXT("And fires nothing"), F.Listener->FeedPawns.Num(), 1);
	F.StandIn->RestoreFixtureFeed(A);
	TestEqual(TEXT("Restore brings the feed back"), F.StandIn->GetFeedInfo(A).SenderId, Id);
	TestEqual(TEXT("Restoring fires"), F.Listener->FeedPawns.Num(), 2);

	F.StandIn->ExpireFixtureFeed(A);
	F.StandIn->AddFixtureFeed(A);
	TestEqual(TEXT("Adding the feed again restores it"), F.StandIn->GetFeedInfo(A).SenderId, Id);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9GenerationTest, "Construct.Webcam.StandIn.Generation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9GenerationTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	APawn* A = F.MakePawn();
	F.StandIn->SetShowVideo(true);
	F.StandIn->AddFixtureFeed(A);
	const FConstructWebcamFeed First = F.StandIn->GetFeedInfo(A);
	F.Listener->FeedPawns.Reset();
	F.StandIn->ChangeFixtureFeedGeneration(A);
	const FConstructWebcamFeed Bumped = F.StandIn->GetFeedInfo(A);
	TestNotEqual(TEXT("A bump changes the generation"), Bumped.Generation, First.Generation);
	TestEqual(TEXT("The sender id is unchanged"), Bumped.SenderId, First.SenderId);
	TestEqual(TEXT("A bump fires"), F.Listener->FeedPawns.Num(), 1);

	F.StandIn->RemoveFixtureFeed(A);
	F.StandIn->AddFixtureFeed(A);
	const FConstructWebcamFeed Readded = F.StandIn->GetFeedInfo(A);
	TestNotEqual(TEXT("Re-adding differs from the first"), Readded.Generation, First.Generation);
	TestNotEqual(TEXT("Re-adding differs from the bump"), Readded.Generation, Bumped.Generation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9DeinitializeTest, "Construct.Webcam.StandIn.DeinitializeStopsTickers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9DeinitializeTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	FStandInC9OwnerResolved Resolved;
	Resolved.BindDynamic(F.Listener, &UStandInC9TestListener::OnOwner);
	F.StandIn->SetCameraOn(true);
	F.StandIn->ResolveActorOwner(FGuid::NewGuid(), Resolved);
	const int32 Events = F.Listener->States.Num();
	F.StandIn->Deinitialize();
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("The state stays as it was"), F.StandIn->GetCameraState(), EConstructCameraState::Starting);
	TestEqual(TEXT("No event fires"), F.Listener->States.Num(), Events);
	TestEqual(TEXT("No owner answer arrives"), F.Listener->OwnerAnswers, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9OwnerTest, "Construct.Webcam.StandIn.OwnerLookup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9OwnerTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	FStandInC9OwnerResolved Resolved;
	Resolved.BindDynamic(F.Listener, &UStandInC9TestListener::OnOwner);
	const FGuid Id = FGuid::NewGuid();
	F.StandIn->ResolveActorOwner(Id, Resolved);
	TestEqual(TEXT("Never answers inside the call"), F.Listener->OwnerAnswers, 0);
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("Answers on a later tick"), F.Listener->OwnerAnswers, 1);
	TestEqual(TEXT("The first ask is Unknown"), F.Listener->Result, EStandInC9OwnerResult::Unknown);

	F.StandIn->ResolveActorOwner(Id, Resolved);
	TestEqual(TEXT("Not inside the call"), F.Listener->OwnerAnswers, 1);
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("Answers again"), F.Listener->OwnerAnswers, 2);
	TestEqual(TEXT("A later ask is Confirmed"), F.Listener->Result, EStandInC9OwnerResult::Confirmed);
	TestTrue(TEXT("With a fixture user id"), F.Listener->UserId > 0);

	F.StandIn->SetFixtureOwner(EStandInC9OwnerResult::Failed, 0);
	F.StandIn->ResolveActorOwner(FGuid::NewGuid(), Resolved);
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("A forced answer applies to every ask"), F.Listener->Result, EStandInC9OwnerResult::Failed);

	F.StandIn->SetFixtureOwner(EStandInC9OwnerResult::Confirmed, 42);
	F.StandIn->ResolveActorOwner(FGuid::NewGuid(), Resolved);
	FStandInC9Fixture::Pump();
	TestEqual(TEXT("Forced Confirmed"), F.Listener->Result, EStandInC9OwnerResult::Confirmed);
	TestEqual(TEXT("The selected user id"), F.Listener->UserId, static_cast<int64>(42));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStandInC9FixturesTest, "Construct.Webcam.StandIn.FixturesDecode",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStandInC9FixturesTest::RunTest(const FString& Parameters)
{
	FStandInC9Fixture F;
	F.StandIn->SetCameraOn(true);
	FStandInC9Fixture::Pump();
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const UTexture2D* Texture = F.StandIn->GetLocalPreview();
		if (!TestNotNull(*FString::Printf(TEXT("Frame %d decoded"), Index), Texture))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Frame %d width"), Index), Texture->GetSizeX(), 128);
		TestEqual(*FString::Printf(TEXT("Frame %d height"), Index), Texture->GetSizeY(), 96);
		F.StandIn->AdvanceFixtureFrameForTest();
	}
	return true;
}

#endif
