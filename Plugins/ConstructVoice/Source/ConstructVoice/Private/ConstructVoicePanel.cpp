#include "ConstructVoiceComponent.h"

#include "ConstructShellStyle.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Styling/SlateStyle.h"
#include "TimerManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructVoice"

UConstructVoiceComponent* UConstructVoicePanel::FindVoiceComponent() const
{
	return GetOwningPlayer() ? GetOwningPlayer()->FindComponentByClass<UConstructVoiceComponent>() : nullptr;
}

void UConstructVoicePanel::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameplayFocusTimer);
	}
	GameplayFocusTimer.Invalidate();
	Super::NativeDestruct();
}

void UConstructVoicePanel::RequestGameplayFocusRestore()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	UConstructUIInputSubsystem* InputGate =
	    LocalPlayer ? LocalPlayer->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
	UConstructVoiceComponent* Voice = FindVoiceComponent();
	if (!World || !IsInViewport() || !Controller || !Controller->IsLocalController() || !Voice ||
	    !Voice->IsVoiceAvailable() || !InputGate || !InputGate->IsGameplayInputAllowed())
	{
		return;
	}
	World->GetTimerManager().ClearTimer(GameplayFocusTimer);
	const TWeakObjectPtr<APlayerController> ExpectedController(Controller);
	const TWeakObjectPtr<ULocalPlayer> ExpectedPlayer(LocalPlayer);
	const TWeakObjectPtr<UConstructVoiceComponent> ExpectedVoice(Voice);
	// Defer until the checkbox/button has applied its own focus reply.
	GameplayFocusTimer = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
	    this,
	    [this, ExpectedController, ExpectedPlayer, ExpectedVoice]()
	    {
		    GameplayFocusTimer.Invalidate();
		    APlayerController* CurrentController = GetOwningPlayer();
		    ULocalPlayer* Player = CurrentController ? CurrentController->GetLocalPlayer() : nullptr;
		    UWorld* CurrentWorld = GetWorld();
		    UConstructVoiceComponent* CurrentVoice = FindVoiceComponent();
		    UConstructUIInputSubsystem* Gate = Player ? Player->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
		    if (!IsInViewport() || !Player || Player != ExpectedPlayer.Get() || !CurrentWorld ||
		        CurrentController != ExpectedController.Get() || !CurrentController->IsLocalController() ||
		        Player->GetPlayerController(CurrentWorld) != CurrentController || CurrentVoice != ExpectedVoice.Get() ||
		        !CurrentVoice || !CurrentVoice->IsVoiceAvailable() || !Gate || !Gate->IsGameplayInputAllowed() ||
		        !FSlateApplication::IsInitialized())
		    {
			    return;
		    }
		    // Another panel or a legal prompt may have taken focus since the action.
		    if (!HasUserFocus(CurrentController) && !HasUserFocusedDescendants(CurrentController))
		    {
			    return;
		    }
		    UGameViewportClient* Viewport = CurrentWorld->GetGameViewport();
		    const TSharedPtr<FSlateUser> User = Player->GetSlateUser();
		    const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		    if (User && ViewportWidget && Player->ViewportClient == Viewport)
		    {
			    FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), ViewportWidget, EFocusCause::SetDirectly);
		    }
	    }));
}

TSharedRef<SWidget> UConstructVoicePanel::RebuildWidget()
{
	const ISlateStyle& Style = FConstructShellStyle::Get();
	const TWeakObjectPtr<UConstructVoicePanel> WeakThis(this);
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot()
	    .AutoHeight()[SNew(STextBlock).Text(LOCTEXT("Title", "Voice")).TextStyle(&Style, "Construct.Text.Heading")];
	Content->AddSlot().AutoHeight().Padding(
	    0, 12, 0, 20)[SNew(STextBlock)
	                      .Text(LOCTEXT("Intro", "Microphone and listening are separate choices. Both start off."))
	                      .TextStyle(&Style, "Construct.Text.Body")
	                      .AutoWrapText(true)];
	Content->AddSlot().AutoHeight().Padding(
	    0, 0, 0, 16)[SNew(SCheckBox)
	                     .IsChecked_Lambda(
	                         [WeakThis]()
	                         {
		                         const UConstructVoiceComponent* Voice =
		                             WeakThis.IsValid() ? WeakThis->FindVoiceComponent() : nullptr;
		                         return Voice && Voice->IsVoiceAvailable() && Voice->IsPushToTalkEnabled()
		                                    ? ECheckBoxState::Checked
		                                    : ECheckBoxState::Unchecked;
	                         })
	                     .OnCheckStateChanged_Lambda(
	                         [WeakThis](ECheckBoxState State)
	                         {
		                         if (UConstructVoiceComponent* Voice =
		                                 WeakThis.IsValid() ? WeakThis->FindVoiceComponent() : nullptr)
		                         {
			                         Voice->SetPushToTalkEnabled(State == ECheckBoxState::Checked);
			                         WeakThis->RequestGameplayFocusRestore();
		                         }
	                         })[SNew(STextBlock)
	                                .Text(LOCTEXT("PTT", "Enable push-to-talk"))
	                                .TextStyle(&Style, "Construct.Text.Heading")]];
	Content->AddSlot().AutoHeight().Padding(
	    0, 0, 0, 20)[SNew(SCheckBox)
	                     .IsChecked_Lambda(
	                         [WeakThis]()
	                         {
		                         const UConstructVoiceComponent* Voice =
		                             WeakThis.IsValid() ? WeakThis->FindVoiceComponent() : nullptr;
		                         return Voice && Voice->IsListeningEnabled() ? ECheckBoxState::Checked
		                                                                     : ECheckBoxState::Unchecked;
	                         })
	                     .OnCheckStateChanged_Lambda(
	                         [WeakThis](ECheckBoxState State)
	                         {
		                         if (UConstructVoiceComponent* Voice =
		                                 WeakThis.IsValid() ? WeakThis->FindVoiceComponent() : nullptr)
		                         {
			                         Voice->SetListeningEnabled(State == ECheckBoxState::Checked);
			                         WeakThis->RequestGameplayFocusRestore();
		                         }
	                         })[SNew(STextBlock)
	                                .Text(LOCTEXT("Listen", "Hear incoming voice"))
	                                .TextStyle(&Style, "Construct.Text.Heading")]];
	Content->AddSlot().AutoHeight().Padding(
	    0, 0, 0, 18)[SNew(SButton)
	                     .ButtonStyle(&Style, "Construct.Button.Secondary")
	                     .ContentPadding(FMargin(18, 10))
	                     .OnClicked_Lambda(
	                         [WeakThis]()
	                         {
		                         if (UConstructVoiceComponent* Voice =
		                                 WeakThis.IsValid() ? WeakThis->FindVoiceComponent() : nullptr)
		                         {
			                         Voice->StopTalking();
			                         WeakThis->RequestGameplayFocusRestore();
		                         }
		                         return FReply::Handled();
	                         })[SNew(STextBlock)
	                                .Text(LOCTEXT("Stop", "Stop microphone now"))
	                                .TextStyle(&Style, "Construct.Text.Body")]];
	Content->AddSlot().AutoHeight().Padding(
	    0, 0, 0, 20)[SNew(STextBlock)
	                     .Text_Lambda(
	                         [WeakThis]()
	                         {
		                         const UConstructVoiceComponent* Voice =
		                             WeakThis.IsValid() ? WeakThis->FindVoiceComponent() : nullptr;
		                         return Voice ? Voice->GetStatusText()
		                                      : LOCTEXT("Missing", "Voice is unavailable for this controller.");
	                         })
	                     .TextStyle(&Style, "Construct.Text.Body")
	                     .AutoWrapText(true)];
	Content->AddSlot().AutoHeight()
	    [SNew(STextBlock)
	         .Text_Lambda(
	             [WeakThis]()
	             {
		             const UConstructVoiceComponent* Voice =
		                 WeakThis.IsValid() ? WeakThis->FindVoiceComponent() : nullptr;
		             return FText::Format(
		                 LOCTEXT(
		                     "Instructions",
		                     "Hold {0} during gameplay to talk; release to stop. Menus, focus loss and sign-out stop the microphone.\n\nVoice availability depends on app and microphone permissions."),
		                 Voice ? Voice->PushToTalkKey.GetDisplayName() : FText::FromString(TEXT("V")));
	             })
	         .TextStyle(&Style, "Construct.Text.Muted")
	         .AutoWrapText(true)];
	return SNew(SOverlay) +
	       SOverlay::Slot()
	           .HAlign(HAlign_Right)
	           .VAlign(VAlign_Top)
	           .Padding(FMargin(24, 72, 24, 24))[SNew(SBorder)
	                                                 .BorderImage(Style.GetBrush("Construct.Panel"))
	                                                 .Padding(18)[SNew(SBox).WidthOverride(360)[Content]]];
}

#undef LOCTEXT_NAMESPACE
