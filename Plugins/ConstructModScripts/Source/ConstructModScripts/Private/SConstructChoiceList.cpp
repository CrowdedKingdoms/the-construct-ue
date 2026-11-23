#include "SConstructChoiceList.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "ConstructShellStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructChoices"

namespace
{
const FSlateBrush* CardBrush(bool bSelected)
{
    static const FSlateRoundedBoxBrush Normal(FLinearColor::FromSRGBColor(FColor(25, 25, 27)), 6.0f, FLinearColor::FromSRGBColor(FColor(68, 68, 72)), 1.0f);
    static const FSlateRoundedBoxBrush Selected(FLinearColor::FromSRGBColor(FColor(33, 31, 23)), 6.0f, FConstructShellStyle::Gold(), 1.0f);
    return bSelected ? &Selected : &Normal;
}

const FSlateBrush* RadioBrush(bool bSelected)
{
    static const FSlateRoundedBoxBrush Normal(FLinearColor::Transparent, 8.0f, FConstructShellStyle::Muted(), 1.25f);
    static const FSlateRoundedBoxBrush Selected(FLinearColor::Transparent, 8.0f, FConstructShellStyle::Gold(), 1.25f);
    return bSelected ? &Selected : &Normal;
}

const FSlateBrush* RadioDot()
{
    static const FSlateRoundedBoxBrush Dot(FConstructShellStyle::Gold(), 3.0f);
    return &Dot;
}
}

void SConstructChoiceList::Construct(const FArguments& Args)
{
    CanConfirm = Args._CanConfirm;
    OnConfirmed = Args._OnConfirmed;
    for (const FConstructChoiceOption& Option : Args._Options)
    {
        if (!Option.Key.IsNone() && Options.Num() < 32 && !Options.ContainsByPredicate([&Option](const FConstructChoiceOption& Existing) { return Existing.Key == Option.Key; }))
        {
            Options.Add(Option);
        }
    }
    Select(Args._InitiallySelected);
    TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
    const TWeakPtr<SConstructChoiceList> WeakThis = SharedThis(this);
    for (const FConstructChoiceOption& Option : Options)
    {
        Cards->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SButton)
            .ButtonStyle(&FConstructShellStyle::Get().GetWidgetStyle<FButtonStyle>("Construct.Button.Ghost"))
            .ContentPadding(0)
            .IsEnabled_Lambda([WeakThis, bEnabled = Option.bEnabled]()
            {
                const TSharedPtr<SConstructChoiceList> Self = WeakThis.Pin();
                return Self && bEnabled && Self->CanConfirm.Get();
            })
            .OnClicked_Lambda([WeakThis, Key = Option.Key]()
            {
                if (const TSharedPtr<SConstructChoiceList> Self = WeakThis.Pin())
                {
                    Self->Select(Key);
                }
                return FReply::Handled();
            })
            [
                SNew(SBorder).Padding(FMargin(14, 12))
                .BorderImage_Lambda([WeakThis, Key = Option.Key]()
                {
                    const TSharedPtr<SConstructChoiceList> Self = WeakThis.Pin();
                    return CardBrush(Self && Self->Selection == Key);
                })
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 2, 12, 0)
                    [
                        SNew(SBox).WidthOverride(16).HeightOverride(16).Visibility(EVisibility::HitTestInvisible)
                        [
                            SNew(SOverlay)
                            + SOverlay::Slot()
                            [
                                SNew(SBorder).Padding(0).BorderImage_Lambda([WeakThis, Key = Option.Key]()
                                {
                                    const TSharedPtr<SConstructChoiceList> Self = WeakThis.Pin();
                                    return RadioBrush(Self && Self->Selection == Key);
                                })
                            ]
                            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                            [
                                SNew(SBox).WidthOverride(6).HeightOverride(6)
                                .Visibility_Lambda([WeakThis, Key = Option.Key]()
                                {
                                    const TSharedPtr<SConstructChoiceList> Self = WeakThis.Pin();
                                    return Self && Self->Selection == Key ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                                })
                                [SNew(SBorder).Padding(0).BorderImage(RadioDot())]
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock).Text(Option.Title).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading"))
                            .WrapTextAt(540).AutoWrapText(true)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                        [
                            SNew(STextBlock).Text(Option.Description).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted"))
                            .WrapTextAt(540).AutoWrapText(true)
                        ]
                    ]
                ]
            ]
        ];
    }
    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SBox).MaxDesiredHeight(280)[SNew(SScrollBox) + SScrollBox::Slot()[Cards]]];
    Rows->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
    [
        SNew(SButton).ButtonStyle(&FConstructShellStyle::Get().GetWidgetStyle<FButtonStyle>("Construct.Button.Primary"))
        .ContentPadding(FMargin(18, 10)).HAlign(HAlign_Center)
        .IsEnabled_Lambda([WeakThis]()
        {
            const TSharedPtr<SConstructChoiceList> Self = WeakThis.Pin();
            return Self && Self->CanConfirm.Get() && !Self->Selection.IsNone() && Self->OnConfirmed.IsBound();
        })
        .OnClicked_Lambda([WeakThis]()
        {
            if (const TSharedPtr<SConstructChoiceList> Self = WeakThis.Pin())
            {
                Self->Confirm();
            }
            return FReply::Handled();
        })
        [SNew(STextBlock).Text(LOCTEXT("Confirm", "Confirm choice")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12)).ColorAndOpacity(FLinearColor::Black)]
    ];
    ChildSlot[Rows];
}

bool SConstructChoiceList::Select(FName Key)
{
    if (!CanConfirm.Get() || !Options.ContainsByPredicate([Key](const FConstructChoiceOption& Option) { return Option.Key == Key && Option.bEnabled; }))
    {
        return false;
    }
    Selection = Key;
    return true;
}

bool SConstructChoiceList::Confirm()
{
    if (!CanConfirm.Get() || Selection.IsNone() || !OnConfirmed.IsBound())
    {
        return false;
    }
    OnConfirmed.Execute(Selection);
    return true;
}

#undef LOCTEXT_NAMESPACE
