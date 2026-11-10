#include "ConstructChatComponent.h"

#include "ConstructShellStyle.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructChat"

UConstructChatComponent* UConstructChatPanel::Chat() const
{
	return GetOwningPlayer() ? GetOwningPlayer()->FindComponentByClass<UConstructChatComponent>() : nullptr;
}

void UConstructChatPanel::ClearComposer()
{
	if (Composer)
	{
		Composer->SetText(FText::GetEmpty());
	}
}

FReply UConstructChatPanel::NativeOnFocusReceived(const FGeometry& Geometry, const FFocusEvent& FocusEvent)
{
	if (Composer && Chat() && Chat()->IsComposerOpen())
	{
		return FReply::Handled().SetUserFocus(Composer.ToSharedRef(), FocusEvent.GetCause());
	}
	return Super::NativeOnFocusReceived(Geometry, FocusEvent);
}

FReply UConstructChatPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (UConstructChatComponent* Component = Chat(); Component && Component->IsComposerOpen())
	{
		if (KeyEvent.GetKey() == EKeys::Escape)
		{
			Component->CloseComposer();
			return FReply::Handled();
		}
		if (KeyEvent.GetKey() == EKeys::F3)
		{
			if (!KeyEvent.IsRepeat())
			{
				Component->TogglePanel();
			}
			return FReply::Handled();
		}
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

void UConstructChatPanel::ReleaseSlateResources(bool bReleaseChildren)
{
	Composer.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

TSharedRef<SWidget> UConstructChatPanel::RebuildWidget()
{
	const ISlateStyle& Style = FConstructShellStyle::Get();
	const TWeakObjectPtr<UConstructChatPanel> WeakThis(this);
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	Content->AddSlot().AutoHeight()[SNew(STextBlock)
	                                    .Text_Lambda(
	                                        [WeakThis]()
	                                        {
		                                        const UConstructChatComponent* Component =
		                                            WeakThis.IsValid() ? WeakThis->Chat() : nullptr;
		                                        return Component && Component->IsChannelMode()
		                                                   ? LOCTEXT("ChannelTitle", "Channel chat")
		                                                   : LOCTEXT("NearbyTitle", "Nearby chat");
	                                        })
	                                    .TextStyle(&Style, "Construct.Text.Heading")];
	Content->AddSlot().AutoHeight().Padding(
	    0, 8)[SNew(STextBlock)
	              .AutoWrapText(true)
	              .TextStyle(&Style, "Construct.Text.Small")
	              .Text_Lambda(
	                  [WeakThis]()
	                  {
		                  const UConstructChatComponent* Component = WeakThis.IsValid() ? WeakThis->Chat() : nullptr;
		                  return Component ? Component->GetStatus() : FText::GetEmpty();
	                  })];
	Content->AddSlot()
	    .AutoHeight()[SNew(SHorizontalBox) +
	                  SHorizontalBox::Slot().AutoWidth().Padding(
	                      0, 0, 8, 0)[SNew(SButton)
	                                      .ButtonStyle(&Style, "Construct.Button.Secondary")
	                                      .ContentPadding(FMargin(10, 6))
	                                      .OnClicked_Lambda(
	                                          [WeakThis]()
	                                          {
		                                          if (UConstructChatComponent* Component =
		                                                  WeakThis.IsValid() ? WeakThis->Chat() : nullptr)
		                                          {
			                                          if (Component->IsChannelMode())
			                                          {
				                                          Component->UseNearbyChat();
			                                          }
			                                          else
			                                          {
				                                          Component->EnableChannelChat();
			                                          }
			                                          Component->RequestGameplayFocusRestore();
		                                          }
		                                          return FReply::Handled();
	                                          })[SNew(STextBlock)
	                                                 .Text_Lambda(
	                                                     [WeakThis]()
	                                                     {
		                                                     const UConstructChatComponent* Component =
		                                                         WeakThis.IsValid() ? WeakThis->Chat() : nullptr;
		                                                     return Component && Component->IsChannelMode()
		                                                                ? LOCTEXT("NearbyChoice", "Use nearby chat")
		                                                                : LOCTEXT("ChannelChoice", "Use channel chat");
	                                                     })
	                                                 .TextStyle(&Style, "Construct.Text.Small")]] +
	                  SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	                                                         .ButtonStyle(&Style, "Construct.Button.Secondary")
	                                                         .ContentPadding(FMargin(10, 6))
	                                                         .IsEnabled_Lambda(
	                                                             [WeakThis]()
	                                                             {
		                                                             const UConstructChatComponent* Component =
		                                                                 WeakThis.IsValid() ? WeakThis->Chat()
		                                                                                    : nullptr;
		                                                             return Component && Component->IsChannelMode();
	                                                             })
	                                                         .OnClicked_Lambda(
	                                                             [WeakThis]()
	                                                             {
		                                                             if (UConstructChatComponent* Component =
		                                                                     WeakThis.IsValid() ? WeakThis->Chat()
		                                                                                        : nullptr)
		                                                             {
			                                                             Component->RefreshChannel();
			                                                             Component->RequestGameplayFocusRestore();
		                                                             }
		                                                             return FReply::Handled();
	                                                             })[SNew(STextBlock)
	                                                                    .Text(LOCTEXT("Refresh", "Refresh"))
	                                                                    .TextStyle(&Style, "Construct.Text.Small")]]];
	Content->AddSlot().AutoHeight().Padding(0, 10)[SNew(SBox).HeightOverride(
	    150)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock)
	                                                   .AutoWrapText(true)
	                                                   .WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
	                                                   .TextStyle(&Style, "Construct.Text.Body")
	                                                   .Text_Lambda(
	                                                       [WeakThis]()
	                                                       {
		                                                       const UConstructChatComponent* Component =
		                                                           WeakThis.IsValid() ? WeakThis->Chat() : nullptr;
		                                                       return Component ? Component->GetHistory()
		                                                                        : FText::GetEmpty();
	                                                       })]]];
	Content->AddSlot().AutoHeight()
	    [SNew(SHorizontalBox)
	         .Visibility_Lambda(
	             [WeakThis]()
	             {
		             const UConstructChatComponent* Component = WeakThis.IsValid() ? WeakThis->Chat() : nullptr;
		             return Component && Component->IsComposerOpen() ? EVisibility::Visible : EVisibility::Collapsed;
	             }) +
	     SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Composer, SEditableTextBox)
	                                             .Style(&Style, "Construct.Input")
	                                             .HintText(LOCTEXT("Hint", "Message (240 characters)"))
	                                             .RevertTextOnEscape(false)
	                                             .OnTextChanged_Lambda(
	                                                 [WeakThis](const FText& Text)
	                                                 {
		                                                 if (WeakThis.IsValid() && WeakThis->Composer &&
		                                                     Text.ToString().Len() > 240)
		                                                 {
			                                                 WeakThis->Composer->SetText(FText::FromString(
			                                                     UConstructChatComponent::SanitizeOutgoing(
			                                                         Text.ToString())));
		                                                 }
	                                                 })
	                                             .OnTextCommitted_Lambda(
	                                                 [WeakThis](const FText& Text, ETextCommit::Type Commit)
	                                                 {
		                                                 if (Commit == ETextCommit::OnEnter)
		                                                 {
			                                                 if (UConstructChatComponent* Component =
			                                                         WeakThis.IsValid() ? WeakThis->Chat() : nullptr)
			                                                 {
				                                                 Component->SubmitComposer(Text.ToString());
			                                                 }
		                                                 }
	                                                 })] +
	     SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
	         [SNew(SButton)
	              .ButtonStyle(&Style, "Construct.Button.Primary")
	              .ContentPadding(FMargin(10, 6))
	              .IsEnabled_Lambda(
	                  [WeakThis]()
	                  {
		                  const UConstructChatComponent* Component = WeakThis.IsValid() ? WeakThis->Chat() : nullptr;
		                  return Component && Component->CanSend();
	                  })
	              .OnClicked_Lambda(
	                  [WeakThis]()
	                  {
		                  if (UConstructChatComponent* Component = WeakThis.IsValid() ? WeakThis->Chat() : nullptr)
		                  {
			                  Component->SubmitComposer(WeakThis->Composer ? WeakThis->Composer->GetText().ToString()
			                                                               : FString());
		                  }
		                  return FReply::Handled();
	                  })[SNew(STextBlock).Text(LOCTEXT("Send", "Send")).TextStyle(&Style, "Construct.Text.Small")]]];
	Content->AddSlot().AutoHeight().Padding(
	    0, 6, 0, 0)[SNew(STextBlock)
	                    .Text(LOCTEXT("Controls", "Enter: type / send    Esc: cancel    F3: hide chat"))
	                    .TextStyle(&Style, "Construct.Text.Small")];
	return SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible) +
	       SOverlay::Slot()
	           .HAlign(HAlign_Left)
	           .VAlign(VAlign_Top)
	           .Padding(FMargin(20))
	           [SNew(SBox).WidthOverride(430)
	               [SNew(SBorder)
	                    .BorderImage(Style.GetBrush("Construct.Panel"))
	                    .Padding(16)
	                    .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent& Event)
	                    {
		                    if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
		                    {
			                    return FReply::Handled();
		                    }
		                    return FReply::Unhandled();
	                    })[Content]]];
}

#undef LOCTEXT_NAMESPACE
