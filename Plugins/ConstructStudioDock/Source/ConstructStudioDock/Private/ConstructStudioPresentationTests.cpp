#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioPresentation.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace ConstructStudioPresentationTests
{
constexpr auto PresentationTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDefaultLayoutTest, "Construct.StudioPanel.Layout.AcceptedDefaults", ConstructStudioPresentationTests::PresentationTestFlags)
bool FConstructStudioDefaultLayoutTest::RunTest(const FString&)
{
    const FConstructStudioLayout Wide = FConstructStudioLayout::Calculate(1920);
    TestFalse(TEXT("Wide window is inset rather than fullscreen"), Wide.bFullscreen);
    TestTrue(TEXT("Chosen dock uses52percent"), FMath::IsNearlyEqual(Wide.DockPixels, 998.4f, 0.01f));
    TestTrue(TEXT("Game keeps48percent"), FMath::IsNearlyEqual(Wide.GameFraction, 0.48f, 0.0001f));
    const FConstructStudioLayout Narrow = FConstructStudioLayout::Calculate(999);
    TestTrue(TEXT("Below1000 opens fullscreen"), Narrow.bFullscreen);
    TestEqual(TEXT("Fullscreen retains a valid scene projection behind the frame"), Narrow.GameFraction, 1.0f);
    TestFalse(TEXT("Exactly1000 is docked"), FConstructStudioLayout::Calculate(1000).bFullscreen);
    TestEqual(TEXT("Invalid viewport owns no inset"), FConstructStudioLayout::Calculate(0).GameFraction, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioResizeBoundsTest, "Construct.StudioPanel.Layout.ResizerPreservesGameAndDockMinimums", ConstructStudioPresentationTests::PresentationTestFlags)
bool FConstructStudioResizeBoundsTest::RunTest(const FString&)
{
    TestEqual(TEXT("Narrow drag stops at480 dock pixels"), FConstructStudioLayout::Calculate(1920, 0.01f).DockPixels, 480.0f);
    const FConstructStudioLayout Huge = FConstructStudioLayout::Calculate(1920, 0.99f);
    TestTrue(TEXT("Wide drag leaves420 game pixels"), FMath::IsNearlyEqual(Huge.GameFraction * 1920, 420.0f, 0.01f));
    TestEqual(TEXT("NaN request uses accepted default"), FConstructStudioLayout::Calculate(1000, std::numeric_limits<float>::quiet_NaN()).DockPixels, 520.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioPromptInputTest, "Construct.StudioPanel.Input.NativePromptRetainsModalAfterBrowserLosesFocus", ConstructStudioPresentationTests::PresentationTestFlags)
bool FConstructStudioPromptInputTest::RunTest(const FString&)
{
    FConstructStudioPresentation View;
    View.State = EConstructStudioDockState::Ready;
    TestFalse(TEXT("Docked unfocused pane permits gameplay"), View.HoldsModal(false));
    View.bBrowserFocused = true;
    TestTrue(TEXT("Browser focus blocks movement and look"), View.HoldsModal(false));
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 900000000000000001LL;
    View.Present(Prompt, 100);
    View.bBrowserFocused = false;
    TestTrue(TEXT("Native prompt continues holding input"), View.HoldsModal(false));
    View.Withdraw(Prompt.PromptId);
    TestFalse(TEXT("Prompt to game can release only this modal"), View.HoldsModal(false));
    View.State = EConstructStudioDockState::NeedsConsent;
    TestTrue(TEXT("Provider notice is independently modal"), View.HoldsModal(false));
    View.State = EConstructStudioDockState::Ready;
    TestTrue(TEXT("Fullscreen stays modal without browser focus"), View.HoldsModal(true));
    View.Reset();
    TestFalse(TEXT("Closed fullscreen never blocks input"), View.HoldsModal(true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioReplacementGuardTest, "Construct.StudioPanel.Prompt.ReplacementRestartsClickGuard", ConstructStudioPresentationTests::PresentationTestFlags)
bool FConstructStudioReplacementGuardTest::RunTest(const FString&)
{
    FConstructStudioPresentation View;
    View.State = EConstructStudioDockState::Ready;
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 1;
    Prompt.Revision = TEXT("old");
    View.Present(Prompt, 100);
    TestFalse(TEXT("Run unavailable before one second"), View.CanRun(100.999));
    TestTrue(TEXT("Run available after one second"), View.CanRun(101));
    Prompt.PromptId = 2;
    Prompt.Revision = TEXT("new");
    View.Present(Prompt, 105);
    View.Withdraw(1);
    TestTrue(TEXT("Stale withdrawal leaves replacement intact"), View.Prompt.IsSet());
    TestEqual(TEXT("New revision is displayed"), View.Prompt->Revision, FString(TEXT("new")));
    TestFalse(TEXT("Old guard age cannot approve replacement"), View.CanRun(105.5));
    TestTrue(TEXT("Replacement receives its full one second"), View.CanRun(106));
    View.Withdraw(2);
    TestFalse(TEXT("Withdrawn prompt cannot be run"), View.CanRun(200));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioVisibleGuardTest, "Construct.StudioPanel.Prompt.ClickGuardStartsWhenPromptIsSeen", ConstructStudioPresentationTests::PresentationTestFlags)
bool FConstructStudioVisibleGuardTest::RunTest(const FString&)
{
    FConstructStudioPresentation View;
    View.State = EConstructStudioDockState::Ready;
    View.Warning = FText::FromString(TEXT("Agent: earlier"));
    FConstructStudioDeployPrompt Prompt;
    Prompt.PromptId = 3;
    View.Present(Prompt, 100, false);
    TestTrue(TEXT("A new prompt clears the earlier warning"), View.Warning.IsEmpty());
    TestFalse(TEXT("A hidden prompt is never runnable"), View.CanRun(500));
    View.ShowPrompt(true, 600);
    TestFalse(TEXT("Its guard starts when it is first seen"), View.CanRun(600.999));
    TestTrue(TEXT("and lasts one second"), View.CanRun(601));
    View.ShowPrompt(true, 650);
    TestTrue(TEXT("Staying visible does not restart the guard"), View.CanRun(650));
    View.ShowPrompt(false, 700);
    TestFalse(TEXT("Hiding it disables Run"), View.CanRun(800));
    View.ShowPrompt(true, 900);
    TestFalse(TEXT("Showing it again restarts the guard"), View.CanRun(900.5));
    TestTrue(TEXT("for one more second"), View.CanRun(901));
    View.Warning = FText::FromString(TEXT("Agent: later"));
    TestFalse(TEXT("A warning stays hidden while the prompt shows"), View.ShowsWarning());
    View.Withdraw(3);
    TestTrue(TEXT("and appears once the prompt is gone"), View.ShowsWarning());
    return true;
}

#endif

