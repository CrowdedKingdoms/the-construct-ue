#include "ConstructModScriptInput.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPointerDrainTest, "Construct.ModScripts.Input.PerDrain", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructPointerDrainTest::RunTest(const FString& Parameters)
{
    FConstructPointerQueue Queue;
    Queue.Record(0, true, 100, FVector2D(0.25, -0.5));
    Queue.Record(0, true, 110, FVector2D::ZeroVector);
    FConstructPointerSnapshot First = Queue.Drain(120);
    TestEqual(TEXT("A drain returns each down once and ignores duplicate downs"), First.Clicks.Num(), 1);
    TestEqual(TEXT("The original timestamp is preserved"), First.Clicks[0].AtMs, int64(100));
    TestEqual(TEXT("Press duration is zero"), First.Clicks[0].HeldMs, int64(0));
    TestEqual(TEXT("X is preserved"), First.Clicks[0].X, 0.25f);
    TestEqual(TEXT("Y is preserved"), First.Clicks[0].Y, -0.5f);
    FConstructPointerSnapshot Second = Queue.Drain(130);
    TestTrue(TEXT("The second drain has no repeated click"), Second.Clicks.IsEmpty());
    TestEqual(TEXT("Draining keeps a button held"), Second.Buttons, 1);
    TestEqual(TEXT("Held time advances across drains"), Second.LeftHeldMs, int64(30));
    Queue.Record(0, false, 160, FVector2D::ZeroVector);
    Queue.Record(0, false, 170, FVector2D::ZeroVector);
    const FConstructPointerSnapshot Released = Queue.Drain(180);
    TestEqual(TEXT("Duplicate releases are ignored"), Released.Clicks.Num(), 1);
    TestFalse(TEXT("The release is marked up"), Released.Clicks[0].bDown);
    TestEqual(TEXT("The release measures from the first down"), Released.Clicks[0].HeldMs, int64(60));
    TestEqual(TEXT("Released buttons have no held bit"), Released.Buttons, 0);
    TestEqual(TEXT("Released buttons have no live held time"), Released.LeftHeldMs, int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPointerButtonsTest, "Construct.ModScripts.Input.ButtonMappingAndHeld", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructPointerButtonsTest::RunTest(const FString& Parameters)
{
    FConstructPointerQueue Queue;
    Queue.Record(0, true, 100, FVector2D::ZeroVector);
    Queue.Record(1, true, 110, FVector2D::ZeroVector);
    Queue.Record(2, true, 120, FVector2D::ZeroVector);
    const FConstructPointerSnapshot All = Queue.Drain(150);
    TestEqual(TEXT("All three held bits are present"), All.Buttons, 7);
    TestEqual(TEXT("Left held time"), All.LeftHeldMs, int64(50));
    TestEqual(TEXT("Middle held time"), All.MiddleHeldMs, int64(40));
    TestEqual(TEXT("Right held time"), All.RightHeldMs, int64(30));
    TestEqual(TEXT("Left event button number"), All.Clicks[0].Button, 0);
    TestEqual(TEXT("Middle event button number"), All.Clicks[1].Button, 1);
    TestEqual(TEXT("Right event button number"), All.Clicks[2].Button, 2);
    Queue.Record(0, false, 160, FVector2D::ZeroVector);
    TestEqual(TEXT("Middle and right use bits 4 and 2"), Queue.Drain(160).Buttons, 6);
    Queue.Record(1, false, 170, FVector2D::ZeroVector);
    TestEqual(TEXT("Right uses bit 2"), Queue.Drain(170).Buttons, 2);
    Queue.Record(2, false, 180, FVector2D::ZeroVector);
    TestEqual(TEXT("All releases clear their bits"), Queue.Drain(180).Buttons, 0);
    Queue.Record(1, true, 200, FVector2D::ZeroVector);
    TestEqual(TEXT("Middle alone uses bit 4"), Queue.Drain(210).Buttons, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPointerCapacityTest, "Construct.ModScripts.Input.OldestDropped", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructPointerCapacityTest::RunTest(const FString& Parameters)
{
    FConstructPointerQueue Queue;
    for (int32 Index = 0; Index < 70; ++Index)
    {
        Queue.Record(0, Index % 2 == 0, Index, FVector2D::ZeroVector);
    }
    const FConstructPointerSnapshot Snapshot = Queue.Drain(80);
    TestEqual(TEXT("Only the most recent 64 events are kept"), Snapshot.Clicks.Num(), 64);
    TestEqual(TEXT("The oldest six events were dropped"), Snapshot.Clicks[0].AtMs, int64(6));
    TestEqual(TEXT("The newest event remains last"), Snapshot.Clicks.Last().AtMs, int64(69));
    TestEqual(TEXT("Overflow does not lose button state"), Snapshot.Buttons, 0);
    for (int32 Index = 1; Index < Snapshot.Clicks.Num(); ++Index)
    {
        TestEqual(TEXT("Retained events remain in order"), Snapshot.Clicks[Index].AtMs, Snapshot.Clicks[Index - 1].AtMs + 1);
    }
    TestTrue(TEXT("Overflow events still drain once"), Queue.Drain(90).Clicks.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPointerNormalizationTest, "Construct.ModScripts.Input.NormalizationYUp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructPointerNormalizationTest::RunTest(const FString& Parameters)
{
    FVector2D Out;
    TestTrue(TEXT("Viewport top left is accepted"), FConstructPointerQueue::Normalize(FVector2D::ZeroVector, FVector2D(800, 600), Out));
    TestTrue(TEXT("Top left maps to -1,+1"), Out.Equals(FVector2D(-1, 1)));
    TestTrue(TEXT("Viewport center is accepted"), FConstructPointerQueue::Normalize(FVector2D(400, 300), FVector2D(800, 600), Out));
    TestTrue(TEXT("Viewport center maps to zero"), Out.IsNearlyZero());
    TestTrue(TEXT("Viewport bottom right is accepted"), FConstructPointerQueue::Normalize(FVector2D(800, 600), FVector2D(800, 600), Out));
    TestTrue(TEXT("Bottom right maps to +1,-1"), Out.Equals(FVector2D(1, -1)));
    TestFalse(TEXT("A point outside the viewport is rejected"), FConstructPointerQueue::Normalize(FVector2D(801, 300), FVector2D(800, 600), Out));
    TestFalse(TEXT("An empty viewport is rejected"), FConstructPointerQueue::Normalize(FVector2D::ZeroVector, FVector2D(0, 600), Out));
    TestFalse(TEXT("A negative point is rejected"), FConstructPointerQueue::Normalize(FVector2D(-1, 300), FVector2D(800, 600), Out));
    TestTrue(TEXT("A local-player region is accepted"), FConstructPointerQueue::Normalize(FVector2D(600, 300), FVector2D(800, 600), Out, FVector2D(0.5, 0.0), FVector2D(0.5, 1.0)));
    TestTrue(TEXT("The local-player region has its own center"), Out.IsNearlyZero());
    TestFalse(TEXT("Another player's region is excluded"), FConstructPointerQueue::Normalize(FVector2D(200, 300), FVector2D(800, 600), Out, FVector2D(0.5, 0.0), FVector2D(0.5, 1.0)));
    TestFalse(TEXT("A region outside the viewport is rejected"), FConstructPointerQueue::Normalize(FVector2D(400, 300), FVector2D(800, 600), Out, FVector2D(0.75, 0.0), FVector2D(0.5, 1.0)));
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("Nonfinite coordinates are rejected"), FConstructPointerQueue::Normalize(FVector2D(NaN, 0), FVector2D(800, 600), Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPointerResetTest, "Construct.ModScripts.Input.ResetAndStaleRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructPointerResetTest::RunTest(const FString& Parameters)
{
    FConstructPointerQueue Queue;
    Queue.Record(0, false, 90, FVector2D::ZeroVector);
    TestTrue(TEXT("An unmatched release produces nothing"), Queue.Drain(90).Clicks.IsEmpty());
    Queue.Record(0, true, 100, FVector2D::ZeroVector);
    Queue.Record(1, true, 100, FVector2D::ZeroVector);
    Queue.Record(2, true, 100, FVector2D::ZeroVector);
    Queue.Reset();
    Queue.Record(0, false, 120, FVector2D::ZeroVector);
    Queue.Record(1, false, 120, FVector2D::ZeroVector);
    Queue.Record(2, false, 120, FVector2D::ZeroVector);
    const FConstructPointerSnapshot Cleared = Queue.Drain(150);
    TestTrue(TEXT("Reset clears queued downs and stale releases"), Cleared.Clicks.IsEmpty());
    TestEqual(TEXT("Reset clears every held bit"), Cleared.Buttons, 0);
    TestEqual(TEXT("Reset clears left held duration"), Cleared.LeftHeldMs, int64(0));
    TestEqual(TEXT("Reset clears middle held duration"), Cleared.MiddleHeldMs, int64(0));
    TestEqual(TEXT("Reset clears right held duration"), Cleared.RightHeldMs, int64(0));
    Queue.Record(0, true, 200, FVector2D::ZeroVector);
    TestEqual(TEXT("A fresh down after reset is accepted"), Queue.Drain(210).Clicks.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPointerInvalidReleaseTest, "Construct.ModScripts.Input.InvalidReleaseAndClockClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructPointerInvalidReleaseTest::RunTest(const FString& Parameters)
{
    FConstructPointerQueue Queue;
    Queue.Record(0, true, 100, FVector2D(2, 0));
    Queue.Record(-1, true, 100, FVector2D::ZeroVector);
    Queue.Record(3, true, 100, FVector2D::ZeroVector);
    TestTrue(TEXT("Invalid downs never enter the queue"), Queue.Drain(110).Clicks.IsEmpty());
    TestEqual(TEXT("Invalid downs do not hold a button"), Queue.Drain(110).Buttons, 0);
    Queue.Record(0, true, 200, FVector2D::ZeroVector);
    TestEqual(TEXT("Held time clamps a backwards clock"), Queue.Drain(190).LeftHeldMs, int64(0));
    Queue.Record(0, false, 190, FVector2D::ZeroVector);
    const FConstructPointerSnapshot Backwards = Queue.Drain(210);
    TestEqual(TEXT("Release duration clamps a backwards clock"), Backwards.Clicks[0].HeldMs, int64(0));
    Queue.Record(0, true, 300, FVector2D::ZeroVector);
    Queue.Drain(310);
    Queue.Record(0, false, 320, FVector2D(2, 0));
    const FConstructPointerSnapshot Outside = Queue.Drain(330);
    TestEqual(TEXT("Release outside the viewport clears held state"), Outside.Buttons, 0);
    TestTrue(TEXT("Release outside the viewport emits no invalid click"), Outside.Clicks.IsEmpty());
    Queue.Record(0, false, 340, FVector2D::ZeroVector);
    TestTrue(TEXT("A later in-bounds up cannot resurrect the outside release"), Queue.Drain(350).Clicks.IsEmpty());
    return true;
}

#endif
