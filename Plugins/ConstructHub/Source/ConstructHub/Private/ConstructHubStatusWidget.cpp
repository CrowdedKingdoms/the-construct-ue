#include "ConstructHubStatusWidget.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "ConstructShellStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructHubStatusWidget"

void UConstructHubStatusWidget::SetHubStatus(TOptional<int64> Pulses, TOptional<int64> Level)
{
    bConnected = Pulses.IsSet() && Level.IsSet();
    StatusText = bConnected
        ? FText::Format(LOCTEXT("Ready", "World pulse {0}, level {1}"), FText::AsNumber(Pulses.GetValue()), FText::AsNumber(Level.GetValue()))
        : LOCTEXT("Disconnected", "World hub not connected");
    UpdatePresentation();
}

TSharedRef<SWidget> UConstructHubStatusWidget::RebuildWidget()
{
    const ISlateStyle& Style = FConstructShellStyle::Get();
    if (StatusText.IsEmpty())
    {
        SetHubStatus({}, {});
    }

    TSharedRef<SWidget> Content =
        SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24.0f)
        [
            SNew(SBox).MinDesiredWidth(204.0f).MinDesiredHeight(35.0f)
            [
                SNew(SBorder).BorderImage(Style.GetBrush("Construct.Panel")).Padding(FMargin(12.0f, 6.0f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 9.0f, 0.0f)
                    [
                        SNew(SBox).WidthOverride(7.0f).HeightOverride(7.0f)
                        [
                            SAssignNew(Indicator, SBorder).Padding(0.0f)
                        ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SAssignNew(Label, STextBlock).TextStyle(&Style, "Construct.Text.Body")
                    ]
                ]
            ]
        ];
    UpdatePresentation();
    return Content;
}

void UConstructHubStatusWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    Indicator.Reset();
    Label.Reset();
}

void UConstructHubStatusWidget::UpdatePresentation()
{
    if (Indicator)
    {
        static const FSlateRoundedBoxBrush ReadyDot(FConstructShellStyle::Gold(), 3.5f);
        static const FSlateRoundedBoxBrush OfflineDot(FLinearColor::Transparent, 3.5f, FConstructShellStyle::Muted(), 1.0f);
        Indicator->SetBorderImage(bConnected ? &ReadyDot : &OfflineDot);
    }
    if (Label)
    {
        Label->SetText(StatusText);
        Label->SetColorAndOpacity(bConnected ? FConstructShellStyle::Ink() : FConstructShellStyle::Muted());
    }
}

#undef LOCTEXT_NAMESPACE
