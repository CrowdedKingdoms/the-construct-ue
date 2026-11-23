#include "ConstructModPanel.h"

#include "ConstructShellStyle.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructModPanel"

FText UConstructModPanel::Caption() const
{
    if (MutationKind == EMutation::Deploy)
    {
        return LOCTEXT("Deploying", "Deploying...");
    }
    if (MutationKind == EMutation::Disable)
    {
        return LOCTEXT("Disabling", "Switching off...");
    }
    if (MutationKind == EMutation::Delete)
    {
        return LOCTEXT("Deleting", "Deleting...");
    }
    switch (State)
    {
    case EConstructModPanelState::Loading: return LOCTEXT("Loading", "Reading this land's mods...");
    case EConstructModPanelState::NoMod: return LOCTEXT("NoMod", "Ready to build");
    case EConstructModPanelState::BuildFailed: return LOCTEXT("BuildFailed", "Build failed");
    case EConstructModPanelState::Built: return LOCTEXT("Built", "Built. Ready to deploy.");
    case EConstructModPanelState::Off: return MutationKind == EMutation::Enable ? LOCTEXT("Enabling", "Switching on...") : LOCTEXT("Off", "Off");
    case EConstructModPanelState::On: return bWakePending ? LOCTEXT("Starting", "On - starting...") : bRunningFromList && ShownMod.bRunning ? LOCTEXT("Running", "On - running") : LOCTEXT("NotRunning", "On - not running");
    case EConstructModPanelState::Blocked: return FText::Format(LOCTEXT("Blocked", "Blocked: {0}"), FText::FromString(ShownMod.BlockedReason));
    case EConstructModPanelState::Error: return LOCTEXT("Error", "Could not complete the request");
    case EConstructModPanelState::Building:
        return FText::Format(LOCTEXT("Building", "{0} - {1} seconds"), BuildStatus == ECrowdyModBuildStatus::Queued ? LOCTEXT("Queued", "Queued") : LOCTEXT("Compiling", "Building"), FText::AsNumber(FMath::Max(0, FMath::FloorToInt(FPlatformTime::Seconds() - BuildStartedAt))));
    default: return FText::GetEmpty();
    }
}

TSharedRef<SWidget> UConstructModPanel::RebuildWidget()
{
    const ISlateStyle& Style = FConstructShellStyle::Get();
    const TWeakObjectPtr<UConstructModPanel> WeakThis(this);
    const auto Action = [WeakThis, &Style](const FText& Label, TFunction<bool(const UConstructModPanel&)> Offered, TFunction<void(UConstructModPanel&)> Clicked)
    {
        return SNew(SButton)
            .ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
            .ContentPadding(FMargin(14, 9))
            .Visibility_Lambda([WeakThis, Offered]() { const auto* Self = WeakThis.Get(); return Self && Offered(*Self) ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnClicked_Lambda([WeakThis, Clicked]() { if (auto* Self = WeakThis.Get())
                {
                    Clicked(*Self);
                } return FReply::Handled(); })
            [SNew(STextBlock).Text(Label)];
    };
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [
            SNew(STextBlock).AutoWrapText(true).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted"))
            .Text_Lambda([WeakThis]()
            {
                return WeakThis.IsValid() ? WeakThis->LandLabel : FText::GetEmpty();
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading")).Text(LOCTEXT("Starter", "Choose an example"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [
                SNew(SButton).ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary")).ContentPadding(FMargin(12, 8))
                .IsEnabled_Lambda([WeakThis]() { const auto* Self = WeakThis.Get(); return Self && Self->MutationKind == EMutation::None && !Self->bLogPending; })
                .OnClicked_Lambda([WeakThis]() { if (auto* Self = WeakThis.Get())
                {
                    Self->SelectStarter(TEXT("construct-spinning-child"));
                } return FReply::Handled(); })
                [SNew(STextBlock).Text(LOCTEXT("Spinning", "Spinning blocks"))]
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SButton).ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary")).ContentPadding(FMargin(12, 8))
                .IsEnabled_Lambda([WeakThis]() { const auto* Self = WeakThis.Get(); return Self && Self->MutationKind == EMutation::None && !Self->bLogPending; })
                .OnClicked_Lambda([WeakThis]() { if (auto* Self = WeakThis.Get())
                {
                    Self->SelectStarter(TEXT("platform-greeter"));
                } return FReply::Handled(); })
                [SNew(STextBlock).Text(LOCTEXT("Greeter", "Greeting example"))]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [
            SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted"))
            .Text_Lambda([WeakThis]() { const auto* Self = WeakThis.Get(); return Self ? FText::Format(LOCTEXT("Selected", "Selected: {0}"), Self->SelectedStarter == TEXT("platform-greeter") ? LOCTEXT("Greeter", "Greeting example") : LOCTEXT("Spinning", "Spinning blocks")) : FText::GetEmpty(); })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Heading")).Text_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->Caption() : FText::GetEmpty(); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(STextBlock).AutoWrapText(true).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Error"))
            .Text_Lambda([WeakThis]() { return WeakThis.IsValid() ? FText::FromString(WeakThis->Notice) : FText::GetEmpty(); })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Action(LOCTEXT("Build", "Build"), [](const auto& Self) { return Self.CanBuild(); }, [](auto& Self) { Self.Build(Self.SelectedStarter); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Action(LOCTEXT("Deploy", "Deploy"), [](const auto& Self) { return Self.CanDeploy(); }, [](auto& Self) { Self.Deploy(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Action(LOCTEXT("On", "Switch on"), [](const auto& Self) { return Self.CanToggle(true); }, [](auto& Self) { Self.SetEnabled(true); })]
            + SHorizontalBox::Slot().AutoWidth()[Action(LOCTEXT("Off", "Switch off"), [](const auto& Self) { return Self.CanToggle(false); }, [](auto& Self) { Self.SetEnabled(false); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Action(LOCTEXT("Log", "Show log"), [](const auto& Self) { return Self.CanLog(); }, [](auto& Self) { Self.ShowLog(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Action(LOCTEXT("Delete", "Delete"), [](const auto& Self) { return Self.CanDelete(); }, [](auto& Self) { Self.Delete(); })]
            + SHorizontalBox::Slot().AutoWidth()[Action(LOCTEXT("Refresh", "Refresh"), [](const auto& Self) { return Self.CanRefresh(); }, [](auto& Self) { Self.Refresh(); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(STextBlock).AutoWrapText(true).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted"))
            .Text_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->RefreshHint() : FText::GetEmpty(); })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SButton).ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary")).ContentPadding(FMargin(10, 6))
            .Visibility_Lambda([WeakThis]() { const auto* Self = WeakThis.Get(); return Self && !Self->bStarterReady && !Self->bStarterPending ? EVisibility::Visible : EVisibility::Collapsed; })
            .ToolTipText_Lambda([WeakThis]() { return WeakThis.IsValid() ? FText::FromString(WeakThis->StarterError) : FText::GetEmpty(); })
            .OnClicked_Lambda([WeakThis]() { if (auto* Self = WeakThis.Get())
                {
                    Self->FetchStarter();
                } return FReply::Handled(); })
            [SNew(STextBlock).Text(LOCTEXT("RetryStarter", "Retry greeting starter"))]
        ]
        + SVerticalBox::Slot().FillHeight(1)
        [
            SNew(SBorder).BorderImage(Style.GetBrush("Construct.Background")).Padding(10)
            [
                SNew(SScrollBox) + SScrollBox::Slot()
                [SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Small")).AutoWrapText(true).Text_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->CachedLog : FText::GetEmpty(); })]
            ]
        ];
}

#undef LOCTEXT_NAMESPACE