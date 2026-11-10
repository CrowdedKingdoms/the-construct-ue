#include "ConstructUIInputSubsystem.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Components/InputComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "ConstructShellFramework.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Slate/SceneViewport.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"

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
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
		WorldContext.SetCurrentWorld(World);
		TStrongObjectPtr<UGameViewportClient> Viewport(NewObject<UGameViewportClient>(GEngine));
		const TSharedRef<SViewport> ViewportWidget = SNew(SViewport);
		TSharedPtr<FSceneViewport> SceneViewport = Viewport->CreateViewport(ViewportWidget);
		Viewport->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringRightMouseDown);
		Viewport->SetMouseLockMode(EMouseLockMode::LockInFullscreen);
		Viewport->SetHideCursorDuringCapture(true);
		Viewport->SetIgnoreInput(true);
		WorldContext.GameViewport = Viewport.Get();
		TStrongObjectPtr<ULocalPlayer> Local;
		bool bPlayerAdded = false;
		ON_SCOPE_EXIT
		{
			if (bPlayerAdded)
			{
				Local->PlayerRemoved();
			}
			SceneViewport.Reset();
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
		Local->PlayerAdded(Viewport.Get(), int32(0));
		bPlayerAdded = true;
		Controller->SetPlayer(Local.Get());
		Controller->bShowMouseCursor = true;
		UConstructUIInputSubsystem* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
		if (!TestNotNull(*FString::Printf(TEXT("%s: actual LocalPlayer owns its input gate"), *Case), Gate))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s: HUD resolves the same LocalPlayer used by the test"), *Case),
		         HUD->GetOwningPlayerController()->GetLocalPlayer() == Local.Get());
		Gate->SetSessionReady(bPreviouslyReady);
		TestEqual(TEXT("The shell installed its requested capture mode"), Viewport->GetMouseCaptureMode(),
		          bPreviouslyReady ? EMouseCaptureMode::CapturePermanently : EMouseCaptureMode::CaptureDuringMouseDown);
		TestEqual(*FString::Printf(TEXT("%s: fixture installed the intended readiness"), *Case),
		          Gate->IsGameplayInputAllowed(),
		          bPreviouslyReady);
		TestEqual(*FString::Printf(TEXT("%s: management actually reaches controller move input"), *Case),
		          Controller->IsMoveInputIgnored(),
		          !bPreviouslyReady);
		TestEqual(*FString::Printf(TEXT("%s: management actually reaches controller look input"), *Case),
		          Controller->IsLookInputIgnored(),
		          !bPreviouslyReady);
		TestEqual(TEXT("Cursor is hidden in gameplay and available when input is blocked"),
		          Controller->bShowMouseCursor != 0, !bPreviouslyReady);
		if (bPreviouslyReady && TestNotNull(TEXT("The shell installed its controller input"), Gate->PointerInput.Get()))
		{
			const FInputKeyBinding* CursorBinding = Gate->PointerInput->KeyBindings.FindByPredicate(
			    [](const FInputKeyBinding& Binding)
			    {
				    return Binding.Chord.Key == EKeys::F4 && Binding.KeyEvent == IE_Pressed;
			    });
			if (TestNotNull(TEXT("F4 is bound to the cursor toggle"), CursorBinding))
			{
				CursorBinding->KeyDelegate.Execute(EKeys::F4);
				TestTrue(TEXT("F4 enables the cursor"), Controller->bShowMouseCursor);
				CursorBinding->KeyDelegate.Execute(EKeys::F4);
				TestFalse(TEXT("F4 returns to gameplay"), Controller->bShowMouseCursor);
				CursorBinding->KeyDelegate.Execute(EKeys::F4);
				const FName ChatModal(TEXT("ConstructChat"));
				Gate->SetModal(ChatModal, true);
				TestTrue(TEXT("Chat keeps the cursor visible"), Controller->bShowMouseCursor);
				TestTrue(TEXT("Chat blocks look input"), Controller->IsLookInputIgnored());
				Gate->SetModal(ChatModal, false);
				TestFalse(TEXT("Closing chat clears pointer mode and hides the cursor"), Controller->bShowMouseCursor);
				TestFalse(TEXT("Closing chat restores look input"), Controller->IsLookInputIgnored());
			}
		}
		// DestroyActor is the operation under test. Removing the EndPlay ReleaseManagement line must make this red.
		TestTrue(*FString::Printf(TEXT("%s: Engine destroys the actual shell HUD"), *Case), HUD->Destroy());
		TestFalse(*FString::Printf(TEXT("%s: HUD destruction routed its actual EndPlay"), *Case),
		          HUD->HasActorBegunPlay());
		TestFalse(*FString::Printf(TEXT("%s: HUD teardown releases its own move-input block"), *Case),
		          Controller->IsMoveInputIgnored());
		TestFalse(*FString::Printf(TEXT("%s: HUD teardown releases its own look-input block"), *Case),
		          Controller->IsLookInputIgnored());
		TestNull(TEXT("HUD teardown releases the cursor binding"), Gate->PointerInput.Get());
		TestTrue(TEXT("HUD teardown restores the old cursor visibility"), Controller->bShowMouseCursor);
		TestEqual(TEXT("HUD teardown restores the previous capture policy"), Viewport->GetMouseCaptureMode(),
		          EMouseCaptureMode::CaptureDuringRightMouseDown);
		TestEqual(TEXT("HUD teardown restores the previous lock policy"), Viewport->GetMouseLockMode(),
		          EMouseLockMode::LockInFullscreen);
		TestTrue(TEXT("HUD teardown restores the previous cursor-capture visibility"), Viewport->HideCursorDuringCapture());
		TestTrue(TEXT("HUD teardown restores the previous viewport input policy"), Viewport->IgnoreInput());
		TestTrue(TEXT("HUD teardown releases Slate mouse capture"), Local->GetSlateOperations().ShouldReleaseMouse());
		TestTrue(TEXT("HUD teardown releases Slate mouse lock"), Local->GetSlateOperations().ShouldReleaseMouseLock());
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
