#include "ConstructWebcamFeedWidget.h"

#include "ConstructPresencePawn.h"
#include "ConstructShellStyle.h"
#include "ConstructWebcamPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructWebcamFeedWidget"

void UConstructWebcamFeedWidget::BindFeed(UConstructWebcamPanel* InPanel, AConstructPresencePawn* InPawn)
{
    Panel = InPanel;
    Pawn = InPawn;
}

void UConstructWebcamFeedWidget::SetFeed(const FConstructWebcamFeed& Feed)
{
    Brush.SetResourceObject(Feed.Texture.Get());
    Brush.DrawAs = ESlateBrushDrawType::Image;
    Brush.ImageSize = FVector2D(128, 96);
}

TSharedRef<SWidget> UConstructWebcamFeedWidget::RebuildWidget()
{
    const TWeakObjectPtr<UConstructWebcamFeedWidget> WeakWidget(this);
    const ISlateStyle& Style = FConstructShellStyle::Get();
    const auto CanModerate = [WeakWidget]()
    {
        const UConstructWebcamFeedWidget* Widget = WeakWidget.Get();
        const UConstructWebcamPanel* Owner = Widget ? Widget->Panel.Get() : nullptr;
        return Owner && Owner->CanModerate(Widget->Pawn.Get()) && !Owner->IsBusy();
    };
    return SNew(SBox).WidthOverride(160).HeightOverride(144)
        .Visibility_Lambda([WeakWidget]()
        {
            const UConstructWebcamFeedWidget* Widget = WeakWidget.Get();
            return Widget && Widget->HasPicture() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        })
        [
            SNew(SBorder).BorderImage(Style.GetBrush("Construct.Panel")).Padding(4)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [
                    SNew(STextBlock)
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Small"))
                    .Text_Lambda([WeakWidget]()
                    {
                        const UConstructWebcamFeedWidget* Widget = WeakWidget.Get();
                        const AConstructPresencePawn* Player = Widget ? Widget->Pawn.Get() : nullptr;
                        const FString Name = Player ? Player->GetPlayerDisplayName() : FString();
                        return Name.IsEmpty() ? LOCTEXT("Player", "Player") : FText::FromString(Name);
                    })
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 2)
                [
                    SNew(SBox).WidthOverride(128).HeightOverride(96)
                    [
                        SNew(SImage)
                        .Image_Lambda([WeakWidget]() -> const FSlateBrush*
                        {
                            const UConstructWebcamFeedWidget* Widget = WeakWidget.Get();
                            return Widget ? &Widget->Brush : nullptr;
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
                    [
                        SNew(SButton)
                        .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                        .ContentPadding(FMargin(5, 2))
                        .IsEnabled_Lambda(CanModerate)
                        .OnClicked_Lambda([WeakWidget]()
                        {
                            if (const UConstructWebcamFeedWidget* Widget = WeakWidget.Get())
                            {
                                if (UConstructWebcamPanel* Owner = Widget->Panel.Get())
                                {
                                    Owner->BlockPlayer(Widget->Pawn.Get());
                                }
                            }
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT("Block", "Block")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Small"))
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                    [
                        SNew(SButton)
                        .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                        .ContentPadding(FMargin(5, 2))
                        .IsEnabled_Lambda(CanModerate)
                        .OnClicked_Lambda([WeakWidget]()
                        {
                            if (const UConstructWebcamFeedWidget* Widget = WeakWidget.Get())
                            {
                                if (UConstructWebcamPanel* Owner = Widget->Panel.Get())
                                {
                                    Owner->OpenReport(Widget->Pawn.Get());
                                }
                            }
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT("Report", "Report")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Small"))
                        ]
                    ]
                ]
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
