#include "ConstructVoiceComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructUIInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/Input/SCheckBox.h"

namespace ConstructVoiceFocusTests
{
TSharedPtr<SCheckBox> FindFirstCheckBox(const TSharedRef<SWidget>& Widget)
{
	if (Widget->GetType() == FName(TEXT("SCheckBox")))
	{
		return StaticCastSharedRef<SCheckBox>(Widget);
	}
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
	{
		if (TSharedPtr<SCheckBox> Found = FindFirstCheckBox(Children->GetChildAt(Index)))
		{
			return Found;
		}
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceRenderedFocusTest,
                                 "Construct.Voice.RenderedFocus.ReturnAfterPushToTalkToggle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
                                     EAutomationTestFlags::NonNullRHI)
bool FConstructVoiceRenderedFocusTest::RunTest(const FString& Parameters)
{
	APlayerController* Controller = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World(); World && World->WorldType == EWorldType::PIE)
			{
				APlayerController* Candidate = World->GetFirstPlayerController();
				if (Candidate && Candidate->IsLocalController() && Candidate->GetLocalPlayer())
				{
					Controller = Candidate;
					break;
				}
			}
		}
	}
	if (!TestNotNull(TEXT("Requires an existing rendered PIE local controller"), Controller))
	{
		return false;
	}
	ULocalPlayer* Player = Controller->GetLocalPlayer();
	UConstructUIInputSubsystem* Gate = Player->GetSubsystem<UConstructUIInputSubsystem>();
	UConstructVoiceComponent* Voice = Controller->FindComponentByClass<UConstructVoiceComponent>();
	UGameInstance* Instance = Controller->GetWorld()->GetGameInstance();
	const UCrowdyAuthentication* Authentication = Instance ? Instance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	if (!TestNotNull(TEXT("Uses the actual enabled voice component"), Voice) ||
	    !TestNotNull(TEXT("Uses the actual runtime input gate"), Gate) ||
	    !TestTrue(TEXT("Requires a real signed-in session and gameplay readiness"),
	              Authentication && Authentication->IsSignedIn() && Gate->IsGameplayInputAllowed()) ||
	    !TestTrue(TEXT("Capture, listening and push-to-talk must begin off"),
	              Voice->IsVoiceAvailable() && !Voice->IsPushToTalkEnabled() && !Voice->IsListeningEnabled() &&
	                  !Voice->IsCaptureActive()) ||
	    !TestTrue(TEXT("Requires an initialized Slate application"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UConstructVoicePanel* Panel = nullptr;
	for (TObjectIterator<UConstructVoicePanel> It; It; ++It)
	{
		if (It->GetOwningPlayer() == Controller && It->IsInViewport())
		{
			Panel = *It;
			break;
		}
	}
	if (!TestNotNull(TEXT("Uses the component's actual on-screen voice panel"), Panel))
	{
		return false;
	}
	const TSharedPtr<SWidget> PanelWidget = Panel->GetCachedWidget();
	const TSharedPtr<SCheckBox> CheckBox =
	    PanelWidget ? ConstructVoiceFocusTests::FindFirstCheckBox(PanelWidget.ToSharedRef()) : nullptr;
	const TSharedPtr<FSlateUser> User = Player->GetSlateUser();
	if (!TestTrue(TEXT("The actual push-to-talk checkbox and local Slate user exist"), CheckBox && User))
	{
		return false;
	}
	FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), CheckBox, EFocusCause::SetDirectly);
	TestTrue(TEXT("The actual checkbox holds this local user's native focus"),
	         CheckBox->HasUserFocus(User->GetUserIndex()).IsSet());
	const FKeyEvent Space(EKeys::SpaceBar, FModifierKeysState(), User->GetUserIndex(), false, 0, 0);
	TestTrue(TEXT("Checkbox handles native Space press"),
	         CheckBox->OnKeyDown(CheckBox->GetCachedGeometry(), Space).IsEventHandled());
	TestTrue(TEXT("Checkbox handles native Space release"),
	         CheckBox->OnKeyUp(CheckBox->GetCachedGeometry(), Space).IsEventHandled());
	TestTrue(TEXT("The native checkbox callback opts in without starting capture or listening"),
	         Voice->IsPushToTalkEnabled() && !Voice->IsCaptureActive() && !Voice->IsListeningEnabled());
	const TWeakObjectPtr<APlayerController> WeakController(Controller);
	const TWeakObjectPtr<UConstructVoiceComponent> WeakVoice(Voice);
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand(
	    [this, WeakController, WeakVoice]()
	    {
		    APlayerController* Current = WeakController.Get();
		    UConstructVoiceComponent* CurrentVoice = WeakVoice.Get();
		    UGameViewportClient* Viewport =
		        Current && Current->GetWorld() ? Current->GetWorld()->GetGameViewport() : nullptr;
		    TestTrue(TEXT("Push-to-talk UI returns focus to the actual scene viewport used by V"),
		             Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus());
		    TestTrue(TEXT("The focus transition never starts capture or listening"),
		             CurrentVoice && !CurrentVoice->IsCaptureActive() && !CurrentVoice->IsListeningEnabled());
		    if (CurrentVoice)
		    {
			    CurrentVoice->SetPushToTalkEnabled(false);
		    }
	    },
	    0.2f));
	return true;
}
#endif
