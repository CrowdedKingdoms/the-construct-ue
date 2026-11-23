#include "ConstructVoiceSubsystem.h"
#include "ConstructVoiceActivationTestReceiver.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

void UConstructVoiceActivationTestReceiver::ReplaceController(UActorComponent* Component, bool bReset)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!bInvoked && IsValid(Replacement) && IsValid(Player))
	{
		bInvoked = true;
		Replacement->SetPlayer(Player);
	}
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructDeveloperSettings.h"
#include "ConstructVoiceComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

struct FConstructVoiceInstallationTestAccess
{
	static bool HasInstanceObservers(const UConstructVoiceSubsystem& Subsystem)
	{
		return Subsystem.ObservedInstance.IsValid() || Subsystem.LocalPlayerAddedHandle.IsValid() ||
		       Subsystem.LocalPlayerRemovedHandle.IsValid();
	}

	static int32 PlayerObserverCount(const UConstructVoiceSubsystem& Subsystem)
	{
		return Subsystem.ControllerChangedHandles.Num();
	}

	static void Install(UConstructVoiceSubsystem& Subsystem, APlayerController* Controller, ULocalPlayer* Player)
	{
		Subsystem.InstallForController(Controller, Player);
	}

	static UConstructVoiceComponent* TrackedComponent(const UConstructVoiceSubsystem& Subsystem, ULocalPlayer* Player)
	{
		return Subsystem.ActiveComponents.FindRef(Player).Get();
	}
};

struct FConstructVoiceRuntimeTestAccess
{
	static void Begin(UConstructVoiceComponent& Component)
	{
		Component.BeginPlay();
	}
	static bool HasNoRuntimeResources(const UConstructVoiceComponent& Component)
	{
		return !Component.Panel && !Component.TalkInput && !Component.TalkAction && !Component.TalkContext &&
		       !Component.InputGate.IsValid() && !Component.ActivationHandle.IsValid() &&
		       !Component.bContextInstalled && !Component.bCaptureRequested && !Component.bVoiceServiceActivated &&
		       !Component.TalkKeyState.IsDown() && !Component.RuntimeInstance.IsValid();
	}
};

namespace ConstructVoiceInstallationTests
{
struct FSettingsScope
{
	const bool bPrevious = GetDefault<UConstructDeveloperSettings>()->bEnableVoice;

	explicit FSettingsScope(bool bEnabled)
	{
		GetMutableDefault<UConstructDeveloperSettings>()->bEnableVoice = bEnabled;
	}

	~FSettingsScope()
	{
		GetMutableDefault<UConstructDeveloperSettings>()->bEnableVoice = bPrevious;
	}
};

struct FRuntimeWorld
{
	UWorld* World = nullptr;
	UConstructVoiceSubsystem* Installer = nullptr;
	TStrongObjectPtr<UGameInstance> Instance;
	TStrongObjectPtr<ULocalPlayer> Player;
	APlayerController* Controller = nullptr;

	~FRuntimeWorld()
	{
		if (Instance.IsValid() && Player.IsValid() && Instance->GetLocalPlayers().Contains(Player.Get()))
		{
			Instance->RemoveLocalPlayer(Player.Get());
		}
		if (World)
		{
			World->SetGameInstance(nullptr);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		if (!Test.TestNotNull(TEXT("Runtime installer fixture needs the initialized Engine"), GEngine))
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
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Actual isolated game world exists"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		// The GameInstance supplies real player-list events without initializing SDK services or session state.
		Instance.Reset(NewObject<UGameInstance>(GEngine));
		World->SetGameInstance(Instance.Get());
		World->InitializeActorsForPlay(FURL());
		Installer = World->GetSubsystem<UConstructVoiceSubsystem>();
		if (!Test.TestNotNull(TEXT("Runtime subsystem was created through the world collection"), Installer))
		{
			return false;
		}
		Installer->OnWorldBeginPlay(*World);
		return true;
	}

	bool AddPlayer(FAutomationTestBase& Test)
	{
		Player.Reset(NewObject<ULocalPlayer>(GEngine));
		Instance->AddLocalPlayer(Player.Get(), FPlatformUserId::CreateFromInternalId(0));
		Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Actual runtime player controller exists"), Controller))
		{
			return false;
		}
		Controller->SetPlayer(Player.Get());
		return Test.TestTrue(TEXT("Fixture controller is an actual local controller"), Controller->IsLocalController());
	}

	int32 VoiceCount(APlayerController* Target) const
	{
		TInlineComponentArray<UConstructVoiceComponent*> Components;
		if (Target)
		{
			Target->GetComponents(Components);
		}
		return Components.Num();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceSettingsDefaultsTest,
                                 "Construct.Voice.Installation.SettingsDefaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FConstructVoiceSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	const UConstructDeveloperSettings* Settings = GetDefault<UConstructDeveloperSettings>();
	TestFalse(TEXT("A fresh project does not opt in to Construct voice"), Settings->bEnableVoice);
	TestEqual(TEXT("Settings live in the Project container"), Settings->GetContainerName(), FName(TEXT("Project")));
	TestEqual(TEXT("Settings live under Plugins"), Settings->GetCategoryName(), FName(TEXT("Plugins")));
	TestEqual(TEXT("Settings section is Crowdy Construct"), Settings->GetSectionName(), FName(TEXT("CrowdyConstruct")));
	TestEqual(TEXT("Construct settings use their own Game config section"),
	          Settings->GetClass()->ClassConfigName,
	          FName(TEXT("Game")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceDisabledInstallationTest,
                                 "Construct.Voice.Installation.DisabledSnapshot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FConstructVoiceDisabledInstallationTest::RunTest(const FString& Parameters)
{
	using namespace ConstructVoiceInstallationTests;
	FSettingsScope Settings(false);
	FRuntimeWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	GetMutableDefault<UConstructDeveloperSettings>()->bEnableVoice = true;
	TestFalse(TEXT("Existing world's disabled policy is immutable"), Fixture.Installer->IsVoiceEnabled());
	if (!Fixture.AddPlayer(*this))
	{
		return false;
	}
	FConstructVoiceInstallationTestAccess::Install(*Fixture.Installer, Fixture.Controller, Fixture.Player.Get());
	TestEqual(TEXT("Disabled automatic and direct installer paths add no component"),
	          Fixture.VoiceCount(Fixture.Controller),
	          0);
	TestFalse(TEXT("Disabled installer registers no instance events"),
	          FConstructVoiceInstallationTestAccess::HasInstanceObservers(*Fixture.Installer));
	TestEqual(TEXT("Disabled installer registers no controller observers"),
	          FConstructVoiceInstallationTestAccess::PlayerObserverCount(*Fixture.Installer),
	          0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceEnabledInstallationTest,
                                 "Construct.Voice.Installation.EnabledLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FConstructVoiceEnabledInstallationTest::RunTest(const FString& Parameters)
{
	using namespace ConstructVoiceInstallationTests;
	FSettingsScope Settings(true);
	FRuntimeWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	GetMutableDefault<UConstructDeveloperSettings>()->bEnableVoice = false;
	TestTrue(TEXT("Existing world's enabled policy is immutable"), Fixture.Installer->IsVoiceEnabled());
	TestTrue(TEXT("Enabled installer binds actual instance player-list events"),
	         FConstructVoiceInstallationTestAccess::HasInstanceObservers(*Fixture.Installer));
	if (!Fixture.AddPlayer(*this))
	{
		return false;
	}
	TestEqual(TEXT("Actual player-added/controller-change events install one component"),
	          Fixture.VoiceCount(Fixture.Controller),
	          1);
	UConstructVoiceComponent* First = Fixture.Controller->FindComponentByClass<UConstructVoiceComponent>();
	FConstructVoiceInstallationTestAccess::Install(*Fixture.Installer, Fixture.Controller, Fixture.Player.Get());
	FConstructVoiceInstallationTestAccess::Install(*Fixture.Installer, Fixture.Controller, Fixture.Player.Get());
	TestEqual(TEXT("Repeated installer calls retain the same component"),
	          Fixture.Controller->FindComponentByClass<UConstructVoiceComponent>(),
	          First);
	TestEqual(TEXT("Repeated setup leaves exactly one voice component"), Fixture.VoiceCount(Fixture.Controller), 1);
	TestEqual(TEXT("One observer tracks the actual local player"),
	          FConstructVoiceInstallationTestAccess::PlayerObserverCount(*Fixture.Installer),
	          1);

	APlayerController* Remote = Fixture.World->SpawnActor<APlayerController>();
	FConstructVoiceInstallationTestAccess::Install(*Fixture.Installer, Remote, Fixture.Player.Get());
	TestEqual(TEXT("A controller without this local player receives no voice"), Fixture.VoiceCount(Remote), 0);

	APlayerController* Replacement = Fixture.World->SpawnActor<APlayerController>();
	Replacement->SetPlayer(Fixture.Player.Get());
	TestEqual(TEXT("A later controller change is installed without polling"), Fixture.VoiceCount(Replacement), 1);
	Fixture.Controller = Replacement;
	Fixture.Installer->Deinitialize();
	TestFalse(TEXT("Teardown releases game-instance event handles"),
	          FConstructVoiceInstallationTestAccess::HasInstanceObservers(*Fixture.Installer));
	TestEqual(TEXT("Teardown releases all per-player controller observers"),
	          FConstructVoiceInstallationTestAccess::PlayerObserverCount(*Fixture.Installer),
	          0);
	APlayerController* AfterTeardown = Fixture.World->SpawnActor<APlayerController>();
	AfterTeardown->SetPlayer(Fixture.Player.Get());
	TestEqual(TEXT("A controller change after teardown installs nothing"), Fixture.VoiceCount(AfterTeardown), 0);
	Fixture.Controller = AfterTeardown;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceExistingComponentTest,
                                 "Construct.Voice.Installation.PreserveExisting",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FConstructVoiceExistingComponentTest::RunTest(const FString& Parameters)
{
	using namespace ConstructVoiceInstallationTests;
	FSettingsScope Settings(true);
	FRuntimeWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	Fixture.Player.Reset(NewObject<ULocalPlayer>(GEngine));
	Fixture.Controller = Fixture.World->SpawnActor<APlayerController>();
	UConstructVoiceComponent* Existing = NewObject<UConstructVoiceComponent>(Fixture.Controller);
	Fixture.Controller->AddInstanceComponent(Existing);
	Existing->RegisterComponent();
	Fixture.Controller->SetPlayer(Fixture.Player.Get());
	Fixture.Instance->AddLocalPlayer(Fixture.Player.Get(), FPlatformUserId::CreateFromInternalId(0));
	TestEqual(TEXT("Existing authored component is retained"),
	          Fixture.Controller->FindComponentByClass<UConstructVoiceComponent>(),
	          Existing);
	TestEqual(TEXT("Existing authored component is not duplicated"), Fixture.VoiceCount(Fixture.Controller), 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceDisabledManualComponentTest,
                                 "Construct.Voice.Installation.DisabledManualLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FConstructVoiceDisabledManualComponentTest::RunTest(const FString& Parameters)
{
	using namespace ConstructVoiceInstallationTests;
	FSettingsScope Settings(false);
	FRuntimeWorld Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.AddPlayer(*this))
	{
		return false;
	}
	UConstructVoiceComponent* Manual = NewObject<UConstructVoiceComponent>(Fixture.Controller);
	Fixture.Controller->AddInstanceComponent(Manual);
	TestFalse(TEXT("Authored component does not auto activate"), Manual->bAutoActivate);
	TestFalse(TEXT("Authored component starts with ticking disabled"),
	          Manual->PrimaryComponentTick.bStartWithTickEnabled);
	Manual->bAutoActivate = true;
	Manual->PrimaryComponentTick.bStartWithTickEnabled = true;
	Manual->RegisterComponent();
	Manual->Activate(true);
	Manual->SetComponentTickEnabled(true);
	FConstructVoiceRuntimeTestAccess::Begin(*Manual);
	TestFalse(TEXT("Actual BeginPlay respects the world's disabled policy"), Manual->IsVoiceAvailable());
	TestFalse(TEXT("Disabled authored component does not tick"), Manual->IsComponentTickEnabled());
	TestTrue(TEXT("Disabled BeginPlay creates no panel, input or service request"),
	         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*Manual));

	GetMutableDefault<UConstructDeveloperSettings>()->bEnableVoice = true;
	Manual->InitializeLocalRuntime();
	Manual->SetPushToTalkEnabled(true);
	Manual->SetPushToTalkPressed(true);
	Manual->SetListeningEnabled(true);
	TestFalse(TEXT("Project defaults changed mid-world cannot enable this component"), Manual->IsVoiceAvailable());
	TestFalse(TEXT("Inactive component cannot opt in to capture"), Manual->IsPushToTalkEnabled());
	TestFalse(TEXT("Inactive component cannot opt in to listening"), Manual->IsListeningEnabled());
	TestTrue(TEXT("Public controls cannot bypass disabled allocation policy"),
	         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*Manual));
	Manual->DestroyComponent();
	TestTrue(TEXT("Actual component teardown releases no unowned service and creates no resources"),
	         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*Manual));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceLocalRuntimeReplacementTest,
                                 "Construct.Voice.Installation.BegunControllerReplacement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FConstructVoiceLocalRuntimeReplacementTest::RunTest(const FString& Parameters)
{
	using namespace ConstructVoiceInstallationTests;
	FSettingsScope Settings(true);
	FRuntimeWorld Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.AddPlayer(*this))
	{
		return false;
	}
	UConstructVoiceComponent* First = Fixture.Controller->FindComponentByClass<UConstructVoiceComponent>();
	if (!TestNotNull(TEXT("Installer created the first component"), First))
	{
		return false;
	}
	Fixture.Controller->DispatchBeginPlay();
	TestTrue(TEXT("First component completed actual BeginPlay and local setup"),
	         First->HasBegunPlay() && First->IsVoiceAvailable());
	First->SetPushToTalkEnabled(true);
	First->Deactivate();
	TestFalse(TEXT("Native component deactivation releases runtime availability"), First->IsVoiceAvailable());
	TestFalse(TEXT("Native deactivation clears player capture preference"), First->IsPushToTalkEnabled());
	TestTrue(TEXT("Native deactivation releases input and event resources"),
	         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*First));
	First->Activate(true);
	TestTrue(TEXT("Native activation can restore an enabled local component"), First->IsVoiceAvailable());
	First->SetPushToTalkEnabled(true);
	TestTrue(TEXT("First controller owns its player preference"), First->IsPushToTalkEnabled());
	APlayerController* Replacement = Fixture.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Replacement controller exists"), Replacement))
	{
		return false;
	}
	Replacement->SetPlayer(Fixture.Player.Get());
	TestFalse(TEXT("Controller reassignment shuts down previous runtime without destroying its actor"),
	          First->IsVoiceAvailable());
	TestFalse(TEXT("Previous controller's preference cannot survive behind new UI"), First->IsPushToTalkEnabled());
	TestFalse(TEXT("Previous runtime no longer ticks"), First->IsComponentTickEnabled());
	TestTrue(TEXT("Reassignment releases the previous input, UI and event handles"),
	         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*First));
	Fixture.Controller = Replacement;
	Replacement->DispatchBeginPlay();
	UConstructVoiceComponent* Second = Replacement->FindComponentByClass<UConstructVoiceComponent>();
	if (!TestNotNull(TEXT("Replacement has its own component"), Second))
	{
		return false;
	}
	TestTrue(TEXT("Replacement completes actual local BeginPlay"), Second->IsVoiceAvailable());
	Fixture.Instance->RemoveLocalPlayer(Fixture.Player.Get());
	TestFalse(TEXT("Player removal shuts down the surviving controller component"), Second->IsVoiceAvailable());
	TestTrue(TEXT("Player removal releases its runtime resources"),
	         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*Second));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceActivationReplacementTest,
                                 "Construct.Voice.Installation.ActivationReplacesController",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)
bool FConstructVoiceActivationReplacementTest::RunTest(const FString& Parameters)
{
	using namespace ConstructVoiceInstallationTests;
	FSettingsScope Settings(true);
	for (const bool bRemovePlayer : {false, true})
	{
		FRuntimeWorld Fixture;
		if (!Fixture.Initialize(*this))
		{
			return false;
		}
		Fixture.Player.Reset(NewObject<ULocalPlayer>(GEngine));
		Fixture.Instance->AddLocalPlayer(Fixture.Player.Get(), FPlatformUserId::CreateFromInternalId(0));
		APlayerController* FirstController = Fixture.World->SpawnActor<APlayerController>();
		APlayerController* Replacement = Fixture.World->SpawnActor<APlayerController>();
		if (!TestNotNull(TEXT("The authored controller exists"), FirstController) ||
		    !TestNotNull(TEXT("The nested replacement controller exists"), Replacement))
		{
			return false;
		}
		UConstructVoiceComponent* Authored = NewObject<UConstructVoiceComponent>(FirstController);
		FirstController->AddInstanceComponent(Authored);
		Authored->RegisterComponent();
		FirstController->DispatchBeginPlay();
		Replacement->DispatchBeginPlay();
		TestTrue(TEXT("The authored component began play before receiving a local player"), Authored->HasBegunPlay());
		TestFalse(TEXT("The nonlocal authored component stays inactive"), Authored->IsVoiceAvailable());

		TStrongObjectPtr<UConstructVoiceActivationTestReceiver> Receiver(
		    NewObject<UConstructVoiceActivationTestReceiver>());
		Receiver->Replacement = Replacement;
		Receiver->Player = Fixture.Player.Get();
		Authored->OnComponentActivated.AddDynamic(Receiver.Get(),
		                                          &UConstructVoiceActivationTestReceiver::ReplaceController);
		FirstController->SetPlayer(Fixture.Player.Get());
		Fixture.Controller = Replacement;
		UConstructVoiceComponent* Current = Replacement->FindComponentByClass<UConstructVoiceComponent>();
		if (!TestTrue(TEXT("Real activation synchronously invoked the controller replacement"), Receiver->bInvoked) ||
		    !TestNotNull(TEXT("Nested controller assignment installs its own voice"), Current))
		{
			return false;
		}
		TestTrue(TEXT("Controller replacement leaves the departing actor alive"),
		         IsValid(FirstController) && !FirstController->IsActorBeingDestroyed());
		TestEqual(TEXT("The actual local player now belongs to the nested replacement"),
		          Fixture.Player->GetPlayerController(Fixture.World),
		          Replacement);
		TestFalse(TEXT("The departing authored component is shut down during activation"),
		          Authored->IsVoiceAvailable());
		TestFalse(TEXT("The departing component no longer ticks"), Authored->IsComponentTickEnabled());
		TestTrue(TEXT("The departing activation releases every owned runtime resource"),
		         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*Authored));
		TestTrue(TEXT("The nested replacement remains active"), Current->IsVoiceAvailable());
		TestEqual(TEXT("The outer installation never overwrites nested replacement tracking"),
		          FConstructVoiceInstallationTestAccess::TrackedComponent(*Fixture.Installer, Fixture.Player.Get()),
		          Current);
		TestEqual(TEXT("The nested replacement has exactly one component"), Fixture.VoiceCount(Replacement), 1);

		if (bRemovePlayer)
		{
			Fixture.Instance->RemoveLocalPlayer(Fixture.Player.Get());
			TestFalse(TEXT("Actual player removal releases the nested replacement"), Current->IsVoiceAvailable());
		}
		else
		{
			Fixture.Installer->Deinitialize();
			TestTrue(TEXT("Subsystem teardown does not rely on destroying the replacement actor"),
			         IsValid(Replacement) && !Replacement->IsActorBeingDestroyed());
			TestFalse(TEXT("Subsystem teardown releases the tracked nested replacement"), Current->IsVoiceAvailable());
		}
		TestTrue(TEXT("Both cleanup paths release the nested replacement's resources"),
		         FConstructVoiceRuntimeTestAccess::HasNoRuntimeResources(*Current));
		TestNull(TEXT("Cleanup removes the nested replacement's tracking entry"),
		         FConstructVoiceInstallationTestAccess::TrackedComponent(*Fixture.Installer, Fixture.Player.Get()));
		TestEqual(TEXT("Cleanup releases the local player's controller observer"),
		          FConstructVoiceInstallationTestAccess::PlayerObserverCount(*Fixture.Installer),
		          0);
		TestFalse(TEXT("Cleanup cannot reactivate the departed authored component"), Authored->IsVoiceAvailable());
	}
	return true;
}
#endif
