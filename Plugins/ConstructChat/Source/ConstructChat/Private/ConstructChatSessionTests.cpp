#include "ConstructChatComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatSessionIsolationTest,
                                 "Construct.Chat.SessionIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatSessionIsolationTest::RunTest(const FString& Parameters)
{
	UConstructChatComponent* Chat = NewObject<UConstructChatComponent>();
	UConstructChatLookup* Old = NewObject<UConstructChatLookup>(Chat);
	Old->Initialize(Chat);
	Chat->ActiveLookup = Old;
	Chat->History.Add(TEXT("Previous account"));
	Chat->ResetSession();
	TestTrue(TEXT("Session change clears history"), Chat->History.IsEmpty());
	UConstructChatLookup* Current = NewObject<UConstructChatLookup>(Chat);
	Current->Initialize(Chat);
	Chat->ActiveLookup = Current;
	Chat->bLoading = true;
	Chat->Status = TEXT("Current request");
	Old->Failed(FCrowdyChannelError(), TEXT("Old account error"));
	TestTrue(TEXT("Old error cannot finish current query"), Chat->bLoading);
	TestEqual(TEXT("Old error cannot replace status"), Chat->Status, FString(TEXT("Current request")));
	Old->Succeeded(TArray<FCrowdyChannelMembership>());
	TestTrue(TEXT("Old membership cannot finish current query"), Chat->bLoading);
	Current->Failed(FCrowdyChannelError(), TEXT("Current error"));
	TestFalse(TEXT("Current error finishes current query"), Chat->bLoading);
	TestEqual(TEXT("Current error is normalized"),
	          Chat->Status,
	          FString(TEXT("Could not load your chat channel. Try Refresh.")));
	UConstructChatLookup* BeforeLogout = NewObject<UConstructChatLookup>(Chat);
	BeforeLogout->Initialize(Chat);
	Chat->ActiveLookup = BeforeLogout;
	Chat->bLoading = true;
	Chat->History.Add(TEXT("Account-private history"));
	Chat->SignedOut(true);
	BeforeLogout->Failed(FCrowdyChannelError(), TEXT("Late private backend error"));
	TestTrue(TEXT("Logout invalidates pending requests"), Chat->ActiveLookup == nullptr);
	TestFalse(TEXT("Logout clears loading state"), Chat->bLoading);
	TestTrue(TEXT("Late callback cannot restore a prior account's history"), Chat->History.IsEmpty());
	TestEqual(TEXT("Late callback cannot overwrite the logout status"),
	          Chat->Status,
	          FString(TEXT("Signed out. Chat history cleared.")));
	return true;
}
#endif
