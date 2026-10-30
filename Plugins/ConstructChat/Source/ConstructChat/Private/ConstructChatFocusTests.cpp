#include "ConstructChatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructUIInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"

namespace ConstructChatFocusTests
{
void FindButtons(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SButton>>& Buttons)
{
	if (Widget->GetType() == FName(TEXT("SButton")))
	{
		Buttons.Add(StaticCastSharedRef<SButton>(Widget));
	}
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
	{
		FindButtons(Children->GetChildAt(Index), Buttons);
	}
}

bool ActivateButton(FAutomationTestBase& Test, APlayerController* Controller, UConstructChatPanel* Panel, int32 Index)
{
	if (!Test.TestTrue(TEXT("Native button action retains the actual controller and on-screen panel"),
	                   IsValid(Controller) && Controller->GetLocalPlayer() && IsValid(Panel) &&
	                       Panel->GetOwningPlayer() == Controller && Panel->IsInViewport()) ||
	    !Test.TestTrue(TEXT("Native button action requires initialized Slate"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	const TSharedPtr<SWidget> PanelWidget = Panel->GetCachedWidget();
	TArray<TSharedRef<SButton>> Buttons;
	if (PanelWidget)
	{
		FindButtons(PanelWidget.ToSharedRef(), Buttons);
	}
	const TSharedPtr<FSlateUser> User = Controller->GetLocalPlayer()->GetSlateUser();
	if (!Test.TestTrue(TEXT("The actual mode or Refresh button is present and enabled"),
	                   Buttons.IsValidIndex(Index) && Buttons[Index]->IsEnabled() && User))
	{
		return false;
	}
	const TSharedRef<SButton> Button = Buttons[Index];
	FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), Button, EFocusCause::SetDirectly);
	Test.TestTrue(TEXT("The actual chat button acquires native local-user focus before activation"),
	              Button->HasUserFocus(User->GetUserIndex()).IsSet());
	const FKeyEvent Space(EKeys::SpaceBar, FModifierKeysState(), User->GetUserIndex(), false, 0, 0);
	const bool bPressed = Test.TestTrue(TEXT("Chat button handles native Space press"),
	                                    Button->OnKeyDown(Button->GetCachedGeometry(), Space).IsEventHandled());
	const bool bReleased = Test.TestTrue(TEXT("Chat button handles native Space release"),
	                                     Button->OnKeyUp(Button->GetCachedGeometry(), Space).IsEventHandled());
	return bPressed && bReleased;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatRenderedFocusTest,
                                 "Construct.Chat.RenderedFocus.ReturnAfterCloseAndEmptySubmit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
                                     EAutomationTestFlags::NonNullRHI)
bool FConstructChatRenderedFocusTest::RunTest(const FString& Parameters)
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
	UConstructChatComponent* Chat = Controller->FindComponentByClass<UConstructChatComponent>();
	UConstructUIInputSubsystem* Gate = Controller->GetLocalPlayer()->GetSubsystem<UConstructUIInputSubsystem>();
	if (!TestNotNull(TEXT("Uses the actual runtime chat component"), Chat) ||
	    !TestNotNull(TEXT("Uses the actual runtime input gate"), Gate) ||
	    !TestTrue(TEXT("Requires a real signed-in session and gameplay readiness"),
	              Chat->IsSignedIn() && Gate->IsGameplayInputAllowed()))
	{
		return false;
	}
	UConstructChatPanel* Panel = nullptr;
	for (TObjectIterator<UConstructChatPanel> It; It; ++It)
	{
		if (It->GetOwningPlayer() == Controller && It->IsInViewport())
		{
			Panel = *It;
			break;
		}
	}
	if (!TestNotNull(TEXT("Uses the component's actual on-screen chat panel"), Panel))
	{
		return false;
	}
	Chat->UseNearbyChat();
	Chat->OpenComposer();
	if (!TestTrue(TEXT("The actual composer opens and takes native text-field focus"),
	              Chat->IsComposerOpen() && Panel->HasUserFocusedDescendants(Controller)))
	{
		Chat->CloseComposer();
		return false;
	}
	Chat->CloseComposer();
	const TWeakObjectPtr<APlayerController> WeakController(Controller);
	const TWeakObjectPtr<UConstructChatComponent> WeakChat(Chat);
	const TWeakObjectPtr<UConstructChatPanel> WeakPanel(Panel);
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand(
	    [this, WeakController, WeakChat]()
	    {
		    APlayerController* Current = WeakController.Get();
		    UConstructChatComponent* CurrentChat = WeakChat.Get();
		    UGameViewportClient* Viewport =
		        Current && Current->GetWorld() ? Current->GetWorld()->GetGameViewport() : nullptr;
		    TestTrue(TEXT("Closing restores actual scene viewport focus, as required by voice input"),
		             Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus());
		    if (CurrentChat)
		    {
			    CurrentChat->OpenComposer();
			    TestTrue(TEXT("Composer can open again after focus restoration"), CurrentChat->IsComposerOpen());
			    // Empty text returns before any SDK send; this exercises the actual submit/close path.
			    CurrentChat->SubmitComposer(FString());
		    }
	    },
	    0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand(
	    [this, WeakController, WeakChat, WeakPanel]()
	    {
		    APlayerController* Current = WeakController.Get();
		    const UConstructChatComponent* CurrentChat = WeakChat.Get();
		    UGameViewportClient* Viewport =
		        Current && Current->GetWorld() ? Current->GetWorld()->GetGameViewport() : nullptr;
		    TestTrue(TEXT("Empty submit closes without any send"), CurrentChat && !CurrentChat->IsComposerOpen());
		    TestTrue(TEXT("Submitting restores actual scene viewport focus"),
		             Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus());
		    // Native button callbacks may read existing membership, but never publish, create or join a channel.
		    ConstructChatFocusTests::ActivateButton(*this, Current, WeakPanel.Get(), 0);
	    },
	    0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand(
	    [this, WeakController, WeakChat, WeakPanel]()
	    {
		    APlayerController* Current = WeakController.Get();
		    const UConstructChatComponent* CurrentChat = WeakChat.Get();
		    UGameViewportClient* Viewport =
		        Current && Current->GetWorld() ? Current->GetWorld()->GetGameViewport() : nullptr;
		    TestTrue(TEXT("The actual mode button switches to channel chat while the composer is closed"),
		             CurrentChat && CurrentChat->IsChannelMode() && !CurrentChat->IsComposerOpen());
		    TestTrue(TEXT("The channel-mode button restores actual scene viewport focus"),
		             Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus());
		    ConstructChatFocusTests::ActivateButton(*this, Current, WeakPanel.Get(), 1);
	    },
	    0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand(
	    [this, WeakController, WeakChat, WeakPanel]()
	    {
		    APlayerController* Current = WeakController.Get();
		    const UConstructChatComponent* CurrentChat = WeakChat.Get();
		    UGameViewportClient* Viewport =
		        Current && Current->GetWorld() ? Current->GetWorld()->GetGameViewport() : nullptr;
		    TestTrue(TEXT("Refresh retains channel mode with the composer closed"),
		             CurrentChat && CurrentChat->IsChannelMode() && !CurrentChat->IsComposerOpen());
		    TestTrue(TEXT("Refresh restores actual scene viewport focus even if membership is loading or unavailable"),
		             Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus());
		    ConstructChatFocusTests::ActivateButton(*this, Current, WeakPanel.Get(), 0);
	    },
	    0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand(
	    [this, WeakController, WeakChat]()
	    {
		    APlayerController* Current = WeakController.Get();
		    UConstructChatComponent* CurrentChat = WeakChat.Get();
		    UGameViewportClient* Viewport =
		        Current && Current->GetWorld() ? Current->GetWorld()->GetGameViewport() : nullptr;
		    TestTrue(TEXT("The actual mode button returns to nearby chat with the composer closed"),
		             CurrentChat && !CurrentChat->IsChannelMode() && !CurrentChat->IsComposerOpen());
		    TestTrue(TEXT("The nearby-mode button restores actual scene viewport focus"),
		             Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus());
		    if (CurrentChat)
		    {
			    CurrentChat->UseNearbyChat();
		    }
	    },
	    0.2f));
	return true;
}
#endif
