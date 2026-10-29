#include "ConstructUIInputSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "ConstructShellFramework.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModalPolicyTest,
                                 "Construct.Shell.Input.NestedModalPolicy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModalPolicyTest::RunTest(const FString& Parameters)
{
	ULocalPlayer* Local = NewObject<ULocalPlayer>(GEngine);
	UConstructUIInputSubsystem* Gate = NewObject<UConstructUIInputSubsystem>(Local);
	TestFalse(TEXT("No session defaults to blocked"), Gate->IsGameplayInputAllowed());
	Gate->SetSessionReady(true);
	TestTrue(TEXT("Ready session allows local input"), Gate->IsGameplayInputAllowed());
	Gate->SetModal(TEXT("Tools"), true);
	Gate->SetModal(TEXT("Legal"), true);
	Gate->SetModal(TEXT("Tools"), false);
	TestFalse(TEXT("Closing tools cannot bypass legal hold"), Gate->IsGameplayInputAllowed());
	Gate->SetModal(TEXT("Legal"), false);
	TestTrue(TEXT("Last modal release restores input"), Gate->IsGameplayInputAllowed());
	Gate->SetModal(TEXT("Tools"), true);
	Gate->SetModal(TEXT("Tools"), true);
	Gate->SetModal(TEXT("Tools"), false);
	TestTrue(TEXT("Repeated visibility does not leak an input block"), Gate->IsGameplayInputAllowed());
	Gate->SetSessionReady(false);
	TestFalse(TEXT("Session loss blocks again"), Gate->IsGameplayInputAllowed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHUDReleasesInputManagementTest,
                                 "Construct.Shell.Input.HUDEndPlayReleasesManagement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHUDReleasesInputManagementTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Input lifecycle fixture needs the initialized Engine"), GEngine))
	{
		return false;
	}
	// Each case starts with a fresh LocalPlayer and real HUD; no GameInstance or SDK session is created.
	for (const bool bPreviouslyReady : {false, true})
	{
		const FString Case = bPreviouslyReady ? TEXT("previously ready") : TEXT("previously blocked");
		UWorld::InitializationValues Init;
		Init.InitializeScenes(true)
		    .AllowAudioPlayback(false)
		    .CreatePhysicsScene(false)
		    .CreateNavigation(false)
		    .CreateAISystem(false)
		    .ShouldSimulatePhysics(false);
		UWorld* World = UWorld::CreateWorld(
		    EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!TestNotNull(*FString::Printf(TEXT("%s: isolated Engine world exists"), *Case), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
		TStrongObjectPtr<ULocalPlayer> Local;
		bool bPlayerAdded = false;
		ON_SCOPE_EXIT
		{
			if (bPlayerAdded)
			{
				Local->PlayerRemoved();
			}
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		};
		World->InitializeActorsForPlay(FURL());
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!TestNotNull(*FString::Printf(TEXT("%s: real controller exists"), *Case), Controller))
		{
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.Owner = Controller;
		AConstructShellHUD* HUD =
		    World->SpawnActor<AConstructShellHUD>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(*FString::Printf(TEXT("%s: real ConstructShellHUD exists"), *Case), HUD))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s: Engine assigned the HUD's actual owning controller"), *Case),
		         HUD->GetOwningPlayerController() == Controller);
		// Before attaching a player the real ShellHUD BeginPlay safely skips its local UI/SDK branch.
		// Engine lifecycle state is genuine: no synthetic begun-play flag or direct EndPlay call.
		if (!TestFalse(*FString::Printf(TEXT("%s: controller has no local player before HUD BeginPlay"), *Case),
		               Controller->IsLocalController()))
		{
			return false;
		}
		HUD->DispatchBeginPlay();
		TestTrue(*FString::Printf(TEXT("%s: the real HUD entered play"), *Case), HUD->HasActorBegunPlay());
		Local.Reset(NewObject<ULocalPlayer>(GEngine));
		Local->PlayerAdded(nullptr, int32(0));
		bPlayerAdded = true;
		Controller->SetPlayer(Local.Get());
		UConstructUIInputSubsystem* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
		if (!TestNotNull(*FString::Printf(TEXT("%s: actual LocalPlayer owns its input gate"), *Case), Gate))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s: HUD resolves the same LocalPlayer used by the test"), *Case),
		         HUD->GetOwningPlayerController()->GetLocalPlayer() == Local.Get());
		Gate->SetSessionReady(bPreviouslyReady);
		TestEqual(*FString::Printf(TEXT("%s: fixture installed the intended readiness"), *Case),
		          Gate->IsGameplayInputAllowed(),
		          bPreviouslyReady);
		TestEqual(*FString::Printf(TEXT("%s: management actually reaches controller move input"), *Case),
		          Controller->IsMoveInputIgnored(),
		          !bPreviouslyReady);
		TestEqual(*FString::Printf(TEXT("%s: management actually reaches controller look input"), *Case),
		          Controller->IsLookInputIgnored(),
		          !bPreviouslyReady);
		// DestroyActor is the operation under test. Removing the EndPlay ReleaseManagement line must make this red.
		TestTrue(*FString::Printf(TEXT("%s: Engine destroys the actual shell HUD"), *Case), HUD->Destroy());
		TestFalse(*FString::Printf(TEXT("%s: HUD destruction routed its actual EndPlay"), *Case),
		          HUD->HasActorBegunPlay());
		TestFalse(*FString::Printf(TEXT("%s: HUD teardown releases its own move-input block"), *Case),
		          Controller->IsMoveInputIgnored());
		TestFalse(*FString::Printf(TEXT("%s: HUD teardown releases its own look-input block"), *Case),
		          Controller->IsLookInputIgnored());
		TestFalse(*FString::Printf(TEXT("%s: old session readiness is cleared after HUD teardown"), *Case),
		          Gate->IsGameplayInputAllowed());
		TestFalse(*FString::Printf(TEXT("%s: old readiness cannot authorize navigation"), *Case),
		          Gate->CanUseNavigationInput(TEXT("PostShellFixture")));
		Gate->SetModal(TEXT("PostShellFixture"), true);
		TestFalse(*FString::Printf(TEXT("%s: raising a modal after HUD destruction cannot capture move input"), *Case),
		          Controller->IsMoveInputIgnored());
		TestFalse(*FString::Printf(TEXT("%s: raising a modal after HUD destruction cannot capture look input"), *Case),
		          Controller->IsLookInputIgnored());
		Gate->SetModal(TEXT("PostShellFixture"), false);
		TestFalse(*FString::Printf(TEXT("%s: dropping a modal after HUD destruction leaves move input alone"), *Case),
		          Controller->IsMoveInputIgnored());
		TestFalse(*FString::Printf(TEXT("%s: dropping a modal after HUD destruction leaves look input alone"), *Case),
		          Controller->IsLookInputIgnored());
		TestFalse(*FString::Printf(TEXT("%s: releasing a modal cannot restore stale ready state"), *Case),
		          Gate->IsGameplayInputAllowed());
		AddInfo(FString::Printf(
		    TEXT("Actual HUD Destroy/EndPlay exercised with %s management; no SDK state or direct release call used."),
		    *Case));
	}
	return true;
}
#endif
