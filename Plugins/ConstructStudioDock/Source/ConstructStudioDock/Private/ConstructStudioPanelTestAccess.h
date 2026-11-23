#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioPanel.h"
#include "ConstructStudioPresentation.h"
#include "ConstructStudioViewportInset.h"
#include "Widgets/Layout/SBox.h"

struct FConstructStudioPanelTestAccess
{
    static void Ready(UConstructStudioPanel& Panel)
    {
        Panel.StateChanged(EConstructStudioDockState::Ready, EConstructStudioDockReason::None);
    }
    static void Bind(UConstructStudioPanel& Panel) { Panel.BindDock(); }
    static void Answer(UConstructStudioPanel& Panel, int64 PromptId, bool bRun) { Panel.AnswerPrompt(PromptId, bRun); }
    static void AllowRun(UConstructStudioPanel& Panel) { Panel.Presentation->PromptShownAt = 0.0; }
    static void Focus(UConstructStudioPanel& Panel, bool bFocused) { Panel.FocusChanged(bFocused); }
    static void Prompt(UConstructStudioPanel& Panel, const FConstructStudioDeployPrompt& Prompt) { Panel.DeployPrompted(Prompt); }
    static void Withdraw(UConstructStudioPanel& Panel, int64 Id) { Panel.PromptWithdrawn(Id); }
    static void Failed(UConstructStudioPanel& Panel) { Panel.StateChanged(EConstructStudioDockState::Failed, EConstructStudioDockReason::NoModel); }
    static const FConstructStudioPresentation& State(const UConstructStudioPanel& Panel) { return *Panel.Presentation; }
    static FText Toast(const UConstructStudioPanel& Panel) { return Panel.Toast; }
    static void Modal(UConstructStudioPanel& Panel, UConstructUIInputSubsystem* Owner)
    {
        Panel.ModalOwner = Owner;
        Panel.bConstructed = true;
    }
    static void Inset(UConstructStudioPanel& Panel, UGameViewportClient* Viewport, float Fraction) { Panel.Inset->Apply(Viewport, Fraction); }
    static void PreviewWidth(UConstructStudioPanel& Panel, float Width)
    {
        Panel.CurrentDockPixels = FConstructStudioLayout::Calculate(Width).DockPixels;
        Panel.LastDpiScale = 1.0f;
        Panel.LastViewportWidth = Width;
    }
    static void ProviderPreview(UConstructStudioPanel& Panel, const FText& Notice)
    {
        Panel.Presentation->State = EConstructStudioDockState::NeedsConsent;
        Panel.Presentation->Notice = Notice;
        Panel.UpdateModal();
        Panel.RefreshContent();
    }
    static void Tick(UConstructStudioPanel& Panel, float Width) { Panel.NativeTick(FGeometry::MakeRoot(FVector2D(Width, 720), FSlateLayoutTransform()), 1.0f / 60.0f); }
    static bool Context(const UConstructStudioPanel& Panel, int64& Grid, FText& Label)
    {
        FInt64Vector Chunk;
        return Panel.FindOwnedContext(Grid, Label, Chunk) == EConstructStudioOwnership::Owned;
    }
    static bool Shown(const UConstructStudioPanel& Panel) { return Panel.IsDockShown(); }
    static bool HoldsModal(const UConstructStudioPanel& Panel) { return Panel.ShouldHoldModal(); }
    static TSharedPtr<SWidget> Frame(const UConstructStudioPanel& Panel) { return Panel.FrameBox; }
    static TSharedPtr<SWidget> PromptArea(const UConstructStudioPanel& Panel) { return Panel.PromptBox; }
};

#endif

