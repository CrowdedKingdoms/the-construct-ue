#include "ConstructModScriptPanel.h"

#include "ConstructModScriptInput.h"
#include "ConstructShellStyle.h"
#include "ConstructUIInputSubsystem.h"
#include "SConstructChoiceList.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Input/Reply.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructModScripts"

namespace
{
const FName ConsentModal(TEXT("ConstructModScriptConsent"));
const FName ListModal(TEXT("ConstructModScriptList"));

TSharedRef<STextBlock> Text(const FText& Value, const TCHAR* StyleName = TEXT("Construct.Text.Body"))
{
    return SNew(STextBlock).Text(Value).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>(StyleName)).WrapTextAt(600).AutoWrapText(true);
}

TSharedRef<SBorder> Frame(TSharedRef<SWidget> Content)
{
    return SNew(SBorder).BorderImage(FConstructShellStyle::Get().GetBrush("Construct.Panel")).Padding(24)[Content];
}

TSharedRef<SWidget> Notice(TWeakPtr<FConstructModScriptPresentation> Source)
{
    return SNew(STextBlock)
        .TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Construct.Text.Error")))
        .AutoWrapText(true)
        .Text_Lambda([Source]()
        {
            const TSharedPtr<FConstructModScriptPresentation> Model = Source.Pin();
            return Model ? Model->GetNotice() : FText::GetEmpty();
        })
        .Visibility_Lambda([Source]()
        {
            const TSharedPtr<FConstructModScriptPresentation> Model = Source.Pin();
            return Model && !Model->GetNotice().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
        });
}
}

void UConstructModScriptPanel::SetPresentation(TSharedRef<FConstructModScriptPresentation> Source)
{
    const bool bWasConstructed = bConstructed;
    bConstructed = false;
    if (Presentation)
    {
        Presentation->OnChanged.Remove(ChangedHandle);
        Presentation->OnHudChanged.Remove(HudChangedHandle);
    }
    ReleaseModal();
    bListOpen = false;
    Presentation = Source;
    ChangedHandle = Presentation->OnChanged.AddUObject(this, &ThisClass::RefreshPresentation);
    HudChangedHandle = Presentation->OnHudChanged.AddUObject(this, &ThisClass::RefreshHud);
    bConstructed = bWasConstructed;
    RefreshPresentation();
}

TSharedRef<SWidget> UConstructModScriptPanel::RebuildWidget()
{
    SAssignNew(Root, SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24, 84, 24, 24)
        [
            SAssignNew(HudBox, SBox).WidthOverride(350).MaxDesiredHeight(380)
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24)
        [
            SAssignNew(DialogBox, SBox).WidthOverride(660).MaxDesiredHeight(650)
        ];
    return Root.ToSharedRef();
}

void UConstructModScriptPanel::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    bConstructed = true;
    if (ULocalPlayer* Local = GetOwningLocalPlayer())
    {
        ModalOwner = Local->GetSubsystem<UConstructUIInputSubsystem>();
        if (UConstructUIInputSubsystem* Owner = ModalOwner.Get())
        {
            Owner->OnGameplayInputChanged.AddUniqueDynamic(this, &ThisClass::GameplayAdmissionChanged);
        }
    }
    if (APlayerController* Controller = GetOwningPlayer(); Controller && Controller->IsLocalController() && !PointerInput)
    {
        PointerInput = NewObject<UConstructModScriptInput>(Controller);
        PointerInput->OnToggleList.BindUObject(this, &ThisClass::ToggleRunningList);
        PointerInput->Attach(Controller);
    }
    RefreshPresentation();
}

bool UConstructModScriptPanel::CanNavigate() const
{
    const UConstructUIInputSubsystem* Owner = ModalOwner.Get();
    return Owner && Owner->CanUseNavigationInput(ListModal, ConsentModal);
}

void UConstructModScriptPanel::ToggleRunningList()
{
    if (Presentation && !Presentation->GetPrompt() && CanNavigate())
    {
        bListOpen = !bListOpen;
        RefreshPresentation();
    }
}

void UConstructModScriptPanel::CloseRunningList()
{
    bListOpen = false;
    RefreshPresentation();
}

void UConstructModScriptPanel::ReleaseModal()
{
    if (UConstructUIInputSubsystem* Owner = ModalOwner.Get())
    {
        Owner->SetModal(ConsentModal, false);
        Owner->SetModal(ListModal, false);
    }
}

void UConstructModScriptPanel::UpdateModal()
{
    if (!bConstructed)
    {
        return;
    }
    if (UConstructUIInputSubsystem* Owner = ModalOwner.Get())
    {
        if (bPresentingConsent)
        {
            // Acquire consent before releasing the list so input never briefly returns to gameplay.
            Owner->SetModal(ConsentModal, true, this);
            Owner->SetModal(ListModal, false);
        }
        else
        {
            if (bPresentingList)
            {
                Owner->SetModal(ListModal, true, this);
            }
            Owner->SetModal(ConsentModal, false);
            if (!bPresentingList)
            {
                Owner->SetModal(ListModal, false);
            }
        }
    }
}

void UConstructModScriptPanel::GameplayAdmissionChanged(bool bAllowed)
{
    if (bAllowed && bConstructed)
    {
        RefreshPresentation();
    }
}

void UConstructModScriptPanel::RefreshPresentation()
{
    if (!HudBox || !DialogBox || !Presentation)
    {
        return;
    }
    RefreshHud();
    const FConstructScriptConsent* Prompt = Presentation->GetPrompt();
    bPresentingConsent = Prompt && CanNavigate();
    bPresentingList = !Prompt && bListOpen && CanNavigate();
    if (bPresentingConsent)
    {
        DialogBox->SetContent(MakeConsent(*Prompt));
        DialogBox->SetVisibility(EVisibility::SelfHitTestInvisible);
    }
    else if (bPresentingList)
    {
        ConsentChoices.Reset();
        DialogBox->SetContent(MakeRunningList());
        DialogBox->SetVisibility(EVisibility::SelfHitTestInvisible);
    }
    else
    {
        ConsentChoices.Reset();
        DialogBox->SetVisibility(EVisibility::Collapsed);
    }
    UpdateModal();
}

void UConstructModScriptPanel::RefreshHud()
{
    if (HudBox && Presentation)
    {
        HudBox->SetVisibility(EVisibility::HitTestInvisible);
        HudBox->SetContent(MakeHud());
    }
}

TSharedRef<SWidget> UConstructModScriptPanel::MakeHud()
{
    TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
    if (!Presentation->GetContext().IsEmpty())
    {
        Cards->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Text(Presentation->GetContext(), TEXT("Construct.Text.Small"))];
    }
    if (!Presentation->GetNotice().IsEmpty())
    {
        Cards->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Frame(Text(Presentation->GetNotice(), TEXT("Construct.Text.Error")))];
    }
    for (const FConstructScriptCard& Card : Presentation->GetCards())
    {
        if (!Card.bRunning || Card.Text.IsEmpty())
        {
            continue;
        }
        Cards->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SBorder).BorderImage(FConstructShellStyle::Get().GetBrush("Construct.Panel")).Padding(14)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                [Text(FText::FromString(Card.Label.Name), TEXT("Construct.Text.Heading"))]
                + SVerticalBox::Slot().AutoHeight()[Text(FText::FromString(Card.Text))]
            ]
        ];
    }
    return SNew(SScrollBox) + SScrollBox::Slot()[Cards];
}

TSharedRef<SWidget> UConstructModScriptPanel::MakeConsent(const FConstructScriptConsent& Prompt)
{
    TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);
    const auto Add = [&Details](const FText& Value, const TCHAR* StyleName = TEXT("Construct.Text.Body"))
    {
        Details->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Text(Value, StyleName)];
    };
    Add(LOCTEXT("ConsentTitle", "Run Mod Scripts here?"), TEXT("Construct.Text.Title"));
    Add(Prompt.Author, TEXT("Construct.Text.Heading"));
    Add(Prompt.Scripts);
    Add(LOCTEXT("ConsentExplanation", "These scripts run on your computer while you visit this land. Choose whether to trust this author for the capabilities below."), TEXT("Construct.Text.Muted"));
    for (const FText& Group : Prompt.Groups)
    {
        Add(Group, TEXT("Construct.Text.Heading"));
    }
    for (const FText& Call : Prompt.Calls)
    {
        Add(Call);
    }
    if (!Prompt.NotOffered.IsEmpty())
    {
        Add(LOCTEXT("NotOffered", "This game does not allow:"), TEXT("Construct.Text.Heading"));
        for (const FText& Call : Prompt.NotOffered)
        {
            Add(Call, TEXT("Construct.Text.Muted"));
        }
    }
    Details->AddSlot().AutoHeight()[Notice(Presentation)];
    if (Prompt.bPending)
    {
        Add(LOCTEXT("ConsentPending", "Waiting for a response..."), TEXT("Construct.Text.Muted"));
    }
    const TWeakObjectPtr<UConstructModScriptPanel> WeakThis(this);
    const TWeakPtr<FConstructModScriptPresentation> BoundSource(Presentation);
    const FName PreviousSelection = ConsentChoices && ConsentChoiceRequest == Prompt.RequestId && ConsentChoiceSource.Pin() == Presentation ? ConsentChoices->GetSelection() : NAME_None;
    ConsentChoiceRequest = Prompt.RequestId;
    ConsentChoiceSource = Presentation;
    const TArray<FConstructChoiceOption> Options = {
        {TEXT("Run"), LOCTEXT("Run", "Run"), LOCTEXT("RunDescription", "Trust this author here and allow the capabilities listed above.")},
        {TEXT("NotNow"), LOCTEXT("NotNow", "Not now"), LOCTEXT("NotNowDescription", "Keep playing without running these scripts on this visit.")}
    };
    SAssignNew(ConsentChoices, SConstructChoiceList)
        .Options(Options)
        .InitiallySelected(PreviousSelection)
        .CanConfirm_Lambda([WeakThis, BoundSource, Id = Prompt.RequestId]()
        {
            const UConstructModScriptPanel* Panel = WeakThis.Get();
            const FConstructScriptConsent* Current = Panel && Panel->Presentation ? Panel->Presentation->GetPrompt() : nullptr;
            return Panel && Panel->Presentation == BoundSource.Pin() && Panel->CanNavigate() && Current && Current->RequestId == Id && !Current->bPending;
        })
        .OnConfirmed_Lambda([WeakThis, BoundSource, Id = Prompt.RequestId](FName Choice)
        {
            if (UConstructModScriptPanel* Panel = WeakThis.Get(); Panel && Panel->Presentation == BoundSource.Pin() && Panel->CanNavigate() && Panel->Presentation)
            {
                if (Choice == TEXT("Run") || Choice == TEXT("NotNow"))
                {
                    Panel->Presentation->Answer(Id, Choice == TEXT("Run"));
                }
            }
        });
    return Frame(SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Details]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[ConsentChoices.ToSharedRef()]);
}



TSharedRef<SWidget> UConstructModScriptPanel::MakeRunningList()
{
    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
    if (Presentation->GetCards().IsEmpty())
    {
        Rows->AddSlot().AutoHeight()[Text(LOCTEXT("NoScripts", "No Mod Scripts are running on this land."), TEXT("Construct.Text.Muted"))];
    }
    const TWeakObjectPtr<UConstructModScriptPanel> WeakThis(this);
    const TWeakPtr<FConstructModScriptPresentation> BoundSource(Presentation);
    for (const FConstructScriptCard& Card : Presentation->GetCards())
    {
        const FString Source = Card.Label.Source;
        const int64 Generation = Card.Label.Generation;
        const int64 Author = Card.Label.AuthorId;
        const auto CanAct = [WeakThis, BoundSource, Source, Generation]()
        {
            const UConstructModScriptPanel* Panel = WeakThis.Get();
            const FConstructScriptCard* Current = Panel && Panel->Presentation ? Panel->Presentation->Find(Source) : nullptr;
            return Panel && Panel->Presentation == BoundSource.Pin() && Panel->CanNavigate() && Current && Current->Label.Generation == Generation && Current->bRunning && !Current->bPending;
        };
        Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 14)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Text(FText::FromString(Card.Label.Name), TEXT("Construct.Text.Heading"))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 8)
            [Text(Card.bRunning ? (Card.bPending ? LOCTEXT("Updating", "Updating...") : LOCTEXT("Running", "Running")) : Card.StopReason, TEXT("Construct.Text.Muted"))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(STextBlock).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
                .WrapTextAt(600).AutoWrapText(true)
                .Text_Lambda([WeakThis, BoundSource, Source, Generation]()
                {
                    const UConstructModScriptPanel* Panel = WeakThis.Get();
                    const FConstructScriptCard* Current = Panel && Panel->Presentation == BoundSource.Pin() && Panel->Presentation ? Panel->Presentation->Find(Source) : nullptr;
                    return Current && Current->bRunning && Current->Label.Generation == Generation ? FText::FromString(Current->Text) : FText::GetEmpty();
                })
                .Visibility_Lambda([WeakThis, BoundSource, Source, Generation]()
                {
                    const UConstructModScriptPanel* Panel = WeakThis.Get();
                    const FConstructScriptCard* Current = Panel && Panel->Presentation == BoundSource.Pin() && Panel->Presentation ? Panel->Presentation->Find(Source) : nullptr;
                    return Current && Current->bRunning && Current->Label.Generation == Generation && !Current->Text.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                })
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [
                    SNew(SButton).ButtonStyle(&FConstructShellStyle::Get().GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                    .ContentPadding(FMargin(12, 7)).IsEnabled_Lambda(CanAct)
                    .OnClicked_Lambda([WeakThis, Source, CanAct]()
                    {
                        if (CanAct())
                        {
                            WeakThis.Get()->Presentation->Revoke(Source);
                        }
                        return FReply::Handled();
                    })[Text(LOCTEXT("Revoke", "Revoke"))]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton).ButtonStyle(&FConstructShellStyle::Get().GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                    .ContentPadding(FMargin(12, 7)).IsEnabled_Lambda([CanAct, Author]() { return Author > 0 && CanAct(); })
                    .OnClicked_Lambda([WeakThis, Author, CanAct]()
                    {
                        if (CanAct())
                        {
                            WeakThis.Get()->Presentation->ForgetAuthor(Author);
                        }
                        return FReply::Handled();
                    })[Text(LOCTEXT("Forget", "Forget author"))]
                ]
            ]
        ];
    }
    Rows->AddSlot().AutoHeight()[Notice(Presentation)];
    return Frame(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[Text(LOCTEXT("RunningTitle", "Mod Scripts"), TEXT("Construct.Text.Title"))]
        + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Rows]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
        [
            SNew(SButton).ButtonStyle(&FConstructShellStyle::Get().GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
            .ContentPadding(FMargin(12, 8)).OnClicked_Lambda([WeakThis]()
            {
                if (UConstructModScriptPanel* Panel = WeakThis.Get())
                {
                    Panel->CloseRunningList();
                }
                return FReply::Handled();
            })[Text(LOCTEXT("CloseList", "Close  [F7]"))]
        ]);
}

FReply UConstructModScriptPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if ((Event.GetKey() == EKeys::F7 || Event.GetKey() == EKeys::Escape) && bListOpen && CanNavigate() && Presentation && !Presentation->GetPrompt())
    {
        CloseRunningList();
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UConstructModScriptPanel::Shutdown()
{
    bConstructed = false;
    if (UConstructUIInputSubsystem* Owner = ModalOwner.Get())
    {
        Owner->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::GameplayAdmissionChanged);
    }
    ReleaseModal();
    ModalOwner.Reset();
    if (PointerInput)
    {
        PointerInput->OnToggleList.Unbind();
        PointerInput->Detach();
        PointerInput->DestroyComponent();
        PointerInput = nullptr;
    }
    if (Presentation)
    {
        Presentation->OnChanged.Remove(ChangedHandle);
        Presentation->OnHudChanged.Remove(HudChangedHandle);
    }
    Presentation.Reset();
    ConsentChoices.Reset();
    ConsentChoiceSource.Reset();
    bListOpen = false;
    bPresentingConsent = false;
    bPresentingList = false;
}

void UConstructModScriptPanel::NativeDestruct()
{
    Shutdown();
    Super::NativeDestruct();
}

void UConstructModScriptPanel::ReleaseSlateResources(bool bReleaseChildren)
{
    Root.Reset();
    HudBox.Reset();
    DialogBox.Reset();
    ConsentChoices.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

#undef LOCTEXT_NAMESPACE
