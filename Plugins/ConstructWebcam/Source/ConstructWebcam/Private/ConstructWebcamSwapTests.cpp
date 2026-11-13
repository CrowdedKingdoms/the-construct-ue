#include "ConstructWebcamFeeds.h"
#include "ConstructWebcamSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructWebcamTestTypes.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamSwapHandlersTest, "Construct.Webcam.Swap.RealHandlersBind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FConstructWebcamSwapHandlersTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<UConstructWebcamSubsystem> Camera(NewObject<UConstructWebcamSubsystem>(Instance.Get()));
	TStrongObjectPtr<UConstructWebcamFeeds> Feeds(NewObject<UConstructWebcamFeeds>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<UConstructWebcamTestListener> Listener(NewObject<UConstructWebcamTestListener>());

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
