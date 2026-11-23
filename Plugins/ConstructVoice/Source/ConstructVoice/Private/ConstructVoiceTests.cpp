#include "ConstructVoiceComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceCaptureGateTest,
                                 "Construct.Voice.CapturePolicy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructVoiceCaptureGateTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A ready opted-in player may request capture"),
	         UConstructVoiceComponent::CanRequestCapture(true, true, true, true, true));
	TestFalse(TEXT("Microphone opt-in is required"),
	          UConstructVoiceComponent::CanRequestCapture(false, true, true, true, true));
	TestFalse(TEXT("Remote controller cannot capture"),
	          UConstructVoiceComponent::CanRequestCapture(true, false, true, true, true));
	TestFalse(TEXT("Signed-out player cannot capture"),
	          UConstructVoiceComponent::CanRequestCapture(true, true, false, true, true));
	TestFalse(TEXT("A modal UI blocks capture"),
	          UConstructVoiceComponent::CanRequestCapture(true, true, true, false, true));
	TestFalse(TEXT("Application focus is required"),
	          UConstructVoiceComponent::CanRequestCapture(true, true, true, true, false));
	UConstructVoiceComponent* Component = NewObject<UConstructVoiceComponent>();
	TestFalse(TEXT("Microphone starts off"), Component->IsPushToTalkEnabled());
	TestFalse(TEXT("Incoming playback starts off"), Component->IsListeningEnabled());
	Component->SetPushToTalkPressed(true);
	TestFalse(TEXT("A component without a world cannot report real capture"), Component->IsCaptureActive());
	Component->SetPushToTalkPressed(false);
	Component->StopTalking();
	TestFalse(TEXT("Stop is safe with no service"), Component->IsCaptureActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoicePhysicalReleaseTest,
                                 "Construct.Voice.PhysicalReleaseLatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructVoicePhysicalReleaseTest::RunTest(const FString& Parameters)
{
	FConstructVoiceTalkKeyState Intent;
	Intent.Press(false);
	TestTrue(TEXT("A fresh physical press may request capture"), Intent.WantsCapture());
	Intent.BlockIfHeld();
	TestTrue(TEXT("Entering a modal while held requires physical release"), Intent.IsReleaseRequired());
	// A player-controller flush is deliberately absent from this physical-edge state machine.
	// Reopening and Windows auto-repeat cannot manufacture the missing physical key-up.
	Intent.Press(true);
	TestFalse(TEXT("Repeat after modal/controller flush cannot restart capture"), Intent.WantsCapture());
	Intent.BlockIfHeld();
	TestFalse(TEXT("Blocked ticks preserve the held latch"), Intent.WantsCapture());
	Intent.Release();
	Intent.BlockIfHeld();
	TestFalse(TEXT("Key-up while the modal is open is not re-armed by another blocked tick"),
	          Intent.IsReleaseRequired());
	Intent.Press(false);
	TestTrue(TEXT("A fresh press after actual key-up can request capture"), Intent.WantsCapture());
	Intent.BlockIfHeld();
	TestFalse(TEXT("Focus loss while held cannot resume without release"), Intent.WantsCapture());
	Intent.Release();
	Intent.Press(false);
	TestTrue(TEXT("A release and fresh press restores intent after focus returns"), Intent.WantsCapture());
	FConstructVoiceTalkKeyState UnknownHeldKey;
	UnknownHeldKey.Press(true);
	TestFalse(TEXT("First observed event being a repeat cannot start the microphone"), UnknownHeldKey.WantsCapture());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceAccessLossTest,
                                 "Construct.Voice.AccessLossCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructVoiceAccessLossTest::RunTest(const FString& Parameters)
{
	UConstructVoiceComponent* Component = NewObject<UConstructVoiceComponent>();
	Component->bListeningEnabled = true;
	Component->bCaptureRequested = true;
	Component->TalkKeyState.Press(false);
	Component->AccessEnded(TEXT("Access revoked"), TEXT(""));
	TestFalse(TEXT("Access-ended handler clears the playback opt-in"), Component->IsListeningEnabled());
	TestFalse(TEXT("Access-ended handler stops the outstanding capture request"), Component->bCaptureRequested);
	TestFalse(TEXT("A held talk key cannot resume after access ends"), Component->TalkKeyState.WantsCapture());
	Component->SetListeningEnabled(false);
	TestFalse(TEXT("Explicit mute remains safe without a world or auth service"), Component->IsListeningEnabled());
	Component->SetListeningEnabled(true);
	TestFalse(TEXT("Missing active app access cannot enable playback"), Component->IsListeningEnabled());
	return true;
}
#endif
