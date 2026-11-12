#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructShellDiagnostics.h"
#include "ConstructShellPresentation.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructShellPresentationTest,
                                 "Construct.Shell.Lifecycle.PresentationPolicy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructShellPresentationTest::RunTest(const FString& Parameters)
{
	using namespace ConstructShellPresentation;
	TestTrue(TEXT("No account shows login"), ShouldShowSignIn(false, false, false));
	TestFalse(TEXT("Signed-in account hides login"), ShouldShowSignIn(false, true, false));
	TestTrue(TEXT("Ended access shows login even with identity still present"), ShouldShowSignIn(false, true, true));
	TestFalse(TEXT("Signing in is not application readiness"), IsSessionReady(false, true, false, false, false, false));
	TestTrue(TEXT("Connected authorized account can enter"), IsSessionReady(false, true, false, true, false, false));
	TestFalse(TEXT("Legal consent prevents entry"), IsSessionReady(false, true, false, true, true, false));
	TestFalse(TEXT("Paused application prevents entry"), IsSessionReady(false, true, false, true, false, true));
	TestFalse(TEXT("Ended access prevents entry"), IsSessionReady(false, true, true, true, false, false));
	TestFalse(TEXT("Disconnected signed-out account cannot enter"),
	          IsSessionReady(false, false, false, false, false, false));
	TestTrue(TEXT("Explicit local preview remains usable without authentication"),
	         IsSessionReady(true, false, false, false, false, false));
	TestEqual(TEXT("Full application has useful feedback"),
	          ConnectionCaption(EUDPConnectionState::GateKeep, false, TEXT(""), true).ToString(),
	          FString(TEXT("App is full")));
	TestEqual(TEXT("Reconnect remains visibly pending"),
	          ConnectionCaption(EUDPConnectionState::Reconnecting, false, TEXT(""), true).ToString(),
	          FString(TEXT("Reconnecting...")));
	TestEqual(TEXT("Pause reason is visible in UI"),
	          ConnectionCaption(EUDPConnectionState::Connected, true, TEXT("Maintenance"), true).ToString(),
	          FString(TEXT("App paused: Maintenance")));
	TestEqual(TEXT("Missing SDK has explicit feedback"),
	          ConnectionCaption(EUDPConnectionState::Disconnected, false, TEXT(""), false).ToString(),
	          FString(TEXT("Crowdy unavailable")));
	TestEqual(TEXT("A session released for idleness says how to come back"),
	          ConnectionCaption(EUDPConnectionState::Inactive, false, TEXT(""), true).ToString(),
	          FString(TEXT("Away: press any key to come back")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructShellIdleResumeTest,
                                 "Construct.Shell.Lifecycle.IdleResume",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructShellIdleResumeTest::RunTest(const FString& Parameters)
{
	using namespace ConstructShellPresentation;
	constexpr EUDPConnectionState Away = EUDPConnectionState::Inactive;
	double AwaySince = -1.0;
	TestFalse(TEXT("Seeing the release does not resume"), StepIdleResume(AwaySince, Away, true, 100.0));
	TestEqual(TEXT("The release records the last input time"), AwaySince, 100.0);
	TestFalse(TEXT("No input since the release keeps the player away"), StepIdleResume(AwaySince, Away, true, 100.0));
	TestFalse(TEXT("Input while a resume is not allowed is held back"), StepIdleResume(AwaySince, Away, false, 120.0));
	TestTrue(TEXT("Input after the release brings the player back"), StepIdleResume(AwaySince, Away, true, 120.0));
	TestFalse(TEXT("The same input resumes only once"), StepIdleResume(AwaySince, Away, true, 120.0));
	TestTrue(TEXT("Newer input resumes again"), StepIdleResume(AwaySince, Away, true, 130.0));
	TestFalse(TEXT("A connected session needs no resume"),
	          StepIdleResume(AwaySince, EUDPConnectionState::Connected, true, 140.0));
	TestEqual(TEXT("Leaving Inactive forgets the release"), AwaySince, -1.0);
	TestFalse(TEXT("A dropped connection is the SDK's reconnect, not an idle resume"),
	          StepIdleResume(AwaySince, EUDPConnectionState::Disconnected, true, 150.0));
	TestFalse(TEXT("A second release starts from its own last input"), StepIdleResume(AwaySince, Away, true, 150.0));

	TestTrue(TEXT("A signed-in focused player may resume"), CanResumeAfterIdle(false, true, false, true));
	TestFalse(TEXT("Local preview never opens a session"), CanResumeAfterIdle(true, true, false, true));
	TestFalse(TEXT("A signed-out player has no token to resume with"), CanResumeAfterIdle(false, false, false, true));
	TestFalse(TEXT("Ended access is not resumed"), CanResumeAfterIdle(false, true, true, true));
	TestFalse(TEXT("A mouse over an unfocused window is not a return"), CanResumeAfterIdle(false, true, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructShellDiagnosticsTest,
                                 "Construct.Shell.Lifecycle.SafeDiagnostics",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructShellDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace ConstructShellDiagnostics;
	TestEqual(TEXT("Restore miss keeps its useful category"),
	          Issue(TEXT("No saved session found")),
	          FString(TEXT("No saved session found")));
	TestEqual(TEXT("Known errors retain a safe explanation"),
	          Issue(TEXT("Invalid credentials for private@example.invalid")),
	          FString(TEXT("Invalid credentials")));
	TestEqual(TEXT("Unknown backend prose is never copied into logs"),
	          Issue(TEXT(
	              "private@example.test password=not-a-real-password https://example.test/?token=private\nforged log")),
	          FString(TEXT("Details shown in runtime UI")));
	TestEqual(TEXT("Opaque identifiers cannot leak through log detail"),
	          Issue(FString::ChrN(4096, TEXT('x'))),
	          FString(TEXT("Details shown in runtime UI")));
	TestEqual(TEXT("Suspension has a useful safe reason"),
	          Issue(TEXT("Account suspended: private@example.invalid")),
	          FString(TEXT("Access suspended or blocked")));
	TestEqual(TEXT("Missing reason is explicit"), Issue(TEXT("")), FString(TEXT("No reason supplied")));
	TestEqual(TEXT("Valid deadline is normalized"),
	          Deadline(TEXT("2030-01-02T03:04:05Z")),
	          FString(TEXT("2030-01-02T03:04:05.000Z")));
	TestEqual(TEXT("Malformed deadline cannot inject a log"),
	          Deadline(TEXT("private@example.invalid\nforged")),
	          FString(TEXT("unspecified")));
	return true;
}
#endif
