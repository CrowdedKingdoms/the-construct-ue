#include "ConstructWebcamPanel.h"

#include "ConstructShellStyle.h"
#include "ConstructSocialPanelLayout.h"
#include "InputCoreTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructWebcamPanel"

TSharedRef<SWidget> UConstructWebcamPanel::RebuildWidget()
{
    const TWeakObjectPtr<UConstructWebcamPanel> WeakPanel(this);
    const ISlateStyle& Style = FConstructShellStyle::Get();
    const auto CanAct = [WeakPanel]()
    {
        const UConstructWebcamPanel* Panel = WeakPanel.Get();
        return Panel && Panel->CanInteract() && !Panel->IsBusy();
    };
    const auto Close = [WeakPanel]()
    {
        if (UConstructWebcamPanel* Panel = WeakPanel.Get())
        {
            Panel->CloseDialog();
        }
        return FReply::Handled();
    };
    const auto ScopeRow = [WeakPanel, CanAct, &Style](FText Label, ECrowdyCommunicationSetting Setting) -> TSharedRef<SWidget>
    {
        TSharedRef<SHorizontalBox> Choices = SNew(SHorizontalBox);
        const ECrowdyCommunicationScope Scopes[] = { ECrowdyCommunicationScope::Off, ECrowdyCommunicationScope::Contacts, ECrowdyCommunicationScope::Everyone };
        const FText Labels[] = { LOCTEXT("Off", "Off"), LOCTEXT("Contacts", "Contacts"), LOCTEXT("Everyone", "Everyone") };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Scopes); ++Index)
        {
            const ECrowdyCommunicationScope Scope = Scopes[Index];
            Choices->AddSlot().FillWidth(1.0f).Padding(0, 0, Index < 2 ? 6 : 0, 0)
            [
                SNew(SButton)
                .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                .ContentPadding(FMargin(8, 6))
                .IsEnabled_Lambda([CanAct, WeakPanel]()
                {
                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                    return CanAct() && Panel && Panel->AreSettingsKnown();
                })
                .OnClicked_Lambda([WeakPanel, Setting, Scope]()
                {
                    if (UConstructWebcamPanel* Panel = WeakPanel.Get())
                    {
                        Panel->SetScope(Setting, Scope);
                    }
                    return FReply::Handled();
                })
                [
                    SNew(STextBlock).Text(Labels[Index]).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
                ]
            ];
        }
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(STextBlock).Text(Label).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(STextBlock)
                .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted"))
                .AutoWrapText(true)
                .Text_Lambda([WeakPanel, Setting]()
                {
                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                    return Panel ? Panel->GetScopeText(Setting) : FText::GetEmpty();
                })
            ]
            + SVerticalBox::Slot().AutoHeight()[Choices];
    };

    TSharedRef<SVerticalBox> SettingsContent = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [
            SNew(STextBlock).Text(LOCTEXT("SettingsTitle", "Video settings")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [
            ScopeRow(LOCTEXT("VideoSend", "Video Send"), ECrowdyCommunicationSetting::VideoSend)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [
            ScopeRow(LOCTEXT("VideoReceive", "Video Receive"), ECrowdyCommunicationSetting::VideoReceive)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
        [
            SNew(SButton)
            .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
            .ContentPadding(FMargin(10, 7))
            .IsEnabled_Lambda(CanAct)
            .OnClicked_Lambda([WeakPanel]()
            {
                if (UConstructWebcamPanel* Panel = WeakPanel.Get())
                {
                    Panel->ReadSettings();
                }
                return FReply::Handled();
            })
            [
                SNew(STextBlock).Text(LOCTEXT("ReadSettings", "Read settings")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
        [
            SNew(STextBlock).Text(LOCTEXT("ParentTitle", "Parent's consent")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ParentHelp", "If video needs a parent's consent, enter their email to request it. Your account's consent rules still apply."))
            .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted"))
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SAssignNew(ParentEmail, SEditableTextBox)
            .Style(&Style.GetWidgetStyle<FEditableTextBoxStyle>("Construct.Input"))
            .HintText(LOCTEXT("ParentEmail", "Parent's email"))
            .IsEnabled_Lambda(CanAct)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [
            SNew(SButton)
            .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
            .ContentPadding(FMargin(10, 7))
            .IsEnabled_Lambda(CanAct)
            .OnClicked_Lambda([WeakPanel]()
            {
                if (UConstructWebcamPanel* Panel = WeakPanel.Get())
                {
                    if (Panel->ParentEmail.IsValid())
                    {
                        Panel->AskParent(Panel->ParentEmail->GetText().ToString());
                    }
                }
                return FReply::Handled();
            })
            [
                SNew(STextBlock).Text(LOCTEXT("AskParent", "Ask parent")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
            ]
        ];

    const TSharedRef<ECrowdySafetyReportReason> SelectedReason = MakeShared<ECrowdySafetyReportReason>(ECrowdySafetyReportReason::Other);
    TSharedRef<SVerticalBox> ReportContent = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [
            SNew(STextBlock)
            .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading"))
            .AutoWrapText(true)
            .Text_Lambda([WeakPanel]()
            {
                const UConstructWebcamPanel* Panel = WeakPanel.Get();
                return Panel ? Panel->GetReportHeading() : FText::GetEmpty();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(STextBlock).Text(LOCTEXT("ReportReason", "Reason (selected below)")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
        ];
    const ECrowdySafetyReportReason Reasons[] = { ECrowdySafetyReportReason::ChildSafety, ECrowdySafetyReportReason::Harassment, ECrowdySafetyReportReason::SexualContent, ECrowdySafetyReportReason::Hate, ECrowdySafetyReportReason::SelfHarm, ECrowdySafetyReportReason::Cheating, ECrowdySafetyReportReason::Other };
    const FText ReasonLabels[] = { LOCTEXT("ChildSafety", "Child safety"), LOCTEXT("Harassment", "Harassment"), LOCTEXT("SexualContent", "Sexual content"), LOCTEXT("Hate", "Hate"), LOCTEXT("SelfHarm", "Self harm"), LOCTEXT("Cheating", "Cheating"), LOCTEXT("Other", "Other") };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Reasons); ++Index)
    {
        const ECrowdySafetyReportReason Reason = Reasons[Index];
        const FText Label = ReasonLabels[Index];
        ReportContent->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
        [
            SNew(SButton)
            .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
            .ContentPadding(FMargin(10, 5))
            .IsEnabled_Lambda(CanAct)
            .OnClicked_Lambda([SelectedReason, Reason]()
            {
                *SelectedReason = Reason;
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
                .Text_Lambda([SelectedReason, Reason, Label]()
                {
                    return *SelectedReason == Reason ? FText::Format(LOCTEXT("SelectedReason", "Selected: {0}"), Label) : Label;
                })
            ]
        ];
    }
    ReportContent->AddSlot().AutoHeight().Padding(0, 8, 0, 8)
    [
        SAssignNew(ReportDetails, SEditableTextBox)
        .Style(&Style.GetWidgetStyle<FEditableTextBoxStyle>("Construct.Input"))
        .HintText(LOCTEXT("ReportDetails", "Details (optional)"))
        .IsEnabled_Lambda(CanAct)
    ];
    ReportContent->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
    [
        SNew(SButton)
        .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Primary"))
        .ContentPadding(FMargin(10, 7))
        .IsEnabled_Lambda(CanAct)
        .OnClicked_Lambda([WeakPanel, SelectedReason]()
        {
            if (UConstructWebcamPanel* Panel = WeakPanel.Get())
            {
                if (Panel->ReportDetails.IsValid())
                {
                    Panel->SubmitReport(*SelectedReason, Panel->ReportDetails->GetText().ToString());
                }
            }
            return FReply::Handled();
        })
        [
            SNew(STextBlock).Text(LOCTEXT("SubmitReport", "Submit report"))
        ]
    ];

    return SNew(SOverlay)
        .Visibility_Lambda([WeakPanel]()
        {
            const UConstructWebcamPanel* Panel = WeakPanel.Get();
            return Panel && Panel->IsAvailable() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        })
        + SOverlay::Slot()
        [
            ConstructSocialPanelLayout::Place(EConstructSocialPanelRegion::Webcam,
                SNew(SBorder).BorderImage(Style.GetBrush("Construct.Panel")).Padding(12)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).Text(LOCTEXT("FixedCameraOn", "CAMERA ON"))
                        .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
                        .ColorAndOpacity(FConstructShellStyle::Gold())
                        .Visibility_Lambda([WeakPanel]()
                        {
                            const UConstructWebcamPanel* Panel = WeakPanel.Get();
                            return Panel && Panel->IsCameraOn() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body")).AutoWrapText(true)
                        .Visibility_Lambda([WeakPanel]()
                        {
                            const UConstructWebcamPanel* Panel = WeakPanel.Get();
                            return Panel && !Panel->GetRecordingText().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                        })
                        .Text_Lambda([WeakPanel]()
                        {
                            const UConstructWebcamPanel* Panel = WeakPanel.Get();
                            return Panel ? Panel->GetRecordingText() : FText::GetEmpty();
                        })
                    ]
                    + SVerticalBox::Slot().FillHeight(1)
                    [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [
                        SNew(SExpandableArea)
                        .InitiallyCollapsed(true)
                        .BorderImage(Style.GetBrush("Construct.Panel"))
                        .BodyBorderImage(Style.GetBrush("Construct.Panel"))
                        .HeaderContent()
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body")).AutoWrapText(true)
                                .Text_Lambda([WeakPanel]()
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel && Panel->IsCameraOn() ? LOCTEXT("ExpandCameraOn", "Webcam controls - CAMERA ON") : LOCTEXT("ExpandWebcam", "Webcam controls");
                                })
                            ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted")).AutoWrapText(true)
                                .Text_Lambda([WeakPanel]()
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel ? Panel->GetReasonText() : FText::GetEmpty();
                                })
                            ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body")).AutoWrapText(true)
                                .Text_Lambda([WeakPanel]()
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel ? Panel->GetRecordingText() : FText::GetEmpty();
                                })
                            ]
                        ]
                        .BodyContent()
                        [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock).Text(LOCTEXT("Webcam", "Webcam")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading"))
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                        [
                            SNew(SButton)
                            .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                            .ContentPadding(FMargin(8, 6))
                            .IsEnabled_Lambda(CanAct)
                            .OnClicked_Lambda([WeakPanel]()
                            {
                                if (UConstructWebcamPanel* Panel = WeakPanel.Get())
                                {
                                    Panel->ToggleCamera();
                                }
                                return FReply::Handled();
                            })
                            [
                                SNew(STextBlock)
                                .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body")).AutoWrapText(true)
                                .Text_Lambda([WeakPanel]()
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel ? Panel->GetCameraCaption() : FText::GetEmpty();
                                })
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                            [
                                SNew(SButton)
                                .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                                .ContentPadding(FMargin(6, 6))
                                .IsEnabled_Lambda(CanAct)
                                .OnClicked_Lambda([WeakPanel]()
                                {
                                    if (UConstructWebcamPanel* Panel = WeakPanel.Get())
                                    {
                                        Panel->ToggleVideo();
                                    }
                                    return FReply::Handled();
                                })
                                [
                                    SNew(STextBlock)
                                    .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body")).AutoWrapText(true)
                                    .Text_Lambda([WeakPanel]()
                                    {
                                        const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                        return Panel && Panel->IsShowingVideo() ? LOCTEXT("VideoOn", "Show Video on [F6]") : LOCTEXT("VideoOff", "Show Video off [F6]");
                                    })
                                ]
                            ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(SButton)
                                .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                                .ContentPadding(FMargin(8, 6))
                                .IsEnabled_Lambda(CanAct)
                                .OnClicked_Lambda([WeakPanel]()
                                {
                                    if (UConstructWebcamPanel* Panel = WeakPanel.Get())
                                    {
                                        Panel->ToggleSettings();
                                    }
                                    return FReply::Handled();
                                })
                                [
                                    SNew(STextBlock).Text(LOCTEXT("Settings", "Settings")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("CameraOn", "CAMERA ON"))
                            .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
                            .ColorAndOpacity(FConstructShellStyle::Gold())
                            .Visibility_Lambda([WeakPanel]()
                            {
                                const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                return Panel && Panel->IsCameraOn() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                            })
                        ]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)
                        [
                            SNew(SBox).WidthOverride(128).HeightOverride(96)
                            .Visibility_Lambda([WeakPanel]()
                            {
                                const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                return Panel && Panel->GetPreviewBrush()->GetResourceObject() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                            })
                            [
                                SNew(SImage).Image_Lambda([WeakPanel]() -> const FSlateBrush*
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel ? Panel->GetPreviewBrush() : nullptr;
                                })
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted")).AutoWrapText(true)
                            .Text_Lambda([WeakPanel]()
                            {
                                const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                return Panel ? Panel->GetReasonText() : FText::GetEmpty();
                            })
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                        [
                            SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body")).AutoWrapText(true)
                            .Text_Lambda([WeakPanel]()
                            {
                                const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                return Panel ? Panel->GetRecordingText() : FText::GetEmpty();
                            })
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                        [
                            SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted")).AutoWrapText(true)
                            .Text_Lambda([WeakPanel]()
                            {
                                const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                return Panel ? Panel->GetMessage() : FText::GetEmpty();
                            })
                        ]
                        ]
                    ]
                    ]
                ])
        ]
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .BorderImage(Style.GetBrush("Construct.Background"))
            .BorderBackgroundColor(FLinearColor(1, 1, 1, 0.85f))
            .Padding(24)
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            .Visibility_Lambda([WeakPanel]()
            {
                const UConstructWebcamPanel* Panel = WeakPanel.Get();
                return Panel && (Panel->IsSettingsOpen() || Panel->IsReportOpen()) ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&)
            {
                return FReply::Handled();
            })
            [
                SNew(SBox).WidthOverride(440).MaxDesiredHeight(640)
                [
                    SNew(SBorder).BorderImage(Style.GetBrush("Construct.Panel")).Padding(20)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(SBox)
                                .Visibility_Lambda([WeakPanel]()
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel && Panel->IsSettingsOpen() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
                                })
                                [SettingsContent]
                            ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(SBox)
                                .Visibility_Lambda([WeakPanel]()
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel && Panel->IsReportOpen() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
                                })
                                [ReportContent]
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                            [
                                SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted")).AutoWrapText(true)
                                .Text_Lambda([WeakPanel]()
                                {
                                    const UConstructWebcamPanel* Panel = WeakPanel.Get();
                                    return Panel ? Panel->GetMessage() : FText::GetEmpty();
                                })
                            ]
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(SButton)
                                .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
                                .ContentPadding(FMargin(10, 7))
                                .OnClicked_Lambda(Close)
                                [
                                    SNew(STextBlock).Text(LOCTEXT("Close", "Close (Esc)")).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body"))
                                ]
                            ]
                        ]
                    ]
                ]
            ]
        ];
}

void UConstructWebcamPanel::ReleaseSlateResources(bool bReleaseChildren)
{
    ParentEmail.Reset();
    ReportDetails.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

FReply UConstructWebcamPanel::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape && (IsSettingsOpen() || IsReportOpen()))
    {
        CloseDialog();
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

#undef LOCTEXT_NAMESPACE

