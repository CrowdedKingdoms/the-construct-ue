#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructModScriptPresentation.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr auto PresentationTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptHudTest, "Construct.ModScripts.Presentation.HudBoundsAndPlainText", PresentationTestFlags)
bool FConstructScriptHudTest::RunTest(const FString&)
{
    FConstructModScriptPresentation View;
    const FConstructScriptLabel Label{TEXT("one"), TEXT("Example"), 900000000000000001LL, 1};
    View.ShowHud(Label, TEXT("<b>Literal text</b>\r\n2\n3\n4\n5\n6\n7\n8\n9"));
    TestEqual(TEXT("Plain text retained and only eight lines shown"), View.Find(TEXT("one"))->Text, FString(TEXT("<b>Literal text</b>\n2\n3\n4\n5\n6\n7\n8")));
    View.ShowHud(Label, FString::ChrN(1000, 'x'));
    TestEqual(TEXT("HUD text bounded"), View.Find(TEXT("one"))->Text.Len(), 500);
    View.ShowHud(Label, FString());
    TestTrue(TEXT("Empty text removes the card body"), View.Find(TEXT("one"))->Text.IsEmpty());
    TestTrue(TEXT("Empty HUD does not stop its source"), View.Find(TEXT("one"))->bRunning);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptGenerationTest, "Construct.ModScripts.Presentation.StoppedRunsCannotResurrect", PresentationTestFlags)
bool FConstructScriptGenerationTest::RunTest(const FString&)
{
    FConstructModScriptPresentation View;
    View.ShowHud({TEXT("one"), TEXT("Old"), 41, 1}, TEXT("Old text"));
    View.Stopped(TEXT("one"), 1, FText::FromString(TEXT("Revoked")));
    View.ShowHud({TEXT("one"), TEXT("Old"), 41, 1}, TEXT("Late old text"));
    TestFalse(TEXT("Stopped generation stays stopped"), View.Find(TEXT("one"))->bRunning);
    TestTrue(TEXT("Late old HUD remains absent"), View.Find(TEXT("one"))->Text.IsEmpty());
    View.ShowHud({TEXT("one"), TEXT("New"), 41, 2}, TEXT("New text"));
    View.Stopped(TEXT("one"), 1, FText::FromString(TEXT("Old stop")));
    View.ClearHud(TEXT("one"), 1);
    TestTrue(TEXT("Old stop cannot stop new run"), View.Find(TEXT("one"))->bRunning);
    TestEqual(TEXT("Old clear cannot clear new run"), View.Find(TEXT("one"))->Text, FString(TEXT("New text")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptRequestTest, "Construct.ModScripts.Presentation.RequestsWaitForSource", PresentationTestFlags)
bool FConstructScriptRequestTest::RunTest(const FString&)
{
    FConstructModScriptPresentation View;
    int32 Requests = 0;
    View.OnRevokeRequested.AddLambda([&Requests](const FString&) { ++Requests; });
    View.Started({TEXT("one"), TEXT("Example"), 41, 1});
    TestTrue(TEXT("Revoke emitted"), View.Revoke(TEXT("one")));
    TestFalse(TEXT("Double revoke suppressed"), View.Revoke(TEXT("one")));
    TestEqual(TEXT("Only one request"), Requests, 1);
    TestTrue(TEXT("Request does not fabricate a stopped reply"), View.Find(TEXT("one"))->bRunning);
    View.Error(TEXT("one"), FText::FromString(TEXT("Request failed")));
    TestFalse(TEXT("Error releases pending state"), View.Find(TEXT("one"))->bPending);
    View.Stopped(TEXT("one"), 1, FText::FromString(TEXT("Revoked")));
    TestFalse(TEXT("Source stop applies"), View.Find(TEXT("one"))->bRunning);
    TestEqual(TEXT("Stop reason retained"), View.Find(TEXT("one"))->StopReason.ToString(), FString(TEXT("Revoked")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptConsentTest, "Construct.ModScripts.Presentation.ConsentOrderingAndWithdrawal", PresentationTestFlags)
bool FConstructScriptConsentTest::RunTest(const FString&)
{
    FConstructModScriptPresentation View;
    FConstructScriptConsent First;
    First.RequestId = 11;
    FConstructScriptConsent Second;
    Second.RequestId = 12;
    View.ShowPrompt(First);
    View.ShowPrompt(Second);
    int32 SeenId = 0;
    bool bSeenRun = true;
    View.OnAnswerRequested.AddLambda([&](int32 Id, bool bRun) { SeenId = Id; bSeenRun = bRun; });
    TestFalse(TEXT("Cannot answer a queued prompt"), View.Answer(12, true));
    TestTrue(TEXT("Can decline current prompt"), View.Answer(11, false));
    TestEqual(TEXT("Answer names actual request"), SeenId, 11);
    TestFalse(TEXT("Decline preserved"), bSeenRun);
    TestFalse(TEXT("Double answer suppressed"), View.Answer(11, true));
    TestTrue(TEXT("Answer alone starts no script"), View.GetCards().IsEmpty());
    View.WithdrawPrompt(11);
    TestEqual(TEXT("Withdrawal advances prompt queue"), View.GetPrompt()->RequestId, 12);
    TestFalse(TEXT("Stale button cannot answer a different prompt"), View.Answer(11, true));
    View.WithdrawPrompt(12);
    TestNull(TEXT("All prompts removed"), View.GetPrompt());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptAuthorTest, "Construct.ModScripts.Presentation.AuthorIdentityAndPendingGroup", PresentationTestFlags)
bool FConstructScriptAuthorTest::RunTest(const FString&)
{
    FConstructModScriptPresentation View;
    constexpr int64 Author = 900000000000000001LL;
    View.Started({TEXT("one"), TEXT("One"), Author, 1});
    View.Started({TEXT("two"), TEXT("Two"), Author, 1});
    View.Started({TEXT("other"), TEXT("Other"), 41, 1});
    int64 Requested = 0;
    View.OnForgetRequested.AddLambda([&](int64 Id) { Requested = Id; });
    TestTrue(TEXT("Forget author requested"), View.ForgetAuthor(Author));
    TestEqual(TEXT("Large identity retained exactly"), Requested, Author);
    TestTrue(TEXT("All author's rows pending"), View.Find(TEXT("one"))->bPending && View.Find(TEXT("two"))->bPending);
    TestFalse(TEXT("Other author untouched"), View.Find(TEXT("other"))->bPending);
    TestFalse(TEXT("Duplicate group request suppressed"), View.ForgetAuthor(Author));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptUnrelatedErrorTest, "Construct.ModScripts.Presentation.UnrelatedErrorCannotResubmitConsent", PresentationTestFlags)
bool FConstructScriptUnrelatedErrorTest::RunTest(const FString&)
{
    FConstructModScriptPresentation View;
    FConstructScriptConsent Prompt;
    Prompt.RequestId = 11;
    View.ShowPrompt(Prompt);
    int32 Requests = 0;
    View.OnAnswerRequested.AddLambda([&](int32, bool) { ++Requests; });
    View.Answer(11, true);
    View.Error(TEXT("another-script"), FText::FromString(TEXT("A different request failed")));
    TestFalse(TEXT("An unrelated error cannot unlock a second consent submission"), View.Answer(11, true));
    TestEqual(TEXT("Exactly one consent request"), Requests, 1);
    View.WithdrawPrompt(11);
    TestNull(TEXT("The source withdraws the pending prompt"), View.GetPrompt());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptLimitsTest, "Construct.ModScripts.Presentation.BoundedRowsAndReset", PresentationTestFlags)
bool FConstructScriptLimitsTest::RunTest(const FString&)
{
    FConstructModScriptPresentation View;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        TestTrue(TEXT("Up to eight running scripts"), View.Started({FString::FromInt(Index), TEXT("Example"), 41, 1}));
    }
    TestFalse(TEXT("Ninth running script refused"), View.Started({TEXT("nine"), TEXT("Example"), 41, 1}));
    for (int32 Index = 0; Index < 8; ++Index)
    {
        View.Stopped(FString::FromInt(Index), 1, FText::FromString(TEXT("Left land")));
    }
    for (int32 Index = 8; Index < 100; ++Index)
    {
        const FString Id = FString::FromInt(Index);
        View.Started({Id, TEXT("Example"), 41, 1});
        View.Stopped(Id, 1, FText::FromString(TEXT("Stopped")));
    }
    TestEqual(TEXT("Retained history is bounded"), View.GetCards().Num(), 32);
    View.Reset();
    TestTrue(TEXT("Reset releases display state"), View.GetCards().IsEmpty());
    return true;
}

#endif
