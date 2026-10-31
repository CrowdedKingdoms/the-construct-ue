#include "ConstructVoiceComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SWindow.h"

namespace ConstructVoiceClickTests
{
void FindCheckBoxes(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SCheckBox>>& Boxes)
{
	if (Widget->GetType() == FName(TEXT("SCheckBox")))
	{
		Boxes.Add(StaticCastSharedRef<SCheckBox>(Widget));
	}
	FChildren* Children = Widget->GetChildren();
	for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
	{
		FindCheckBoxes(Children->GetChildAt(Index), Boxes);
	}
}

APlayerController* FindGameController()
{
	if (!GEngine)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		if (World && World->WorldType == EWorldType::Game && Controller && Controller->GetLocalPlayer())
		{
			return Controller;
		}
	}
	return nullptr;
}

UConstructVoicePanel* FindPanel(const APlayerController* Controller)
{
	for (TObjectIterator<UConstructVoicePanel> It; Controller && It; ++It)
	{
		if (It->GetOwningPlayer() == Controller && It->IsInViewport())
		{
			return *It;
		}
	}
	return nullptr;
}

bool IsReady()
{
	const APlayerController* Controller = FindGameController();
	const UConstructVoiceComponent* Voice = Controller ? Controller->FindComponentByClass<UConstructVoiceComponent>() : nullptr;
	return Voice && Voice->IsVoiceAvailable() && FindPanel(Controller);
}

// A real pointer click routed by Slate's hit test within the game's window, so any widget the game draws over the box
// takes the click instead.
void Click(FAutomationTestBase& Test, const TSharedRef<SCheckBox>& Box, int32 UserIndex, const TCHAR* What)
{
	FSlateApplication& Slate = FSlateApplication::Get();
	const TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Box);
	if (!Test.TestTrue(FString::Printf(TEXT("The %s tick is in a window"), What), Window.IsValid()))
	{
		return;
	}
	const FVector2D At = Box->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.05f, 0.5f));
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(At, {Window.ToSharedRef()}, false, UserIndex);
	// The box's own tick image may be the topmost widget; the click reaches the box when the box is on its path.
	const bool bReaches = Path.ContainsWidget(&Box.Get());
	if (!bReaches && Path.Widgets.Num() > 0)
	{
		const TSharedRef<SWidget>& Top = Path.Widgets.Last().Widget;
		Test.AddInfo(FString::Printf(TEXT("The %s tick is covered by %s (%s)"), What, *Top->GetTypeAsString(), *Top->GetReadableLocation()));
	}
	Test.TestTrue(FString::Printf(TEXT("A mouse click on the %s tick reaches it, with nothing drawn over it"), What), bReaches);
	const FPointerEvent Down(UserIndex, 0, At, At, TSet<FKey>({EKeys::LeftMouseButton}), EKeys::LeftMouseButton, 0,
	                         FModifierKeysState());
	const FPointerEvent Up(UserIndex, 0, At, At, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
	Slate.RoutePointerDownEvent(Path, Down);
	Slate.RoutePointerUpEvent(Path, Up);
}

void RunClicks(FAutomationTestBase& Test)
{
	APlayerController* Controller = FindGameController();
	UConstructVoiceComponent* Voice = Controller ? Controller->FindComponentByClass<UConstructVoiceComponent>() : nullptr;
	UConstructVoicePanel* Panel = FindPanel(Controller);
	const TSharedPtr<FSlateUser> User = Controller ? Controller->GetLocalPlayer()->GetSlateUser() : nullptr;
	if (!IsReady())
	{
		// The wrapper reads this line as "not run": a sign-in or setup problem, not a fault in the panel.
		UE_LOG(LogTemp, Display, TEXT("ConstructRenderedFocus: NOT RUN: the game did not sign in or show the voice panel within 120 s"));
	}
	if (!Test.TestTrue(TEXT("A signed-in rendered game shows the enabled voice panel within 120 s"), IsReady()) ||
	    !Test.TestTrue(TEXT("Push-to-talk and listening begin off"), !Voice->IsPushToTalkEnabled() && !Voice->IsListeningEnabled()) ||
	    !Test.TestTrue(TEXT("Requires an initialized Slate application and local user"), FSlateApplication::IsInitialized() && User))
	{
		return;
	}
	TArray<TSharedRef<SCheckBox>> Boxes;
	if (const TSharedPtr<SWidget> Root = Panel->GetCachedWidget())
	{
		FindCheckBoxes(Root.ToSharedRef(), Boxes);
	}
	if (!Test.TestEqual(TEXT("The panel shows the push-to-talk and listening ticks"), Boxes.Num(), 2))
	{
		return;
	}
	const int32 UserIndex = User->GetUserIndex();
	Click(Test, Boxes[0], UserIndex, TEXT("push-to-talk"));
	Test.TestTrue(TEXT("Clicking the push-to-talk tick enables push-to-talk"), Voice->IsPushToTalkEnabled());
	Click(Test, Boxes[0], UserIndex, TEXT("push-to-talk"));
	Test.TestFalse(TEXT("Clicking it again disables push-to-talk"), Voice->IsPushToTalkEnabled());
	Click(Test, Boxes[1], UserIndex, TEXT("listening"));
	Test.TestTrue(TEXT("Clicking the listening tick turns listening on"), Voice->IsListeningEnabled());
	Click(Test, Boxes[1], UserIndex, TEXT("listening"));
	Test.TestFalse(TEXT("Clicking it again turns listening off"), Voice->IsListeningEnabled());
	Voice->SetPushToTalkEnabled(false);
	Voice->SetListeningEnabled(false);
}
}

// Run in a standalone rendered game (-game; scripts\run-rendered-focus.ps1 starts it), whose window holds only game
// widgets, as a packaged game's does. It waits for sign-in and the voice panel itself.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructVoiceMouseClickTest, "Construct.Voice.RenderedClick.MouseClickReachesTicks",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter |
                                     EAutomationTestFlags::NonNullRHI)
bool FConstructVoiceMouseClickTest::RunTest(const FString& Parameters)
{
	const double StartedAt = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
	    [this, StartedAt]()
	    {
		    if (!ConstructVoiceClickTests::IsReady() && FPlatformTime::Seconds() - StartedAt < 120.0)
		    {
			    return false;
		    }
		    ConstructVoiceClickTests::RunClicks(*this);
		    return true;
	    }));
	return true;
}
#endif
