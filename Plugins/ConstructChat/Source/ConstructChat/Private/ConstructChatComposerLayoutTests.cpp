#include "ConstructChatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    void ReadChatGeometry(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, FSlateRect& Card, FSlateRect& Composer, bool& bFoundComposer)
    {
        FArrangedChildren Children(EVisibility::Visible);
        Widget->ArrangeChildren(Geometry, Children);
        if (Widget->GetType() == FName(TEXT("SConstructSocialPanelRegion")) && Children.Num() == 1)
        {
            Card = Children[0].Geometry.GetLayoutBoundingRect();
        }
        if (Widget->GetType() == FName(TEXT("SEditableTextBox")))
        {
            Composer = Geometry.GetLayoutBoundingRect();
            bFoundComposer = true;
        }
        for (int32 Index = 0; Index < Children.Num(); ++Index)
        {
            ReadChatGeometry(Children[Index].Widget, Children[Index].Geometry, Card, Composer, bFoundComposer);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChatComposerLayoutTest, "Construct.Chat.ComposerVisibleAt720p", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructChatComposerLayoutTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
    Context.SetCurrentWorld(World);
    TStrongObjectPtr<UGameViewportClient> Viewport(NewObject<UGameViewportClient>(GEngine));
    Context.GameViewport = Viewport.Get();
    World->InitializeActorsForPlay(FURL());
    APlayerController* Controller = World->SpawnActor<APlayerController>();
    TStrongObjectPtr<ULocalPlayer> Player(NewObject<ULocalPlayer>(GEngine));
    Player->PlayerAdded(Viewport.Get(), int32(0));
    Controller->SetPlayer(Player.Get());
    UConstructChatComponent* Chat = NewObject<UConstructChatComponent>(Controller);
    Controller->AddOwnedComponent(Chat);
    // Exercise presentation of an already-open composer without creating an SDK session or sending text.
    Chat->bComposerOpen = true;
    Chat->HistoryText = FText::FromString(TEXT("Earlier message\nEarlier message\nEarlier message\nEarlier message\nEarlier message\nEarlier message\nEarlier message\nEarlier message"));
    TStrongObjectPtr<UConstructChatPanel> Panel(CreateWidget<UConstructChatPanel>(Controller));
    TSharedPtr<SWidget> Slate = Panel->TakeWidget();
    Slate->SlatePrepass(1.0f);
    FSlateRect Card;
    FSlateRect Composer;
    bool bFoundComposer = false;
    ReadChatGeometry(Slate.ToSharedRef(), FGeometry::MakeRoot(FVector2D(1280, 720), FSlateLayoutTransform()), Card, Composer, bFoundComposer);
    TestTrue(TEXT("Actual open composer participates in layout"), bFoundComposer);
    TestTrue(TEXT("The message field has usable height"), Composer.GetSize().Y >= 20);
    TestTrue(TEXT("The message field stays completely inside the visible card"), Composer.Left >= Card.Left && Composer.Right <= Card.Right && Composer.Top >= Card.Top && Composer.Bottom <= Card.Bottom);
    Slate.Reset();
    Panel.Reset();
    Player->PlayerRemoved();
    Player.Reset();
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
