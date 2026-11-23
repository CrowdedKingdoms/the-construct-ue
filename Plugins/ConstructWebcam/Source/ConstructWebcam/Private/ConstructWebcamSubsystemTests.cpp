#include "ConstructWebcamSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructWebcamCore.h"
#include "ConstructWebcamTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "IMediaCaptureSupport.h"
#include "IMediaEventSink.h"
#include "MediaPlayer.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

struct FConstructWebcamTestAccess
{
	static auto& Hooks(UConstructWebcamSubsystem& Camera)
	{
		return Camera.Hooks;
	}

	static void Tick(UConstructWebcamSubsystem& Camera)
	{
		Camera.TickCamera(ConstructWebcam::FrameInterval);
	}

	static void Answer(UConstructWebcamSubsystem& Camera,
	                   int32 Token,
	                   bool bAfterRefusal,
	                   const FCrowdyCommunicationSettings* Settings,
	                   const FCrowdySafetyError* Error)
	{
		Camera.SettingsAnswered(Token, bAfterRefusal, Settings, Error);
	}

	static void Refuse(UConstructWebcamSubsystem& Camera)
	{
		Camera.HandleSendRefused(FCrowdySendRefusal());
	}

	static void Logout(UConstructWebcamSubsystem& Camera)
	{
		Camera.HandleLogout(true);
	}

	static void AccessEnded(UConstructWebcamSubsystem& Camera)
	{
		Camera.HandleAccessEnded(FString(), FString());
	}

	static void TravelMap(UConstructWebcamSubsystem& Camera)
	{
		FWorldContext Context;
		Context.OwningGameInstance = Camera.GetGameInstance();
		Camera.HandlePreLoadMap(Context, FString());
	}

	static void RecordingChanged(UConstructWebcamSubsystem& Camera, bool bVideo)
	{
		FCrowdyAppRecording Recording;
		Recording.bKnown = true;
		Recording.bVideo = bVideo;
		Camera.HandleRecordingChanged(Recording);
	}

	static UMediaPlayer* BeginOpening(UConstructWebcamSubsystem& Camera)
	{
		UMediaPlayer* Opening = NewObject<UMediaPlayer>(&Camera);
		Opening->OnMediaEvent().AddUObject(&Camera, &UConstructWebcamSubsystem::HandleMediaEvent);
		Camera.Player = Opening;
		Camera.Step = UConstructWebcamSubsystem::EStep::Opening;
		return Opening;
	}

	static int32 RetiringCount(const UConstructWebcamSubsystem& Camera)
	{
		return Camera.RetiringPlayers.Num();
	}

	static void RetiringEvent(UConstructWebcamSubsystem& Camera, UMediaPlayer* Retiring, EMediaEvent Event)
	{
		Camera.HandleRetiringMediaEvent(Event, Retiring);
	}

	static void FailOpening(UConstructWebcamSubsystem& Camera)
	{
		Camera.Player = NewObject<UMediaPlayer>(&Camera);
		Camera.Step = UConstructWebcamSubsystem::EStep::Opening;
		Camera.HandleMediaEvent(EMediaEvent::MediaOpenFailed);
	}
};

namespace ConstructWebcamSubsystemTests
{
constexpr EAutomationTestFlags CameraTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

struct FSettingsRead
{
	int32 Token = 0;
	bool bAfterRefusal = false;
};

struct FCameraFixture
{
	TStrongObjectPtr<UGameInstance> Instance;
	TStrongObjectPtr<UConstructWebcamSubsystem> Camera;
	TStrongObjectPtr<UConstructWebcamTestListener> Listener;
	FConstructWebcamConditions Conditions;
	TArray<FSettingsRead> Reads;
	TArray<TUniqueFunction<void()>> HeldWork;
	TArray<FMediaCaptureDeviceInfo> Devices;
	int32 Sends = 0;
	int32 CheckSends = 0;
	int32 Dispatched = 0;
	bool bNearby = true;
	bool bHoldWork = false;

	FCameraFixture()
	{
		Instance.Reset(NewObject<UGameInstance>(GetTransientPackage()));
		Camera.Reset(NewObject<UConstructWebcamSubsystem>(Instance.Get()));
		Listener.Reset(NewObject<UConstructWebcamTestListener>(GetTransientPackage()));
		Camera->OnCameraStateChanged.AddDynamic(Listener.Get(), &UConstructWebcamTestListener::OnCameraState);
		Conditions.bEnabled = true;
		Conditions.bSignedIn = true;
		Conditions.bAppActive = true;
		Conditions.bGameplayAllowed = true;
		Conditions.bHasLocalPlayer = true;
		Conditions.bRecordingKnown = true;
		Conditions.UserId = 1;

		auto& Hooks = FConstructWebcamTestAccess::Hooks(*Camera);
		Hooks.Conditions = [this]() { return Conditions; };
		Hooks.ReadSettings = [this](int32 Token, bool bAfterRefusal) { Reads.Add({Token, bAfterRefusal}); };
		Hooks.EnumerateDevices = [this](TArray<FMediaCaptureDeviceInfo>& Out) { Out = Devices; };
		Hooks.Nearby = [this]() { return bNearby; };
		Hooks.Send = [this](const TArray<uint8>& Jpeg)
		{
			++Sends;
			FCrowdyVideoFrameSend Result;
			Result.Result = ECrowdyVideoSendResult::Sent;
			return Result;
		};
		Hooks.RunWork = [this](TUniqueFunction<void()> Job)
		{
			++Dispatched;
			if (bHoldWork)
			{
				HeldWork.Add(MoveTemp(Job));
				return;
			}
			Job();
		};
		UsePattern(false);
	}

	~FCameraFixture()
	{
		Camera->Deinitialize();
	}

	void UsePattern(bool bIgnoreNearbyGate)
	{
		Camera->SetFrameSourceForCheck(
		    [](TArray<FColor>& Out)
		    {
			    Out.Init(FColor(90, 90, 90, 255), ConstructWebcam::FramePixels);
			    return true;
		    },
		    bIgnoreNearbyGate, [this](const FCrowdyVideoFrameSend&) { ++CheckSends; });
	}

	void Tick()
	{
		FConstructWebcamTestAccess::Tick(*Camera);
	}

	void Answer(const FCrowdyCommunicationSettings& Settings)
	{
		const FSettingsRead Read = Reads.Num() > 0 ? Reads.Last() : FSettingsRead();
		FConstructWebcamTestAccess::Answer(*Camera, Read.Token, Read.bAfterRefusal, &Settings, nullptr);
	}

	void Fail(ECrowdySafetyErrorCode Code)
	{
		FCrowdySafetyError Error;
		Error.Code = Code;
		const FSettingsRead Read = Reads.Num() > 0 ? Reads.Last() : FSettingsRead();
		FConstructWebcamTestAccess::Answer(*Camera, Read.Token, Read.bAfterRefusal, nullptr, &Error);
	}

	static FCrowdyCommunicationSettings Allowed()
	{
		FCrowdyCommunicationSettings Settings;
		Settings.VideoSend = ECrowdyCommunicationScope::Everyone;
		Settings.VideoReceive = ECrowdyCommunicationScope::Everyone;
		return Settings;
	}

	void TurnOn()
	{
		Camera->SetCameraOn(true);
		Answer(Allowed());
		Tick();
		Tick();
	}

	bool Is(EConstructCameraState State, EConstructCameraBlockedReason Reason) const
	{
		return Camera->GetCameraState() == State && Camera->GetBlockedReason() == Reason;
	}
};
}

using namespace ConstructWebcamSubsystemTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamReasonsTest, "Construct.Webcam.Camera.BlockedReasons", CameraTestFlags)
bool FConstructWebcamReasonsTest::RunTest(const FString& Parameters)
{
	using EState = EConstructCameraState;
	using EReason = EConstructCameraBlockedReason;
	{
		FCameraFixture F;
		F.Conditions.bEnabled = false;
		F.Camera->SetCameraOn(true);
		TestTrue(TEXT("Disabled for the project"), F.Is(EState::Unavailable, EReason::WebcamsDisabled));
	}
	{
		FCameraFixture F;
		F.Camera->ClearFrameSourceForCheck();
		F.Camera->SetCameraOn(true);
		TestTrue(TEXT("No camera listed"), F.Is(EState::Unavailable, EReason::NoCamera));
		F.Camera->SetCameraOn(false);
		TestTrue(TEXT("Turning off keeps Unavailable"), F.Is(EState::Unavailable, EReason::NoCamera));
		TestEqual(TEXT("No settings read without a camera"), F.Reads.Num(), 0);
	}
	{
		FCameraFixture F;
		F.Conditions.bSignedIn = false;
		F.Camera->SetCameraOn(true);
		TestTrue(TEXT("Not signed in"), F.Is(EState::Off, EReason::NotSignedIn));
	}
	{
		FCameraFixture F;
		F.Camera->SetCameraOn(true);
		TestTrue(TEXT("Starting while the settings are read"), F.Is(EState::Starting, EReason::None));
		TestEqual(TEXT("One settings read"), F.Reads.Num(), 1);
		F.Answer(FCrowdyCommunicationSettings());
		TestTrue(TEXT("Video Send off"), F.Is(EState::Off, EReason::SendingOff));
	}
	{
		FCameraFixture F;
		F.Camera->SetCameraOn(true);
		FCrowdyCommunicationSettings Minor = FCameraFixture::Allowed();
		Minor.bIsMinor = true;
		F.Answer(Minor);
		TestTrue(TEXT("A minor without consent"), F.Is(EState::Off, EReason::NeedsParentConsent));
	}
	{
		FCameraFixture F;
		F.Camera->SetCameraOn(true);
		F.Fail(ECrowdySafetyErrorCode::ConsentUnavailable);
		TestTrue(TEXT("Consent unavailable"), F.Is(EState::Off, EReason::ConsentUnavailable));
	}
	{
		FCameraFixture F;
		F.Camera->SetCameraOn(true);
		F.Fail(ECrowdySafetyErrorCode::RateLimited);
		TestTrue(TEXT("Rate limited"), F.Is(EState::Off, EReason::TryAgainLater));
	}
	{
		FCameraFixture F;
		F.Conditions.bRecordingKnown = false;
		F.Camera->SetCameraOn(true);
		F.Answer(FCameraFixture::Allowed());
		F.Tick();
		TestTrue(TEXT("Checking whether the app records"), F.Is(EState::Starting, EReason::CheckingRecording));
		F.Tick();
		TestTrue(TEXT("Still checking while unknown"), F.Is(EState::Starting, EReason::CheckingRecording));
		F.Conditions.bRecordingKnown = true;
		F.Tick();
		TestTrue(TEXT("Known, the notice step"), F.Is(EState::Starting, EReason::None));
		F.Tick();
		TestTrue(TEXT("Then On"), F.Is(EState::On, EReason::None));
	}
	{
		AddExpectedMessage(TEXT("the camera stopped delivering frames"), ELogVerbosity::Warning,
		                   EAutomationExpectedMessageFlags::Contains, 1, false);
		FCameraFixture F;
		F.Camera->SetCameraOn(true);
		FConstructWebcamTestAccess::FailOpening(*F.Camera);
		TestTrue(TEXT("Capture failed"), F.Is(EState::Off, EReason::CaptureFailed));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamNoticeTest, "Construct.Webcam.Camera.RecordingNoticeBeforeCapture", CameraTestFlags)
bool FConstructWebcamNoticeTest::RunTest(const FString& Parameters)
{
	FCameraFixture F;
	F.Conditions.bRecordingVideo = true;
	F.Camera->SetCameraOn(true);
	TestTrue(TEXT("Recording is known before the camera opens"), F.Camera->IsBeingRecorded());
	F.Answer(FCameraFixture::Allowed());
	const int32 Before = F.Listener->States.Num();
	F.Tick();
	TestEqual(TEXT("Still Starting on the notice tick"), F.Camera->GetCameraState(), EConstructCameraState::Starting);
	TestEqual(TEXT("The notice step tells the UI"), F.Listener->States.Num(), Before + 1);
	TestEqual(TEXT("No frame before the notice"), F.Sends, 0);
	TestNull(TEXT("No preview before the notice"), F.Camera->GetLocalPreview());
	F.Tick();
	TestEqual(TEXT("On only on the next tick"), F.Camera->GetCameraState(), EConstructCameraState::On);
	F.Tick();
	TestEqual(TEXT("Then frames go out"), F.Sends, 1);
	TestNotNull(TEXT("And the preview shows"), F.Camera->GetLocalPreview());

	const int32 WhileOn = F.Listener->States.Num();
	FConstructWebcamTestAccess::RecordingChanged(*F.Camera, false);
	TestFalse(TEXT("Recording off"), F.Camera->IsBeingRecorded());
	FConstructWebcamTestAccess::RecordingChanged(*F.Camera, true);
	TestTrue(TEXT("A change to recorded while On is told at once"), F.Camera->IsBeingRecorded());
	TestEqual(TEXT("Each change fires the event"), F.Listener->States.Num(), WhileOn + 2);
	TestEqual(TEXT("With the camera still On"), F.Listener->States.Last(), EConstructCameraState::On);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamClearOrKeepTest, "Construct.Webcam.Camera.ClearOrKeepChoice", CameraTestFlags)
bool FConstructWebcamClearOrKeepTest::RunTest(const FString& Parameters)
{
	using EState = EConstructCameraState;
	using EReason = EConstructCameraBlockedReason;
	FCameraFixture F;
	F.TurnOn();
	TestTrue(TEXT("On"), F.Is(EState::On, EReason::None));

	F.Conditions.bAppActive = false;
	F.Tick();
	TestTrue(TEXT("App paused waits with the choice kept"), F.Is(EState::Starting, EReason::None));
	TestNull(TEXT("And releases the camera"), F.Camera->GetLocalPreview());
	F.Conditions.bAppActive = true;
	F.Tick();
	TestEqual(TEXT("Back again, the settings are read again"), F.Reads.Num(), 2);
	F.Answer(FCameraFixture::Allowed());
	F.Tick();
	F.Tick();
	TestTrue(TEXT("And the camera is On without a fresh toggle"), F.Is(EState::On, EReason::None));

	F.Conditions.bGameplayAllowed = false;
	F.Tick();
	TestTrue(TEXT("Gameplay input lost keeps the choice"), F.Is(EState::Starting, EReason::None));
	F.Conditions.bGameplayAllowed = true;
	F.Tick();
	TestEqual(TEXT("Input back reads the settings again"), F.Reads.Num(), 3);

	FConstructWebcamTestAccess::Logout(*F.Camera);
	TestTrue(TEXT("Sign-out stops"), F.Is(EState::Off, EReason::NotSignedIn));
	F.Tick();
	TestTrue(TEXT("And clears the choice"), F.Is(EState::Off, EReason::NotSignedIn));
	TestEqual(TEXT("No restart after sign-out"), F.Reads.Num(), 3);

	F.TurnOn();
	FConstructWebcamTestAccess::AccessEnded(*F.Camera);
	F.Tick();
	TestTrue(TEXT("Access ended clears the choice"), F.Is(EState::Off, EReason::NotSignedIn));

	F.TurnOn();
	FConstructWebcamTestAccess::TravelMap(*F.Camera);
	F.Tick();
	TestTrue(TEXT("Map travel clears the choice"), F.Is(EState::Off, EReason::None));

	F.TurnOn();
	F.Conditions.UserId = 2;
	F.Tick();
	TestTrue(TEXT("Another session clears the choice"), F.Is(EState::Off, EReason::None));
	F.Conditions.UserId = 1;

	F.TurnOn();
	F.Conditions.bSignedIn = false;
	F.Tick();
	TestTrue(TEXT("Signed out without the event clears the choice"), F.Is(EState::Off, EReason::NotSignedIn));
	F.Conditions.bSignedIn = true;

	F.TurnOn();
	F.Camera->SetCameraOn(false);
	F.Tick();
	TestTrue(TEXT("The toggle clears the choice"), F.Is(EState::Off, EReason::None));
	TestNull(TEXT("And the preview"), F.Camera->GetLocalPreview());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamLateTest, "Construct.Webcam.Camera.LateCompletionsDoNothing", CameraTestFlags)
bool FConstructWebcamLateTest::RunTest(const FString& Parameters)
{
	using EState = EConstructCameraState;
	FCameraFixture F;
	F.Camera->SetCameraOn(true);
	F.Camera->SetCameraOn(false);
	F.Answer(FCameraFixture::Allowed());
	F.Tick();
	F.Tick();
	TestEqual(TEXT("A settings answer after the stop starts nothing"), F.Camera->GetCameraState(), EState::Off);

	F.Camera->SetCameraOn(true);
	const FSettingsRead Stale = F.Reads.Last();
	F.Camera->SetCameraOn(false);
	F.Camera->SetCameraOn(true);
	const FCrowdyCommunicationSettings Allowed = FCameraFixture::Allowed();
	FConstructWebcamTestAccess::Answer(*F.Camera, Stale.Token, false, &Allowed, nullptr);
	F.Tick();
	F.Tick();
	TestEqual(TEXT("An answer for an earlier start is ignored"), F.Camera->GetCameraState(), EState::Starting);
	F.Answer(Allowed);
	F.Tick();
	F.Tick();
	TestEqual(TEXT("The current answer starts the camera"), F.Camera->GetCameraState(), EState::On);

	F.bHoldWork = true;
	F.Tick();
	TestEqual(TEXT("An encode is in flight"), F.HeldWork.Num(), 1);
	F.Camera->SetCameraOn(false);
	F.HeldWork[0]();
	TestEqual(TEXT("An encode finishing after the stop sends nothing"), F.Sends, 0);
	TestNull(TEXT("And shows no preview"), F.Camera->GetLocalPreview());

	F.HeldWork.Reset();
	F.bHoldWork = true;
	F.TurnOn();
	F.Tick();
	F.Camera->SetCameraOn(false);
	F.bHoldWork = false;
	F.TurnOn();
	F.HeldWork[0]();
	TestEqual(TEXT("An encode from an earlier start sends nothing"), F.Sends, 0);
	F.Tick();
	TestEqual(TEXT("The current start still sends"), F.Sends, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamNearbyTest, "Construct.Webcam.Camera.NearbyGate", CameraTestFlags)
bool FConstructWebcamNearbyTest::RunTest(const FString& Parameters)
{
	FCameraFixture F;
	F.bNearby = false;
	F.TurnOn();
	F.Tick();
	TestEqual(TEXT("Alone, nothing is sent"), F.Sends, 0);
	TestNotNull(TEXT("But the preview still updates"), F.Camera->GetLocalPreview());
	F.bNearby = true;
	F.Tick();
	TestEqual(TEXT("With a player near, the frame goes out"), F.Sends, 1);
	TestEqual(TEXT("And the check hears of it"), F.CheckSends, 1);

	F.UsePattern(true);
	F.Answer(FCameraFixture::Allowed());
	F.Tick();
	F.Tick();
	F.bNearby = false;
	F.Tick();
	TestEqual(TEXT("A source that ignores the gate sends alone"), F.Sends, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamRefusalTest, "Construct.Webcam.Camera.RefusalStopsAndExplains", CameraTestFlags)
bool FConstructWebcamRefusalTest::RunTest(const FString& Parameters)
{
	using EState = EConstructCameraState;
	using EReason = EConstructCameraBlockedReason;
	AddExpectedMessage(TEXT("the server refused a video frame"), ELogVerbosity::Warning,
	                   EAutomationExpectedMessageFlags::Contains, 3, false);
	FCameraFixture F;
	F.TurnOn();
	FConstructWebcamTestAccess::Refuse(*F.Camera);
	TestTrue(TEXT("A refusal stops the camera at once"), F.Is(EState::Off, EReason::AppDoesNotPermit));
	TestNull(TEXT("And releases it"), F.Camera->GetLocalPreview());
	TestTrue(TEXT("And reads the settings again"), F.Reads.Last().bAfterRefusal);
	F.Answer(FCrowdyCommunicationSettings());
	TestTrue(TEXT("Video Send off explains it"), F.Is(EState::Off, EReason::SendingOff));
	F.Tick();
	TestTrue(TEXT("The choice is cleared"), F.Is(EState::Off, EReason::SendingOff));

	F.TurnOn();
	FConstructWebcamTestAccess::Refuse(*F.Camera);
	FCrowdyCommunicationSettings Minor = FCameraFixture::Allowed();
	Minor.bIsMinor = true;
	F.Answer(Minor);
	TestTrue(TEXT("A minor without consent explains it"), F.Is(EState::Off, EReason::NeedsParentConsent));

	F.TurnOn();
	FConstructWebcamTestAccess::Refuse(*F.Camera);
	F.Answer(FCameraFixture::Allowed());
	TestTrue(TEXT("Otherwise the app does not permit video"), F.Is(EState::Off, EReason::AppDoesNotPermit));

	const int32 Reads = F.Reads.Num();
	FConstructWebcamTestAccess::Refuse(*F.Camera);
	TestEqual(TEXT("A refusal while Off reads nothing"), F.Reads.Num(), Reads);
	TestTrue(TEXT("And changes nothing"), F.Is(EState::Off, EReason::AppDoesNotPermit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamRefusalPausedTest, "Construct.Webcam.Camera.RefusalWhileWaitingStops", CameraTestFlags)
bool FConstructWebcamRefusalPausedTest::RunTest(const FString& Parameters)
{
	using EState = EConstructCameraState;
	using EReason = EConstructCameraBlockedReason;
	AddExpectedMessage(TEXT("the server refused a video frame"), ELogVerbosity::Warning,
	                   EAutomationExpectedMessageFlags::Contains, 1, false);
	FCameraFixture F;
	F.TurnOn();
	F.Conditions.bAppActive = false;
	F.Tick();
	TestTrue(TEXT("Paused with the choice kept"), F.Is(EState::Starting, EReason::None));
	FConstructWebcamTestAccess::Refuse(*F.Camera);
	TestTrue(TEXT("A refusal while paused still stops"), F.Is(EState::Off, EReason::AppDoesNotPermit));
	TestTrue(TEXT("And reads the settings for the reason"), F.Reads.Last().bAfterRefusal);
	const int32 Reads = F.Reads.Num();
	F.Conditions.bAppActive = true;
	F.Tick();
	TestTrue(TEXT("Unpausing does not restart it"), F.Is(EState::Off, EReason::AppDoesNotPermit));
	TestEqual(TEXT("No start read follows"), F.Reads.Num(), Reads);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamSignOutDropsReadsTest, "Construct.Webcam.Camera.SignOutDropsPendingReads", CameraTestFlags)
bool FConstructWebcamSignOutDropsReadsTest::RunTest(const FString& Parameters)
{
	using EState = EConstructCameraState;
	using EReason = EConstructCameraBlockedReason;
	AddExpectedMessage(TEXT("the server refused a video frame"), ELogVerbosity::Warning,
	                   EAutomationExpectedMessageFlags::Contains, 1, false);
	FCameraFixture F;
	F.TurnOn();
	FConstructWebcamTestAccess::Refuse(*F.Camera);
	FConstructWebcamTestAccess::Logout(*F.Camera);
	F.Answer(FCrowdyCommunicationSettings());
	TestTrue(TEXT("A refusal read answering after sign-out changes nothing"),
	         F.Is(EState::Off, EReason::AppDoesNotPermit));

	F.Camera->SetCameraOn(true);
	F.Answer(FCrowdyCommunicationSettings());
	TestTrue(TEXT("Video Send off"), F.Is(EState::Off, EReason::SendingOff));
	F.Camera->RefreshSettings();
	FConstructWebcamTestAccess::AccessEnded(*F.Camera);
	F.Answer(FCameraFixture::Allowed());
	TestTrue(TEXT("A refresh answering after access ended changes nothing"), F.Is(EState::Off, EReason::SendingOff));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamRefreshTest, "Construct.Webcam.Camera.RefreshSettingsApplies", CameraTestFlags)
bool FConstructWebcamRefreshTest::RunTest(const FString& Parameters)
{
	using EState = EConstructCameraState;
	using EReason = EConstructCameraBlockedReason;
	FCameraFixture F;
	F.TurnOn();
	F.Camera->RefreshSettings();
	F.Answer(FCameraFixture::Allowed());
	TestTrue(TEXT("Settings that still allow video keep the camera On"), F.Is(EState::On, EReason::None));

	F.Camera->RefreshSettings();
	F.Answer(FCrowdyCommunicationSettings());
	TestTrue(TEXT("Video Send now off stops the camera"), F.Is(EState::Off, EReason::SendingOff));
	TestNull(TEXT("And releases it"), F.Camera->GetLocalPreview());
	F.Tick();
	TestTrue(TEXT("And clears the choice"), F.Is(EState::Off, EReason::SendingOff));

	F.Camera->RefreshSettings();
	F.Answer(FCameraFixture::Allowed());
	TestTrue(TEXT("Video Send back on lifts the reason without starting"), F.Is(EState::Off, EReason::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamRetireTest, "Construct.Webcam.Camera.StopWhileOpeningRetiresPlayer", CameraTestFlags)
bool FConstructWebcamRetireTest::RunTest(const FString& Parameters)
{
	FCameraFixture F;
	F.Devices.Add({FText(), FString(), EMediaCaptureDeviceType::Video, TEXT("vidcap://test")});
	F.Camera->ClearFrameSourceForCheck();

	F.Camera->SetCameraOn(true);
	UMediaPlayer* First = FConstructWebcamTestAccess::BeginOpening(*F.Camera);
	F.Camera->SetCameraOn(false);
	TestEqual(TEXT("A player stopped while opening is retired, not closed"),
	          FConstructWebcamTestAccess::RetiringCount(*F.Camera), 1);
	TestEqual(TEXT("And the camera is Off"), F.Camera->GetCameraState(), EConstructCameraState::Off);
	FConstructWebcamTestAccess::RetiringEvent(*F.Camera, First, EMediaEvent::MediaBuffering);
	TestEqual(TEXT("Other events keep it waiting"), FConstructWebcamTestAccess::RetiringCount(*F.Camera), 1);

	F.Camera->SetCameraOn(true);
	UMediaPlayer* Second = FConstructWebcamTestAccess::BeginOpening(*F.Camera);
	TestTrue(TEXT("A new start opens a fresh player"), Second != First);
	F.Camera->SetCameraOn(false);
	TestEqual(TEXT("Both wait for their open"), FConstructWebcamTestAccess::RetiringCount(*F.Camera), 2);

	FConstructWebcamTestAccess::RetiringEvent(*F.Camera, First, EMediaEvent::MediaOpened);
	TestEqual(TEXT("Its open closes and drops the first"), FConstructWebcamTestAccess::RetiringCount(*F.Camera), 1);
	FConstructWebcamTestAccess::RetiringEvent(*F.Camera, Second, EMediaEvent::MediaOpenFailed);
	TestEqual(TEXT("A failed open drops the second"), FConstructWebcamTestAccess::RetiringCount(*F.Camera), 0);

	F.Camera->SetCameraOn(true);
	UMediaPlayer* Third = FConstructWebcamTestAccess::BeginOpening(*F.Camera);
	F.Camera->SetCameraOn(false);
	FConstructWebcamTestAccess::RetiringEvent(*F.Camera, Third, EMediaEvent::TracksChanged);
	TestEqual(TEXT("Tracks arriving drop the third"), FConstructWebcamTestAccess::RetiringCount(*F.Camera), 0);
	TestEqual(TEXT("None of it restarted the camera"), F.Camera->GetCameraState(), EConstructCameraState::Off);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamLoneTest, "Construct.Webcam.Camera.LoneFramesAreNeverEncoded", CameraTestFlags)
bool FConstructWebcamLoneTest::RunTest(const FString& Parameters)
{
	FCameraFixture F;
	F.bNearby = false;
	F.TurnOn();
	F.Dispatched = 0;
	F.Tick();
	F.Tick();
	F.Tick();
	TestEqual(TEXT("Alone, no encode is dispatched"), F.Dispatched, 0);
	TestEqual(TEXT("And nothing is sent"), F.Sends, 0);
	TestNotNull(TEXT("While the preview still updates"), F.Camera->GetLocalPreview());
	F.bNearby = true;
	F.Tick();
	TestEqual(TEXT("A player near starts encoding"), F.Dispatched, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamLeftMidEncodeTest, "Construct.Webcam.Camera.PlayerLeavingMidEncodeSendsNothing", CameraTestFlags)
bool FConstructWebcamLeftMidEncodeTest::RunTest(const FString& Parameters)
{
	FCameraFixture F;
	F.TurnOn();
	F.bHoldWork = true;
	F.Tick();
	if (!TestEqual(TEXT("An encode started with a player near"), F.HeldWork.Num(), 1))
	{
		return true;
	}
	F.bNearby = false;
	F.HeldWork[0]();
	TestEqual(TEXT("The player left before it finished, so nothing is sent"), F.Sends, 0);
	TestNotNull(TEXT("The frame still reaches the preview"), F.Camera->GetLocalPreview());
	return true;
}

#endif
