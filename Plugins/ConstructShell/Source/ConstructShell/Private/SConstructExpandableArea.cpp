#include "SConstructExpandableArea.h"

#include "ConstructShellStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

void SConstructExpandableArea::Construct(const FArguments& InArgs)
{
    FArguments Args = InArgs;
    Args.Style(&FConstructShellStyle::Get().GetWidgetStyle<FExpandableAreaStyle>("Construct.Expandable"));
    Args.HeaderPadding(FMargin(8.0f, 6.0f));
    Args.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
    SExpandableArea::Construct(Args);
}

TSharedRef<SWidget> SConstructExpandableArea::ConstructHeaderWidget(const FArguments& InArgs, TSharedRef<SWidget> HeaderContent)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 8, 0)
        [
            SNew(SBox).WidthOverride(16).HeightOverride(HeaderRowHeight).VAlign(VAlign_Center)
            [
                SNew(SImage).Image(this, &SConstructExpandableArea::OnGetCollapseImage).ColorAndOpacity(FSlateColor::UseForeground())
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Top)
        [
            HeaderContent
        ];
}
