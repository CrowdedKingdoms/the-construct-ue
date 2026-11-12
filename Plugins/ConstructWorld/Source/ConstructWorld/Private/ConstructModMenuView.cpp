#include "ConstructModMenu.h"

#include "ConstructShellStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructModMenu"

TSharedRef<SWidget> UConstructModMenu::RebuildWidget()
{
    const ISlateStyle& Style = FConstructShellStyle::Get();
    const TWeakObjectPtr<UConstructModMenu> WeakThis(this);
    TSharedRef<SWidget> Result =
        SNew(SBorder).BorderImage(Style.GetBrush("Construct.Background")).Padding(32)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [SNew(STextBlock).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Title")).Text(LOCTEXT("Title", "Mods"))]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton).ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Ghost"))
                    .OnClicked_Lambda([WeakThis]()
                    {
                        if (UConstructModMenu* Self = WeakThis.Get())
                        {
                            Self->Close();
                        }
                        return FReply::Handled();
                    })[SNew(STextBlock).Text(LOCTEXT("Close", "Close  (Esc)"))]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
            [SNew(STextBlock).AutoWrapText(true).TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>("Construct.Text.Muted")).Text(LOCTEXT("MissingLand", "Choose land you own. This is a partial list: land claimed outside the Construct and absent from its records may be missing."))]
            + SVerticalBox::Slot().FillHeight(1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.4f).Padding(0, 0, 24, 0)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                    [
                        SNew(SButton).ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary")).ContentPadding(10)
                        .OnClicked_Lambda([WeakThis]()
                        {
                            if (UConstructModMenu* Self = WeakThis.Get())
                            {
                                Self->Discover();
                            }
                            return FReply::Handled();
                        })[SNew(STextBlock).Text(LOCTEXT("Retry", "Refresh land list"))]
                    ]
                    + SVerticalBox::Slot().FillHeight(1)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()[SAssignNew(LandBox, SVerticalBox)]
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(0.6f)
                [SAssignNew(PanelBox, SVerticalBox)]
            ]
        ];
    RebuildRows();
    return Result;
}

void UConstructModMenu::RebuildRows()
{
    if (!LandBox)
    {
        return;
    }
    LandBox->ClearChildren();
    const ISlateStyle& Style = FConstructShellStyle::Get();
    const auto Note = [this, &Style](const FText& Text, bool bError)
    {
        LandBox->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
        [
            SNew(STextBlock).AutoWrapText(true)
            .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(bError ? "Construct.Text.Error" : "Construct.Text.Muted"))
            .Text(Text)
        ];
    };
    if (bHubPending)
    {
        Note(LOCTEXT("Loading", "Reading known land and confirming ownership..."), false);
    }
    if (!HubError.IsEmpty())
    {
        Note(FText::FromString(HubError), true);
        Note(LOCTEXT("Incomplete", "The registry could not be read, so this list is incomplete. Current level pads are checked independently."), false);
    }
    if (Omitted > 0)
    {
        Note(FText::Format(LOCTEXT("Cap", "{0} known chunks omitted: this menu checks up to 64 unique chunks."), FText::AsNumber(Omitted)), false);
    }
    if (!bHubPending && Rows.IsEmpty())
    {
        Note(LOCTEXT("Empty", "No known land here. Claim a chunk, then refresh this list."), false);
    }
    const TWeakObjectPtr<UConstructModMenu> WeakThis(this);
    for (const FConstructModLand& Row : Rows)
    {
        const FString Label = FString::Printf(TEXT("Chunk (%lld, %lld, %lld)%s"), Row.Chunk.X, Row.Chunk.Y, Row.Chunk.Z, Row.bConfirmed ? *FString::Printf(TEXT("\nGrid %lld"), Row.GridId) : TEXT(""));
        LandBox->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
        [
            SNew(SButton).ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>("Construct.Button.Secondary"))
            .ContentPadding(12)
            .IsEnabled_Lambda([WeakThis, GridId = Row.GridId, bConfirmed = Row.bConfirmed]()
            {
                const UConstructModMenu* Self = WeakThis.Get();
                return Self && bConfirmed && Self->CanSelectGrid(GridId);
            })
            .OnClicked_Lambda([WeakThis, GridId = Row.GridId]()
            {
                if (UConstructModMenu* Self = WeakThis.Get())
                {
                    Self->OpenForGrid(GridId);
                }
                return FReply::Handled();
            })[SNew(STextBlock).Text(FText::FromString(Label))]
        ];
        if (!Row.bConfirmed)
        {
            Note(FText::FromString(Row.bPending ? TEXT("Unconfirmed: checking ownership...") : TEXT("Unconfirmed: ") + Row.Message), !Row.bPending);
        }
    }
}
#undef LOCTEXT_NAMESPACE