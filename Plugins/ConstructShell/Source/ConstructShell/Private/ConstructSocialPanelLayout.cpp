#include "ConstructSocialPanelLayout.h"

#include "Layout/ArrangedChildren.h"
#include "Widgets/SCompoundWidget.h"

namespace
{
    class SConstructSocialPanelRegion : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SConstructSocialPanelRegion) {}
            SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_END_ARGS()

        void Construct(const FArguments& Arguments, EConstructSocialPanelRegion InRegion)
        {
            Region = InRegion;
            SetVisibility(EVisibility::SelfHitTestInvisible);
            SetClipping(EWidgetClipping::ClipToBounds);
            ChildSlot[Arguments._Content.Widget];
        }

        virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Children) const override
        {
            const FVector2D View = Geometry.GetLocalSize();
            constexpr double Margin = 20.0;
            constexpr double Gap = 16.0;
            constexpr double Top = 80.0;
            constexpr double Bottom = 110.0;
            const double ColumnWidth = FMath::Max(0.0, (View.X - Margin * 2.0 - Gap) * 0.5);
            const double Width = FMath::Min(Region == EConstructSocialPanelRegion::Chat ? 430.0 : 380.0, ColumnWidth);
            const double RowHeight = FMath::Max(0.0, (View.Y - Top - Bottom - Gap) * 0.5);
            const double Height = FMath::Min(RowHeight, static_cast<double>(ChildSlot.GetWidget()->GetDesiredSize().Y));
            const double X = Region == EConstructSocialPanelRegion::Chat ? Margin : FMath::Max(Margin, View.X - Margin - Width);
            const double Y = Region == EConstructSocialPanelRegion::Webcam ? Top + RowHeight + Gap : Top;
            Children.AddWidget(Geometry.MakeChild(ChildSlot.GetWidget(), FVector2D(Width, Height), FSlateLayoutTransform(FVector2D(X, Y))));
        }

    private:
        EConstructSocialPanelRegion Region = EConstructSocialPanelRegion::Chat;
    };
}

TSharedRef<SWidget> ConstructSocialPanelLayout::Place(EConstructSocialPanelRegion Region, TSharedRef<SWidget> Content)
{
    return SNew(SConstructSocialPanelRegion, Region)[Content];
}
