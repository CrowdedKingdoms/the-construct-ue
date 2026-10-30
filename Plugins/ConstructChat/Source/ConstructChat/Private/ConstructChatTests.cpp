#include "ConstructChatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Containers/StringConv.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatCodecTest,
                                 "Construct.Chat.PayloadBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatCodecTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Bytes;
	FString Text;
	TestFalse(TEXT("Empty text refused"), UConstructChatComponent::Encode(TEXT(" "), Bytes));
	TestTrue(TEXT("Oversized outgoing text is bounded"),
	         UConstructChatComponent::Encode(FString::ChrN(241, TEXT('x')), Bytes));
	// Inspect the outgoing wire payload independently; the receiving decoder also bounds text.
	const FUTF8ToTCHAR WireCharacters(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	const FString Wire(WireCharacters.Length(), WireCharacters.Get());
	const FString Envelope(TEXT("construct.chat.v1\n"));
	TestTrue(TEXT("Outgoing wire data retains its envelope"), Wire.StartsWith(Envelope));
	TestEqual(TEXT("Outgoing wire text itself is bounded before any receiver decodes it"),
	          Wire.Mid(Envelope.Len()).Len(),
	          240);
	TestTrue(TEXT("Bounded payload decodes"), UConstructChatComponent::Decode(Bytes, Text));
	TestEqual(TEXT("Outgoing bound is 240 UTF-16 units"), Text.Len(), 240);
	TestTrue(TEXT("Unicode message accepted"),
	         UConstructChatComponent::Encode(TEXT("Hello \u00e6\u00f8\u00e5"), Bytes));
	TestTrue(TEXT("Own payload decodes"), UConstructChatComponent::Decode(Bytes, Text));
	TestEqual(TEXT("Unicode preserved"), Text, FString(TEXT("Hello \u00e6\u00f8\u00e5")));
	TestTrue(TEXT("Sending folds line breaks"),
	         UConstructChatComponent::Encode(TEXT(" first\r\nsecond<br>third\n "), Bytes));
	TestTrue(TEXT("Folded payload decodes"), UConstructChatComponent::Decode(Bytes, Text));
	TestEqual(TEXT("Sending remains single-line"), Text, FString(TEXT("first second third")));
	Bytes.Init(0, 2049);
	TestFalse(TEXT("Overlong incoming packet refused"), UConstructChatComponent::Decode(Bytes, Text));
	const ANSICHAR Forged[] = "construct.chat.v1\nfirst<br>forged\nsecond";
	Bytes.Reset();
	Bytes.Append(reinterpret_cast<const uint8*>(Forged), UE_ARRAY_COUNT(Forged) - 1);
	TestTrue(TEXT("A bounded hostile payload decodes"), UConstructChatComponent::Decode(Bytes, Text));
	TestEqual(TEXT("Receiving stops at the first break"), Text, FString(TEXT("first")));
	const FString LongIncoming = FString(TEXT("construct.chat.v1\n")) + FString::ChrN(500, TEXT('r'));
	FTCHARToUTF8 IncomingBytes(*LongIncoming);
	Bytes.Reset();
	Bytes.Append(reinterpret_cast<const uint8*>(IncomingBytes.Get()), IncomingBytes.Length());
	TestTrue(TEXT("A long but permitted-size incoming packet is sanitized"),
	         UConstructChatComponent::Decode(Bytes, Text));
	TestEqual(TEXT("Incoming text has its own independent bound"), Text.Len(), 240);
	Bytes[0] = static_cast<uint8>('!');
	TestFalse(TEXT("Unrelated channel bytes are ignored"), UConstructChatComponent::Decode(Bytes, Text));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatHistoryTest,
                                 "Construct.Chat.BoundedHistory",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatHistoryTest::RunTest(const FString& Parameters)
{
	UConstructChatComponent* Chat = NewObject<UConstructChatComponent>();
	for (int32 Index = 0; Index < 105; ++Index)
	{
		Chat->Append(FString::Printf(TEXT("Line %d"), Index));
	}
	TestEqual(TEXT("History contains the latest 100 rows"), Chat->History.Num(), 100);
	TestEqual(TEXT("Oldest rows were removed"), Chat->History[0], FString(TEXT("Line 5")));
	TestTrue(TEXT("Cached visible history contains the latest row"),
	         Chat->GetHistory().ToString().EndsWith(TEXT("Line 104")));
	Chat->Append(FString::ChrN(1000, TEXT('z')));
	TestEqual(TEXT("A displayed line is bounded"), Chat->History.Last().Len(), 320);
	Chat->ResetSession();
	TestTrue(TEXT("Session reset clears the actual visible history"), Chat->GetHistory().IsEmpty());
	TestTrue(TEXT("No-world chat cannot send"), !Chat->CanSend());
	Chat->OpenComposer();
	TestFalse(TEXT("No-world chat cannot intercept input"), Chat->IsComposerOpen());
	return true;
}
#endif
