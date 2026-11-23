#include "ConstructChatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructPresencePawn.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Replication/Subsystems/CrowdyEntitySubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatProximityPresentationTest,
                                 "Construct.Chat.ProximityPresenceNamesAndBounds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatProximityPresentationTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine is initialized"), GEngine))
	{
		return false;
	}
	UWorld::InitializationValues Init;
	Init.InitializeScenes(true)
	    .AllowAudioPlayback(false)
	    .CreatePhysicsScene(false)
	    .CreateNavigation(false)
	    .CreateAISystem(false)
	    .ShouldSimulatePhysics(false);
	UWorld* World =
	    UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("An isolated runtime world exists"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	World->InitializeActorsForPlay(FURL());
	UCrowdyEntitySubsystem* Entities = World->GetSubsystem<UCrowdyEntitySubsystem>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AConstructPresencePawn* Pawn = World->SpawnActor<AConstructPresencePawn>();
	if (!TestNotNull(TEXT("The real entity registry exists"), Entities) ||
	    !TestNotNull(TEXT("The real controller exists"), Controller) ||
	    !TestNotNull(TEXT("The real presence pawn exists"), Pawn))
	{
		return false;
	}
	UConstructChatComponent* Chat = NewObject<UConstructChatComponent>(Controller);
	FStrProperty* NameProperty = FindFProperty<FStrProperty>(Pawn->GetClass(), TEXT("PlayerName"));
	if (!TestNotNull(TEXT("The presence view-state name field exists"), NameProperty))
	{
		return false;
	}
	// Host enrollment derives a class-path identity without a local-player or authentication id.
	const FGuid Sender = Entities->RegisterParticipant(Pawn, ECrowdyOwnership::Host);
	if (!TestTrue(TEXT("Documented Host participant enrollment returns a usable identity"), Sender.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Public entity lookup returns the actual presence pawn"),
	          Entities->FindEntity(Sender),
	          static_cast<AActor*>(Pawn));
	NameProperty->SetPropertyValue_InContainer(Pawn, TEXT("Nanian\nInjected second name"));
	TestEqual(TEXT("The public presence getter reads the raw view state"),
	          Pawn->GetPlayerName(),
	          FString(TEXT("Nanian\nInjected second name")));

	FCrowdyProximityText Message;
	Message.SenderEntityId = Sender;
	Message.SenderUuid = TEXT("Client-asserted actor id");
	Message.Text = TEXT("<literal> first line<br>Injected second line");
	Chat->AppendProximityMessage(Message);
	TestEqual(TEXT("Actual SDK message data uses the registered presence name and literal bounded first line"),
	          Chat->History.Last(),
	          FString(TEXT("Nanian: <literal> first line")));

	NameProperty->SetPropertyValue_InContainer(Pawn, FString::ChrN(80, TEXT('n')) + TEXT("\nextra"));
	Message.Text = FString::ChrN(300, TEXT('x'));
	Chat->AppendProximityMessage(Message);
	TestEqual(TEXT("Untrusted name and text bounds are independently enforced"),
	          Chat->History.Last(),
	          FString::ChrN(64, TEXT('n')) + TEXT(": ") + FString::ChrN(240, TEXT('x')));

	NameProperty->SetPropertyValue_InContainer(Pawn, TEXT("  "));
	Message.Text = TEXT("empty-name fallback");
	Chat->AppendProximityMessage(Message);
	TestEqual(TEXT("An empty presence name falls back to Player"),
	          Chat->History.Last(),
	          FString(TEXT("Player: empty-name fallback")));

	Entities->UnregisterParticipant(Pawn);
	Message.Text = TEXT("no cached name");
	Chat->AppendProximityMessage(Message);
	TestEqual(TEXT("A departed sender immediately falls back without a cached name"),
	          Chat->History.Last(),
	          FString(TEXT("Player: no cached name")));

	const FString SplitPair = FString::ChrN(239, TEXT('s')) + FString::Chr(static_cast<TCHAR>(0xD83D)) +
	                          FString::Chr(static_cast<TCHAR>(0xDE00));
	TestEqual(TEXT("The receive limit never leaves a split UTF-16 surrogate"),
	          UConstructChatComponent::SanitizeIncoming(SplitPair).Len(),
	          239);

	const int32 BeforeEmpty = Chat->History.Num();
	Message.Text = TEXT("\nOnly a second line");
	Chat->AppendProximityMessage(Message);
	TestEqual(TEXT("An empty first line adds no history row"), Chat->History.Num(), BeforeEmpty);

	Chat->Status = TEXT("Existing status");
	Chat->ApplyProximitySendResult(ECrowdyProximityChatResult::EmptyText, FString());
	TestEqual(TEXT("EmptyText leaves status unchanged"), Chat->Status, FString(TEXT("Existing status")));
	TestEqual(TEXT("EmptyText adds no row"), Chat->History.Num(), BeforeEmpty);
	Chat->ApplyProximitySendResult(ECrowdyProximityChatResult::Sent, TEXT("submitted line"));
	TestEqual(TEXT("Sent presents the actual local text"), Chat->History.Last(), FString(TEXT("You: submitted line")));
	TestTrue(TEXT("Sent is not represented as a delivery receipt"), Chat->Status.Contains(TEXT("not acknowledged")));
	Chat->ApplyProximitySendResult(ECrowdyProximityChatResult::NoPosition, TEXT("unused"));
	TestEqual(TEXT("NoPosition shows the contract's connection status"),
	          Chat->Status,
	          FString(TEXT("Chat is not connected")));
	Chat->ApplyProximitySendResult(ECrowdyProximityChatResult::NotConnected, TEXT("unused"));
	TestEqual(
	    TEXT("NotConnected shows the same connection status"), Chat->Status, FString(TEXT("Chat is not connected")));
	Chat->ApplyProximitySendResult(ECrowdyProximityChatResult::TooLong, TEXT("unused"));
	TestEqual(TEXT("TooLong is visible"), Chat->Status, FString(TEXT("Chat message is too long.")));
	TestNull(TEXT("Presentation fixture creates no SDK GameInstance or authentication state"),
	         World->GetGameInstance());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatProximitySessionTest,
                                 "Construct.Chat.ProximitySignedOutDelegateIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatProximitySessionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UCrowdyProximityChat> Service(NewObject<UCrowdyProximityChat>(Instance.Get()));
	TStrongObjectPtr<UConstructChatComponent> Chat(NewObject<UConstructChatComponent>());
	Service->OnProximityTextReceived.AddUniqueDynamic(Chat.Get(), &UConstructChatComponent::ProximityReceived);
	Service->OnProximityTextRefused.AddUniqueDynamic(Chat.Get(), &UConstructChatComponent::ProximityRefused);
	Chat->bRuntimeActive = true;
	Chat->Status = TEXT("Signed out baseline");
	FCrowdyProximityText Message;
	Message.SenderEntityId = FGuid::NewGuid();
	Message.Text = TEXT("A delayed account-private line");
	Service->OnProximityTextReceived.Broadcast(Message);
	FCrowdySendRefusal Refusal;
	Service->OnProximityTextRefused.Broadcast(Refusal);
	TestTrue(TEXT("Actual public receive delegate cannot display text without a signed-in session"),
	         Chat->GetHistory().IsEmpty());
	TestEqual(TEXT("Actual public refusal delegate cannot change signed-out UI"),
	          Chat->Status,
	          FString(TEXT("Signed out baseline")));
	Chat->SendText(TEXT("A no-world request must not use transport"));
	TestTrue(TEXT("No-world sending adds no submitted row"), Chat->GetHistory().IsEmpty());
	TestEqual(
	    TEXT("No-world sending reports unavailable connection"), Chat->Status, FString(TEXT("Chat is not connected")));
	Service->OnProximityTextReceived.RemoveDynamic(Chat.Get(), &UConstructChatComponent::ProximityReceived);
	Service->OnProximityTextRefused.RemoveDynamic(Chat.Get(), &UConstructChatComponent::ProximityRefused);
	return true;
}
#endif
