#include "ConstructWebcamFeeds.h"
#include "ConstructWebcamSubsystem.h"
#include "StandInC9.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructWebcamTestTypes.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <type_traits>

// Every call the UI makes on the stand-in answers the same type on the real subsystems, so the swap is a rename.
static_assert(std::is_same_v<decltype(std::declval<UStandInC9&>().GetCameraState()),
	decltype(std::declval<UConstructWebcamSubsystem&>().GetCameraState())>);
static_assert(std::is_same_v<decltype(std::declval<UStandInC9&>().GetBlockedReason()),
	decltype(std::declval<UConstructWebcamSubsystem&>().GetBlockedReason())>);
static_assert(std::is_same_v<decltype(std::declval<UStandInC9&>().GetLocalPreview()),
	decltype(std::declval<UConstructWebcamSubsystem&>().GetLocalPreview())>);
static_assert(std::is_same_v<decltype(std::declval<UStandInC9&>().IsBeingRecorded()),
	decltype(std::declval<UConstructWebcamSubsystem&>().IsBeingRecorded())>);
static_assert(std::is_same_v<decltype(std::declval<UStandInC9&>().SetCameraOn(true)),
	decltype(std::declval<UConstructWebcamSubsystem&>().SetCameraOn(true))>);
static_assert(std::is_same_v<decltype(std::declval<UStandInC9&>().SetShowVideo(true)),
	decltype(std::declval<UConstructWebcamFeeds&>().SetShowVideo(true))>);
static_assert(std::is_same_v<decltype(std::declval<UStandInC9&>().GetFeedInfo(nullptr)),
	decltype(std::declval<UConstructWebcamFeeds&>().GetFeedInfo(nullptr))>);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamSwapHandlersTest, "Construct.Webcam.Swap.HandlersBindToBoth",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FConstructWebcamSwapHandlersTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<UStandInC9> StandIn(NewObject<UStandInC9>(Instance.Get()));
	TStrongObjectPtr<UConstructWebcamSubsystem> Camera(NewObject<UConstructWebcamSubsystem>(Instance.Get()));
	TStrongObjectPtr<UConstructWebcamFeeds> Feeds(NewObject<UConstructWebcamFeeds>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<UConstructWebcamTestListener> Listener(NewObject<UConstructWebcamTestListener>());

	StandIn->OnCameraStateChanged.AddUniqueDynamic(Listener.Get(), &UConstructWebcamTestListener::OnCameraState);
	StandIn->OnFeedChanged.AddUniqueDynamic(Listener.Get(), &UConstructWebcamTestListener::OnFeed);
	Camera->OnCameraStateChanged.AddUniqueDynamic(Listener.Get(), &UConstructWebcamTestListener::OnCameraState);
	Feeds->OnFeedChanged.AddUniqueDynamic(Listener.Get(), &UConstructWebcamTestListener::OnFeed);

	TestTrue(TEXT("The UI's camera handler binds to the real camera event"),
		Camera->OnCameraStateChanged.IsAlreadyBound(Listener.Get(), &UConstructWebcamTestListener::OnCameraState));
	TestTrue(TEXT("The UI's feed handler binds to the real feed event"),
		Feeds->OnFeedChanged.IsAlreadyBound(Listener.Get(), &UConstructWebcamTestListener::OnFeed));

	Camera->OnCameraStateChanged.Broadcast(EConstructCameraState::On, EConstructCameraBlockedReason::None);
	Feeds->OnFeedChanged.Broadcast(nullptr);
	TestEqual(TEXT("The real camera event reaches the same handler"), Listener->States.Num(), 1);
	TestEqual(TEXT("The real feed event reaches the same handler"), Listener->FeedPawns.Num(), 1);
	return true;
}

#endif
