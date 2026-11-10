#include "ConstructChatComponent.h"
#include "Components/InputComponent.h"
#include "ConstructChatSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructUIInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

namespace ConstructChatTests
{
UWorld* CreateIsolatedWorld(EWorldType::Type Type = EWorldType::EditorPreview)
{
	UWorld::InitializationValues Init;
	Init.InitializeScenes(true)
	    .AllowAudioPlayback(false)
	    .CreatePhysicsScene(false)
	    .CreateNavigation(false)
	    .CreateAISystem(false)
	    .ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(Type, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (World)
	{
		GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}
	return World;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatInstallerTest,
                                 "Construct.Chat.LocalControllerInstallation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatInstallerTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine is initialized"), GEngine))
	{
		return false;
	}
	UWorld* World = ConstructChatTests::CreateIsolatedWorld();
	if (!TestNotNull(TEXT("Isolated world exists"), World))
	{
		return false;
	}
	TStrongObjectPtr<ULocalPlayer> Local(NewObject<ULocalPlayer>(GEngine));
	Local->PlayerAdded(nullptr, int32(0));
	UConstructChatSubsystem* Installer = NewObject<UConstructChatSubsystem>(World);
	ON_SCOPE_EXIT
	{
		Installer->Deinitialize();
		Local->PlayerRemoved();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	TestFalse(TEXT("Installer excludes editor preview worlds"), Installer->ShouldCreateSubsystem(World));
	Installer->WatchPlayer(Local.Get());
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	APlayerController* Remote = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("First controller exists"), Controller) ||
	    !TestNotNull(TEXT("Second controller exists"), Remote))
	{
		return false;
	}
	Installer->ControllerChanged(Remote, Local.Get());
	TestNull(TEXT("Controller without a local player gets no component"),
	         Remote->FindComponentByClass<UConstructChatComponent>());
	Controller->SetPlayer(Local.Get());
	UConstructChatComponent* First = Controller->FindComponentByClass<UConstructChatComponent>();
	TestNotNull(TEXT("Real local-player controller assignment installs chat"), First);
	Installer->ControllerChanged(Controller, Local.Get());
	TArray<UConstructChatComponent*> Components;
	Controller->GetComponents(Components);
	TestEqual(TEXT("Repeated controller observation does not duplicate chat"), Components.Num(), 1);
	TestTrue(TEXT("The same component is preserved"),
	         Controller->FindComponentByClass<UConstructChatComponent>() == First);
	Installer->ForgetPlayer(Local.Get());
	APlayerController* Replacement = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Replacement controller exists"), Replacement))
	{
		return false;
	}
	Replacement->SetPlayer(Local.Get());
	TestNull(TEXT("Removing the player removes its controller observer"),
	         Replacement->FindComponentByClass<UConstructChatComponent>());
	Installer->WatchPlayer(Local.Get());
	TestNotNull(TEXT("Watching an existing local controller installs it"),
	            Replacement->FindComponentByClass<UConstructChatComponent>());
	TestNull(TEXT("Fixture created no SDK GameInstance"), World->GetGameInstance());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatLifecycleTest,
                                 "Construct.Chat.EndPlayReleasesComposer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatLifecycleTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine is initialized"), GEngine))
	{
		return false;
	}
	UWorld* World = ConstructChatTests::CreateIsolatedWorld();
	if (!TestNotNull(TEXT("Isolated world exists"), World))
	{
		return false;
	}
	TStrongObjectPtr<ULocalPlayer> Local(NewObject<ULocalPlayer>(GEngine));
	Local->PlayerAdded(nullptr, int32(0));
	ON_SCOPE_EXIT
	{
		Local->PlayerRemoved();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Controller exists"), Controller))
	{
		return false;
	}
	Controller->SetPlayer(Local.Get());
	UConstructChatComponent* Chat = NewObject<UConstructChatComponent>(Controller);
	Controller->AddInstanceComponent(Chat);
	Chat->RegisterComponent();
	Controller->DispatchBeginPlay();
	if (!TestTrue(TEXT("Component entered the real Engine lifecycle"), Chat->HasBegunPlay()))
	{
		return false;
	}
	UConstructUIInputSubsystem* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
	if (!TestNotNull(TEXT("Actual local input gate exists"), Gate))
	{
		return false;
	}
	Gate->SetSessionReady(true);
	Chat->InputGate = Gate;
	Chat->bComposerOpen = true;
	Gate->SetModal(TEXT("ConstructChat"), true);
	Gate->SetModal(TEXT("OtherUI"), true);
	Chat->History.Add(TEXT("Private history"));
	Chat->ActiveLookup = NewObject<UConstructChatLookup>(Chat);
	Chat->ActiveLookup->Initialize(Chat);
	Chat->bLoading = true;
	Chat->DestroyComponent();
	TestFalse(TEXT("Destroy routes the actual component EndPlay"), Chat->HasBegunPlay());
	TestFalse(TEXT("Composer closes during EndPlay"), Chat->IsComposerOpen());
	TestFalse(TEXT("The other UI still blocks input"), Gate->IsGameplayInputAllowed());
	Gate->SetModal(TEXT("OtherUI"), false);
	TestTrue(TEXT("Chat released only its own modal"), Gate->IsGameplayInputAllowed());
	TestTrue(TEXT("EndPlay clears history"), Chat->History.IsEmpty());
	TestTrue(TEXT("EndPlay invalidates pending membership replies"), Chat->ActiveLookup == nullptr);
	TestFalse(TEXT("EndPlay clears pending load state"), Chat->bLoading);
	TestNull(TEXT("Fixture created no SDK GameInstance"), World->GetGameInstance());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatControllerReplacementTest,
                                 "Construct.Chat.ControllerReplacementCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatControllerReplacementTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine is initialized"), GEngine))
	{
		return false;
	}
	UWorld* World = ConstructChatTests::CreateIsolatedWorld(EWorldType::Game);
	if (!TestNotNull(TEXT("Isolated runtime world exists"), World))
	{
		return false;
	}
	TStrongObjectPtr<ULocalPlayer> Local(NewObject<ULocalPlayer>(GEngine));
	Local->PlayerAdded(nullptr, int32(0));
	UConstructChatSubsystem* Installer = World->GetSubsystem<UConstructChatSubsystem>();
	ON_SCOPE_EXIT
	{
		if (Installer)
		{
			Installer->Deinitialize();
		}
		Local->PlayerRemoved();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	if (!TestNotNull(TEXT("The real runtime subsystem exists"), Installer))
	{
		return false;
	}
	Installer->WatchPlayer(Local.Get());
	APlayerController* FirstController = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("The first actual controller exists"), FirstController))
	{
		return false;
	}
	FirstController->SetPlayer(Local.Get());
	FirstController->DispatchBeginPlay();
	UConstructChatComponent* First = FirstController->FindComponentByClass<UConstructChatComponent>();
	if (!TestNotNull(TEXT("First controller owns installed chat"), First) ||
	    !TestTrue(TEXT("Chat activated through the real runtime BeginPlay"), First->bRuntimeActive))
	{
		return false;
	}
	UConstructUIInputSubsystem* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
	if (!TestNotNull(TEXT("The actual local input gate exists"), Gate))
	{
		return false;
	}
	Gate->SetSessionReady(true);
	Gate->SetModal(TEXT("OtherUI"), true);
	First->bComposerOpen = true;
	Gate->SetModal(TEXT("ConstructChat"), true);
	First->Append(TEXT("Controller-private history"));
	TStrongObjectPtr<UConstructChatLookup> OldLookup(NewObject<UConstructChatLookup>(First));
	OldLookup->Initialize(First);
	First->ActiveLookup = OldLookup.Get();
	First->bLoading = true;
	UInputComponent* FirstInput = First->ChatInput.Get();
	if (!TestNotNull(TEXT("Chat installed a key-binding component"), FirstInput))
	{
		return false;
	}
	TestTrue(TEXT("The old controller owns the exact input component"), FirstInput->GetOuter() == FirstController);
	for (const FKey& Key : {EKeys::Enter, EKeys::F3})
	{
		TestTrue(*FString::Printf(TEXT("Chat bound %s to a fresh press"), *Key.ToString()),
		         FirstInput->KeyBindings.ContainsByPredicate([&Key](const FInputKeyBinding& Binding)
		         {
			         return Binding.Chord.Key == Key && Binding.KeyEvent == IE_Pressed;
		         }));
	}
	FirstController->ReceivedPlayer();
	TestEqual(TEXT("Repeated observation preserves current history"), First->History.Num(), 1);

	APlayerController* Replacement = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("The replacement actual controller exists"), Replacement))
	{
		return false;
	}
	Replacement->SetPlayer(Local.Get());
	Replacement->DispatchBeginPlay();
	UConstructChatComponent* Next = Replacement->FindComponentByClass<UConstructChatComponent>();
	if (!TestNotNull(TEXT("Replacement receives its own chat component"), Next))
	{
		return false;
	}
	TestFalse(TEXT("Old component has released runtime ownership"), First->bRuntimeActive);
	TestFalse(TEXT("Old component has stopped setup ticking"), First->IsComponentTickEnabled());
	TestFalse(TEXT("Old component has closed its composer"), First->IsComposerOpen());
	TestTrue(TEXT("Old component has cleared visible private history"), First->GetHistory().IsEmpty());
	TestTrue(TEXT("Old component has invalidated membership replies"), First->ActiveLookup == nullptr);
	TestTrue(TEXT("Old component has released its input component"), First->ChatInput == nullptr);
	TestFalse(TEXT("Controller replacement unregisters the old key bindings"), FirstInput->IsRegistered());
	TestTrue(TEXT("The replacement owns its own key bindings"),
	         Next->ChatInput && Next->ChatInput->GetOuter() == Replacement && Next->ChatInput != FirstInput);
	TestTrue(TEXT("Old component has released its panel"), First->Panel == nullptr);
	TestTrue(TEXT("Old component has detached its input gate"), !First->InputGate.IsValid());
	TestTrue(TEXT("Authored component remains reusable rather than being destroyed"), IsValid(First));
	TestFalse(TEXT("Unrelated UI remains modal"), Gate->IsGameplayInputAllowed());
	Gate->SetModal(TEXT("OtherUI"), false);
	TestTrue(TEXT("The departing chat released only its own modal"), Gate->IsGameplayInputAllowed());
	OldLookup->Failed(FCrowdyChannelError(), TEXT("Late controller callback"));
	TestTrue(TEXT("A late reply cannot restore old history or request state"),
	         First->GetHistory().IsEmpty() && First->ActiveLookup == nullptr && !First->bLoading);

	Installer->ForgetPlayer(Local.Get());
	TestFalse(TEXT("Removing the player shuts down its current chat"), Next->bRuntimeActive);
	TestTrue(TEXT("Removing the player releases current input"), Next->ChatInput == nullptr);
	Installer->WatchPlayer(Local.Get());
	TestEqual(TEXT("Rewatching preserves the authored component"),
	          Replacement->FindComponentByClass<UConstructChatComponent>(),
	          Next);
	TestTrue(TEXT("A retained component can initialize again for its actual local controller"), Next->bRuntimeActive);
	Installer->Deinitialize();
	TestFalse(TEXT("Subsystem teardown shuts down retained components"), Next->bRuntimeActive);
	Next->ShutdownLocalRuntime();
	TestFalse(TEXT("Repeated shutdown stays inactive"), Next->bRuntimeActive);
	TestNull(TEXT("The fixture creates no SDK GameInstance or session"), World->GetGameInstance());
	return true;
}
#endif
