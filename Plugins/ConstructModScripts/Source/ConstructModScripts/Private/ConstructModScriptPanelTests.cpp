#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ConstructModScriptPanel.h"

#include "ConstructModScriptInput.h"
#include "ConstructModScriptSamples.h"
#include "ConstructShellStyle.h"
#include "ConstructUIInputSubsystem.h"
#include "SConstructChoiceList.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/SceneViewport.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SViewport.h"

namespace
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

bool ContainsText(const TSharedRef<SWidget>& Widget, const FString& Caption)
{
    if (Widget->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Caption)
    {
        return true;
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
    {
        if (ContainsText(Children->GetChildAt(Index), Caption))
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<SButton> FindButton(const TSharedRef<SWidget>& Widget, const FString& Caption)
{
    if (Widget->GetType() == TEXT("SButton") && ContainsText(Widget, Caption))
    {
        return StaticCastSharedRef<SButton>(Widget);
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
    {
        if (TSharedPtr<SButton> Found = FindButton(Children->GetChildAt(Index), Caption))
        {
            return Found;
        }
    }
    return nullptr;
}

TSharedPtr<SScrollBox> FindScroll(const TSharedRef<SWidget>& Widget, const FString& Caption)
{
    if (Widget->GetType() == TEXT("SScrollBox") && ContainsText(Widget, Caption))
    {
        return StaticCastSharedRef<SScrollBox>(Widget);
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Index < Children->Num(); ++Index)
    {
        if (TSharedPtr<SScrollBox> Found = FindScroll(Children->GetChildAt(Index), Caption))
        {
            return Found;
        }
    }
    return nullptr;
}

struct FScriptPanelFixture
{
    UWorld* World = nullptr;
    TStrongObjectPtr<UGameViewportClient> Viewport;
    TSharedPtr<SViewport> ViewportWidget;
    TSharedPtr<FSceneViewport> Scene;
    TStrongObjectPtr<ULocalPlayer> Player;
    TStrongObjectPtr<UConstructModScriptPanel> Panel;
    UConstructUIInputSubsystem* Gate = nullptr;
    APlayerController* Controller = nullptr;
    TSharedPtr<SWidget> Widget;
    TSharedRef<FConstructModScriptPresentation> View = MakeShared<FConstructModScriptPresentation>();

    bool Initialize()
    {
        UWorld::InitializationValues Init;
        Init.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
        if (!World || !GEngine)
        {
            return false;
        }
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
        Context.SetCurrentWorld(World);
        Viewport.Reset(NewObject<UGameViewportClient>(GEngine));
        ViewportWidget = SNew(SViewport);
        Scene = Viewport->CreateViewport(ViewportWidget.ToSharedRef());
        Context.GameViewport = Viewport.Get();
        World->InitializeActorsForPlay(FURL());
        Controller = World->SpawnActor<APlayerController>();
        Player.Reset(NewObject<ULocalPlayer>(GEngine));
        Player->PlayerAdded(Viewport.Get(), int32(0));
        Controller->SetPlayer(Player.Get());
        Gate = Player->GetSubsystem<UConstructUIInputSubsystem>();
        Panel.Reset(CreateWidget<UConstructModScriptPanel>(Controller));
        if (!Gate || !Panel.IsValid())
        {
            return false;
        }
        Panel->SetPresentation(View);
        Widget = Panel->TakeWidget();
        Gate->SetSessionReady(true);
        return true;
    }

    ~FScriptPanelFixture()
    {
        if (Panel.IsValid())
        {
            Panel->Shutdown();
        }
        Widget.Reset();
        Panel.Reset();
        if (Player.IsValid())
        {
            Player->PlayerRemoved();
        }
        Scene.Reset();
        if (World)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptModalTest, "Construct.ModScripts.UI.ModalOwnershipAndWithdrawal", Flags)
bool FConstructScriptModalTest::RunTest(const FString&)
{
    FScriptPanelFixture Fixture;
    if (!TestTrue(TEXT("Local UI fixture without any SDK collection"), Fixture.Initialize()))
    {
        return false;
    }
    TestTrue(TEXT("Gameplay initially available"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.View->ShowPrompt(ConstructModScriptSamples::Consent());
    TestFalse(TEXT("Consent holds input"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.Panel->ToggleRunningList();
    TestFalse(TEXT("F7 does not bypass consent"), Fixture.Panel->IsRunningListOpen());
    Fixture.Gate->SetModal(TEXT("Chat"), true);
    Fixture.View->WithdrawPrompt(1);
    TestFalse(TEXT("Withdrawal does not release another modal"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.Gate->SetModal(TEXT("Chat"), false);
    TestTrue(TEXT("Closing last modal restores gameplay"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.Panel->ToggleRunningList();
    TestTrue(TEXT("Running list opens"), Fixture.Panel->IsRunningListOpen());
    TestFalse(TEXT("Running list owns input"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.Panel->CloseRunningList();
    TestTrue(TEXT("List close restores gameplay"), Fixture.Gate->IsGameplayInputAllowed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptShutdownTest, "Construct.ModScripts.UI.ShutdownReleasesOnlyOwnedInput", Flags)
bool FConstructScriptShutdownTest::RunTest(const FString&)
{
    FScriptPanelFixture Fixture;
    if (!TestTrue(TEXT("Fixture created"), Fixture.Initialize()))
    {
        return false;
    }
    Fixture.View->ShowPrompt(ConstructModScriptSamples::Consent());
    Fixture.Gate->SetModal(TEXT("OtherDialog"), true);
    Fixture.Panel->Shutdown();
    TestNull(TEXT("Pointer bindings detached"), Fixture.Panel->GetPointerInput());
    TestFalse(TEXT("Other modal still holds input"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.Gate->SetModal(TEXT("OtherDialog"), false);
    TestTrue(TEXT("No leaked consent modal"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.View->ShowHud({TEXT("late"), TEXT("Late event"), 41, 1}, TEXT("Must not reopen UI"));
    TestTrue(TEXT("Late model event cannot reacquire input after shutdown"), Fixture.Gate->IsGameplayInputAllowed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptBindingTest, "Construct.ModScripts.UI.ControllerBindings", Flags)
bool FConstructScriptBindingTest::RunTest(const FString&)
{
    FScriptPanelFixture Fixture;
    if (!TestTrue(TEXT("Fixture created"), Fixture.Initialize()))
    {
        return false;
    }
    UConstructModScriptInput* Input = Fixture.Panel->GetPointerInput();
    if (!TestNotNull(TEXT("Input source is on controller"), Input))
    {
        return false;
    }
    int32 MouseBindings = 0;
    for (FInputKeyBinding& Binding : Input->KeyBindings)
    {
        if (Binding.Chord.Key == EKeys::F7)
        {
            Binding.KeyDelegate.Execute(EKeys::F7);
        }
        else if (Binding.Chord.Key.IsMouseButton())
        {
            ++MouseBindings;
            TestFalse(TEXT("Observer does not consume Paint input"), Binding.bConsumeInput);
        }
    }
    TestTrue(TEXT("Actual F7 binding opens list"), Fixture.Panel->IsRunningListOpen());
    TestEqual(TEXT("Three down and three up bindings"), MouseBindings, 6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptPreviewTest, "Construct.ModScripts.UI.PreviewImages", Flags)
bool FConstructScriptPreviewTest::RunTest(const FString&)
{
    if (GUsingNullRHI)
    {
        AddInfo(TEXT("Preview images need a renderer; run with -RenderOffscreen instead of -nullrhi."));
        return true;
    }
    FScriptPanelFixture Fixture;
    if (!TestTrue(TEXT("Fixture created"), Fixture.Initialize()))
    {
        return false;
    }
    const TSharedRef<FConstructModScriptPresentation> Samples = ConstructModScriptSamples::Make();
    Fixture.Panel->SetPresentation(Samples);
    Samples->OnAnswerRequested.AddLambda([](int32, bool) {});
    Samples->OnRevokeRequested.AddLambda([](const FString&) {});
    Samples->OnForgetRequested.AddLambda([](int64) {});
    TSharedRef<SWidget> Scene = SNew(SBorder).BorderImage(FConstructShellStyle::Get().GetBrush("Construct.Background")).Padding(0)[Fixture.Widget.ToSharedRef()];
    FWidgetRenderer Renderer(true);
    const FVector2D Size(1440, 900);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, true));
    // Slate applies display gamma; keep the target linear so hardware does not apply it a second time.
    Target->InitCustomFormat(1440, 900, PF_B8G8R8A8, true);
    Target->UpdateResourceImmediate(true);
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("ModScriptsUI");
    IFileManager::Get().MakeDirectory(*Folder, true);
    const auto Capture = [&](const TCHAR* Name)
    {
        for (int32 Frame = 0; Frame < 3; ++Frame)
        {
            Scene->SlatePrepass();
            Renderer.DrawWidget(Target.Get(), Scene, Size, 0.0f);
            FlushRenderingCommands();
        }
        TArray<FColor> Pixels;
        FReadSurfaceDataFlags ReadFlags;
        ReadFlags.SetLinearToGamma(false);
        if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags))
        {
            return false;
        }
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(1440, 900, Pixels, Png);
        return FFileHelper::SaveArrayToFile(Png, *(Folder / Name));
    };
    Samples->ShowPrompt(ConstructModScriptSamples::Consent());
    TestTrue(TEXT("Consent image exported"), Capture(TEXT("consent.png")));
    if (const TSharedPtr<SButton> Run = FindButton(Fixture.Widget.ToSharedRef(), TEXT("Run")))
    {
        Run->SimulateClick();
        TestTrue(TEXT("Selected-card image exported"), Capture(TEXT("consent-selected.png")));
    }
    Samples->WithdrawPrompt(1);
    Fixture.Panel->ToggleRunningList();
    TestTrue(TEXT("Running list image exported"), Capture(TEXT("running.png")));
    Fixture.Panel->CloseRunningList();
    TestTrue(TEXT("HUD image exported"), Capture(TEXT("hud.png")));
    AddInfo(TEXT("Images contain local presentation samples only, no running module or platform consent."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructScriptActionTest, "Construct.ModScripts.UI.ButtonRequestsAndRebinding", Flags)
bool FConstructScriptActionTest::RunTest(const FString&)
{
    FScriptPanelFixture Fixture;
    if (!TestTrue(TEXT("Fixture created"), Fixture.Initialize()))
    {
        return false;
    }
    int32 OldAnswers = 0;
    Fixture.View->OnAnswerRequested.AddLambda([&](int32, bool) { ++OldAnswers; });
    Fixture.View->ShowPrompt(ConstructModScriptSamples::Consent());
    TSharedPtr<SButton> OldRun = FindButton(Fixture.Widget.ToSharedRef(), TEXT("Run"));
    if (!TestTrue(TEXT("Actual consent button exists"), OldRun.IsValid()))
    {
        return false;
    }
    OldRun->SimulateClick();
    TestEqual(TEXT("Selecting a card is not consent"), OldAnswers, 0);
    const TSharedPtr<SButton> OldConfirm = FindButton(Fixture.Widget.ToSharedRef(), TEXT("Confirm choice"));
    TSharedRef<FConstructModScriptPresentation> Replacement = MakeShared<FConstructModScriptPresentation>();
    int32 NewAnswers = 0;
    Replacement->OnAnswerRequested.AddLambda([&](int32, bool) { ++NewAnswers; });
    Replacement->ShowPrompt(ConstructModScriptSamples::Consent());
    Fixture.Panel->SetPresentation(Replacement);
    if (OldConfirm)
    {
        OldConfirm->SimulateClick();
    }
    TestEqual(TEXT("Detached button cannot answer the new source's reused request id"), NewAnswers, 0);
    TestEqual(TEXT("Detached button cannot answer its old source"), OldAnswers, 0);
    TSharedPtr<SButton> NewRun = FindButton(Fixture.Widget.ToSharedRef(), TEXT("Run"));
    if (TestTrue(TEXT("Replacement button exists"), NewRun.IsValid()))
    {
        NewRun->SimulateClick();
        TestEqual(TEXT("Card selection alone starts no request"), NewAnswers, 0);
        const TSharedPtr<SButton> Confirm = FindButton(Fixture.Widget.ToSharedRef(), TEXT("Confirm choice"));
        if (TestTrue(TEXT("One confirmation button exists"), Confirm.IsValid()))
        {
            Confirm->SimulateClick();
            Confirm->SimulateClick();
        }
        TestEqual(TEXT("Explicit confirmation forwards one request, suppressing double click"), NewAnswers, 1);
        TestTrue(TEXT("Button starts no script by itself"), Replacement->GetCards().IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChoiceListTest, "Construct.ModScripts.UI.ChoiceCardsAreDataDriven", Flags)
bool FConstructChoiceListTest::RunTest(const FString&)
{
    int32 Requests = 0;
    FName Answer;
    bool bAllowed = true;
    const TArray<FConstructChoiceOption> Options = {
        {TEXT("first"), FText::FromString(TEXT("First")), FText::FromString(TEXT("First description"))},
        {TEXT("disabled"), FText::FromString(TEXT("Disabled")), FText::GetEmpty(), false},
        {TEXT("third"), FText::FromString(TEXT("An added option")), FText::FromString(TEXT("Added with data, not another layout branch"))}
    };
    const TSharedRef<SConstructChoiceList> Choices = SNew(SConstructChoiceList).Options(Options)
        .CanConfirm_Lambda([&]() { return bAllowed; })
        .OnConfirmed_Lambda([&](FName Key) { ++Requests; Answer = Key; });
    TestFalse(TEXT("Nothing is selected or agreed by default"), Choices->Confirm());
    TestFalse(TEXT("Disabled option cannot be selected"), Choices->Select(TEXT("disabled")));
    TestTrue(TEXT("Data-added option can be selected"), Choices->Select(TEXT("third")));
    TestEqual(TEXT("Selection alone emits no request"), Requests, 0);
    bAllowed = false;
    TestFalse(TEXT("Modal/source gate blocks confirmation"), Choices->Confirm());
    bAllowed = true;
    TestTrue(TEXT("Explicit confirmation emits selection"), Choices->Confirm());
    TestEqual(TEXT("Correct option key returned"), Answer, FName(TEXT("third")));
    TestEqual(TEXT("Exactly one confirmation"), Requests, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHudDialogTest, "Construct.ModScripts.UI.HudUpdatesPreserveControlsAndOverflow", Flags)
bool FConstructHudDialogTest::RunTest(const FString&)
{
    FScriptPanelFixture Fixture;
    if (!TestTrue(TEXT("Fixture created"), Fixture.Initialize()))
    {
        return false;
    }
    const FConstructScriptLabel Label{TEXT("live"), TEXT("Live source"), 41, 1};
    Fixture.View->Started(Label);
    FConstructScriptConsent Prompt = ConstructModScriptSamples::Consent();
    for (int32 Index = 0; Index < 30; ++Index)
    {
        Prompt.Calls.Add(FText::FromString(FString::Printf(TEXT("Capability %d"), Index)));
    }
    Fixture.View->ShowPrompt(Prompt);
    const TSharedPtr<SButton> Run = FindButton(Fixture.Widget.ToSharedRef(), TEXT("Run"));
    const TSharedPtr<SScrollBox> ConsentScroll = FindScroll(Fixture.Widget.ToSharedRef(), TEXT("Run Mod Scripts here?"));
    if (!TestTrue(TEXT("Consent button and details scroll exist"), Run.IsValid() && ConsentScroll.IsValid()))
    {
        return false;
    }
    ConsentScroll->SetScrollOffset(64);
    for (int32 Index = 0; Index < 100; ++Index)
    {
        Fixture.View->ShowHud(Label, FString::FromInt(Index));
    }
    TestTrue(TEXT("Frequent HUD updates retain the actual consent button"), FindButton(Fixture.Widget.ToSharedRef(), TEXT("Run")) == Run);
    TestTrue(TEXT("Frequent HUD updates retain the scroll container"), FindScroll(Fixture.Widget.ToSharedRef(), TEXT("Run Mod Scripts here?")) == ConsentScroll);
    TestEqual(TEXT("Consent scroll position retained"), ConsentScroll->GetScrollOffset(), 64.0f);
    Fixture.View->WithdrawPrompt(Prompt.RequestId);
    Fixture.Panel->ToggleRunningList();
    const TSharedPtr<SButton> Revoke = FindButton(Fixture.Widget.ToSharedRef(), TEXT("Revoke"));
    const FString FullText(TEXT("1\n2\n3\n4\n5\n6\n7\nFull last line"));
    Fixture.View->ShowHud(Label, FullText);
    TestTrue(TEXT("HUD updates retain the running-list controls"), FindButton(Fixture.Widget.ToSharedRef(), TEXT("Revoke")) == Revoke);
    const TSharedPtr<SScrollBox> List = FindScroll(Fixture.Widget.ToSharedRef(), TEXT("Revoke"));
    TestTrue(TEXT("Full HUD content can be reached in the interactive running list"), List.IsValid() && ContainsText(List.ToSharedRef(), FullText));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructConsentQueueTest, "Construct.ModScripts.UI.ConsentWaitsForExistingModal", Flags)
bool FConstructConsentQueueTest::RunTest(const FString&)
{
    FScriptPanelFixture Fixture;
    if (!TestTrue(TEXT("Fixture created"), Fixture.Initialize()))
    {
        return false;
    }
    Fixture.Gate->SetModal(TEXT("Chat"), true);
    Fixture.View->ShowPrompt(ConstructModScriptSamples::Consent());
    TestTrue(TEXT("An arriving prompt cannot block the existing chat's close/send controls"), Fixture.Gate->CanUseNavigationInput(TEXT("Chat")));
    TestFalse(TEXT("Consent waits rather than steals focus"), FindButton(Fixture.Widget.ToSharedRef(), TEXT("Run")).IsValid());
    Fixture.Gate->SetModal(TEXT("Chat"), false);
    TestTrue(TEXT("Queued consent appears when the existing modal closes"), FindButton(Fixture.Widget.ToSharedRef(), TEXT("Run")).IsValid());
    TestFalse(TEXT("Consent then owns input"), Fixture.Gate->IsGameplayInputAllowed());
    Fixture.View->WithdrawPrompt(1);
    TestTrue(TEXT("Closing consent restores gameplay"), Fixture.Gate->IsGameplayInputAllowed());
    return true;
}

#endif
