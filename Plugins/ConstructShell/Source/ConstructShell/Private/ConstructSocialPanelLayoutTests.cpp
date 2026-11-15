#include "ConstructSocialPanelLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Layout/SBox.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructSocialPanelRegions, "Construct.Shell.SocialPanels.SeparateRegions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructSocialPanelRegions::RunTest(const FString&)
{
    const FVector2D Windows[] = {{1920, 1080}, {1280, 720}, {1024, 768}, {800, 600}, {960, 540}};
    for (const FVector2D Window : Windows)
    {
        TArray<FSlateRect> Cards;
        for (const EConstructSocialPanelRegion Region : {EConstructSocialPanelRegion::Chat, EConstructSocialPanelRegion::Voice, EConstructSocialPanelRegion::Webcam})
        {
            const TSharedRef<SWidget> Card = ConstructSocialPanelLayout::Place(Region, SNew(SBox).WidthOverride(1000).HeightOverride(2000));
            Card->SlatePrepass(1.0f);
            FArrangedChildren Children(EVisibility::Visible);
            Card->ArrangeChildren(FGeometry::MakeRoot(Window, FSlateLayoutTransform()), Children);
            if (!TestEqual(TEXT("The production layout arranges one card"), Children.Num(), 1))
            {
                return false;
            }
            const FSlateRect Rect = Children[0].Geometry.GetLayoutBoundingRect();
            TestTrue(TEXT("Card clears the hub status"), Rect.Top >= 80);
            TestTrue(TEXT("Card clears the footer and feedback"), Rect.Bottom <= Window.Y - 110);
            TestTrue(TEXT("Card stays inside the player's viewport"), Rect.Left >= 0 && Rect.Right <= Window.X && Rect.GetSize().X > 0 && Rect.GetSize().Y > 0);
            for (const FSlateRect& Previous : Cards)
            {
                TestFalse(TEXT("Simultaneously open communication cards never overlap"), Rect.Left < Previous.Right && Rect.Right > Previous.Left && Rect.Top < Previous.Bottom && Rect.Bottom > Previous.Top);
            }
            Cards.Add(Rect);
        }
    }
    return true;
}
#endif
