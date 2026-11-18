#include "ConstructStudioPanel.h"

#include "ConstructClaimsSubsystem.h"
#include "ConstructShellStyle.h"
#include "ConstructStudioCaptureJob.h"
#include "ConstructStudioDock.h"
#include "ConstructStudioInput.h"
#include "ConstructStudioPresentation.h"
#include "ConstructStudioViewportInset.h"
#include "ConstructUIInputSubsystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/Char.h"
#include "Misc/CoreDelegates.h"
#include "Rendering/SlateRenderer.h"
#include "UnrealClient.h"
#include "Styling/CoreStyle.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructStudioPanel"

namespace
{
const FName StudioPanelModal(TEXT("StudioDock"));

TSharedRef<STextBlock> StudioText(const FText& Value, const TCHAR* Style = TEXT("Construct.Text.Body"))
{
    return SNew(STextBlock).Text(Value).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>(Style)).AutoWrapText(true);
}

TSharedRef<SButton> StudioButton(const FText& Label, FOnClicked Clicked, bool bPrimary = false)
{
    TSharedRef<STextBlock> Caption = StudioText(Label);
    Caption->SetColorAndOpacity(FSlateColor::UseForeground());
    return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(12, 8)).HAlign(HAlign_Center).VAlign(VAlign_Center)
        .ButtonStyle(&FConstructShellStyle::Get().GetWidgetStyle<FButtonStyle>(bPrimary ? TEXT("Construct.Button.Primary") : TEXT("Construct.Button.Secondary")))
        .OnClicked(Clicked)[Caption];
}

float StudioTwoLineHeight(const FSlateFontInfo& Font)
{
    const FSlateRenderer* Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
    const float Line = Renderer ? Renderer->GetFontMeasureService()->GetMaxCharacterHeight(Font) : Font.Size * 1.6f;
    return 2.0f * Line + 2.0f;
}

class SConstructStudioResizeGrip final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SConstructStudioResizeGrip) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& Args, TWeakObjectPtr<UConstructStudioPanel> InPanel)
    {
        Panel = InPanel;
        SetCursor(EMouseCursor::ResizeLeftRight);
        ChildSlot[SNew(SBox).WidthOverride(8)[SNew(SBorder).BorderImage(FConstructShellStyle::Get().GetBrush("Construct.Line"))]];
    }

    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        return Event.GetEffectingButton() == EKeys::LeftMouseButton ? FReply::Handled().CaptureMouse(AsShared()) : FReply::Unhandled();
    }

    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (HasMouseCapture())
        {
            if (UConstructStudioPanel* Owner = Panel.Get())
            {
                Owner->ResizeFromScreenPosition(Event.GetScreenSpacePosition());
            }
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        return HasMouseCapture() && Event.GetEffectingButton() == EKeys::LeftMouseButton ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
    }

private:
    TWeakObjectPtr<UConstructStudioPanel> Panel;
};
}

UConstructStudioPanel::UConstructStudioPanel(const FObjectInitializer& Initializer) : Super(Initializer)
{
    Presentation = MakePimpl<FConstructStudioPresentation>();
    Inset = MakePimpl<FConstructStudioViewportInset>();
}

UConstructStudioPanel::~UConstructStudioPanel() = default;

TSharedRef<SWidget> UConstructStudioPanel::RebuildWidget()
{
    if (!Dock)
    {
        Dock = NewObject<UConstructStudioDock>(this);
    }
    const TWeakObjectPtr<UConstructStudioPanel> WeakThis(this);
    const FTextBlockStyle& WarningStyle = FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>("Construct.Text.Error");
    TSharedRef<SButton> ScreenshotButton = StudioButton(LOCTEXT("Screenshot", "Screenshot"), FOnClicked::CreateWeakLambda(this, [this]()
    {
        if (Dock && Presentation->State == EConstructStudioDockState::Ready && CanNavigate())
        {
            Dock->ShareScreenshot();
        }
        return FReply::Handled();
    }));
    ScreenshotButton->SetEnabled(TAttribute<bool>::CreateLambda([WeakThis]()
    {
        const UConstructStudioPanel* Owner = WeakThis.Get();
        return Owner && Owner->Presentation->State == EConstructStudioDockState::Ready && Owner->CanNavigate();
    }));
    SAssignNew(Root, SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill)
        [
            SAssignNew(FrameBox, SBox)
            .WidthOverride_Lambda([WeakThis]() { const UConstructStudioPanel* Owner = WeakThis.Get(); return Owner ? Owner->DockSlateWidth() : 0.0f; })
            .Visibility_Lambda([WeakThis]()
            {
                const UConstructStudioPanel* Owner = WeakThis.Get();
                return Owner && Owner->IsDockShown() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
            })
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SConstructStudioResizeGrip, WeakThis)
                    .Visibility_Lambda([WeakThis]() { const UConstructStudioPanel* Owner = WeakThis.Get(); return Owner && !Owner->bFullscreen ? EVisibility::Visible : EVisibility::Collapsed; })
                ]
                + SHorizontalBox::Slot().FillWidth(1)
                [
                    SNew(SBorder).BorderImage(FConstructShellStyle::Get().GetBrush("Construct.Panel")).Padding(16)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[StudioText(LOCTEXT("Title", "Studio"), TEXT("Construct.Text.Heading"))]
                            + SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
                            [ScreenshotButton]
                            + SHorizontalBox::Slot().AutoWidth()
                            [StudioButton(LOCTEXT("Close", "Close"), FOnClicked::CreateWeakLambda(this, [this]() { CloseStudio(); return FReply::Handled(); }))]
                        ]
                        + SVerticalBox::Slot().FillHeight(1)[Dock->TakeWidget()]
                        + SVerticalBox::Slot().FillContentHeight(1.0f).MaxHeight(440.0f).Padding(0, 12, 0, 0)
                        [
                            SAssignNew(PromptBox, SBox)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                        [
                            SNew(SBox).MaxDesiredHeight(StudioTwoLineHeight(WarningStyle.Font))
                            .Visibility_Lambda([WeakThis]() { const UConstructStudioPanel* Owner = WeakThis.Get(); return Owner && Owner->Presentation->ShowsWarning() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                            [
                                SNew(STextBlock).TextStyle(&WarningStyle).AutoWrapText(true)
                                .Clipping(EWidgetClipping::ClipToBounds).OverflowPolicy(ETextOverflowPolicy::MultilineEllipsis)
                                .Text_Lambda([WeakThis]() { const UConstructStudioPanel* Owner = WeakThis.Get(); return Owner ? Owner->Presentation->Warning : FText::GetEmpty(); })
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                            [
                                SNew(STextBlock).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>("Construct.Text.Small")).AutoWrapText(true)
                                .Text_Lambda([WeakThis, Pending = LOCTEXT("UsagePending", "Usage appears when the agent is ready.")]()
                                {
                                    const UConstructStudioPanel* Owner = WeakThis.Get();
                                    return Owner && !Owner->Presentation->UsageText.IsEmpty() ? Owner->Presentation->UsageText : Pending;
                                })
                            ]
                            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
                            [StudioButton(LOCTEXT("Wallet", "Wallet"), FOnClicked::CreateWeakLambda(this, [this]() { if (Dock && CanNavigate()) { Dock->OpenWallet(); } return FReply::Handled(); }))]
                        ]
                    ]
                ]
            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(24, 32)
        [
            SNew(SBox).MaxDesiredWidth(600)
            .Visibility_Lambda([WeakThis]() { const UConstructStudioPanel* Owner = WeakThis.Get(); return Owner && !Owner->Toast.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [
                SNew(SBorder).BorderImage(FConstructShellStyle::Get().GetBrush("Construct.Panel")).Padding(16)
                [
                    SNew(STextBlock).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>("Construct.Text.Body")).AutoWrapText(true)
                    .Text_Lambda([WeakThis]() { const UConstructStudioPanel* Owner = WeakThis.Get(); return Owner ? Owner->Toast : FText::GetEmpty(); })
                ]
            ]
        ];
    return Root.ToSharedRef();
}

void UConstructStudioPanel::NativeConstruct()
{
    Super::NativeConstruct();
    bShuttingDown = false;
    bConstructed = true;
    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if (ULocalPlayer* Player = GetOwningLocalPlayer())
    {
        ModalOwner = Player->GetSubsystem<UConstructUIInputSubsystem>();
        if (UConstructUIInputSubsystem* Owner = ModalOwner.Get())
        {
            Owner->OnGameplayInputChanged.AddUniqueDynamic(this, &ThisClass::GameplayAdmissionChanged);
        }
    }
    if (FSlateApplication::IsInitialized() && !SlateActivationHandle.IsValid())
    {
        SlateActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &ThisClass::ApplicationActivationChanged);
    }
    if (!DeactivatedHandle.IsValid())
    {
        DeactivatedHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddWeakLambda(this, [this]() { ApplicationActivationChanged(false); });
        ReactivatedHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddWeakLambda(this, [this]() { ApplicationActivationChanged(true); });
    }
    BindDock();
    if (APlayerController* Controller = GetOwningPlayer())
    {
        ToggleInput = NewObject<UConstructStudioInput>(Controller);
        ToggleInput->OnToggle.BindUObject(this, &ThisClass::ToggleStudio);
        ToggleInput->Attach(Controller);
    }
    RefreshContent();
}

void UConstructStudioPanel::BindDock()
{
    if (!Dock || bBound)
    {
        return;
    }
    bBound = true;
    TScriptInterface<IConstructStudioDockHost> Host;
    Host.SetObject(this);
    Host.SetInterface(this);
    Dock->SetHost(Host);
    Dock->OnStateChanged.AddUniqueDynamic(this, &ThisClass::StateChanged);
    Dock->OnDeployPrompt.AddUniqueDynamic(this, &ThisClass::DeployPrompted);
    Dock->OnDeployPromptWithdrawn.AddUniqueDynamic(this, &ThisClass::PromptWithdrawn);
    Dock->OnUsageChanged.AddUniqueDynamic(this, &ThisClass::UsageChanged);
    Dock->OnWarning.AddUniqueDynamic(this, &ThisClass::WarningRaised);
    Dock->OnFocusChanged.AddUniqueDynamic(this, &ThisClass::FocusChanged);
    Dock->OnCloseRequested.AddUniqueDynamic(this, &ThisClass::CloseRequested);
}

void UConstructStudioPanel::UnbindDock()
{
    if (!Dock || !bBound)
    {
        return;
    }
    bBound = false;
    Dock->OnStateChanged.RemoveAll(this);
    Dock->OnDeployPrompt.RemoveAll(this);
    Dock->OnDeployPromptWithdrawn.RemoveAll(this);
    Dock->OnUsageChanged.RemoveAll(this);
    Dock->OnWarning.RemoveAll(this);
    Dock->OnFocusChanged.RemoveAll(this);
    Dock->OnCloseRequested.RemoveAll(this);
    Dock->SetHost(TScriptInterface<IConstructStudioDockHost>());
}

bool UConstructStudioPanel::CanNavigate() const
{
    const UConstructUIInputSubsystem* Owner = ModalOwner.Get();
    return Owner && Owner->CanUseNavigationInput(StudioPanelModal);
}

bool UConstructStudioPanel::CanShowDock() const
{
    // Switching away from the game keeps the dock as it was rather than dropping it.
    return bApplicationActive ? CanNavigate() : bLastNavigationAllowed;
}

bool UConstructStudioPanel::IsDockShown() const
{
    return Presentation->IsOpen() && CanShowDock();
}

bool UConstructStudioPanel::ShouldHoldModal() const
{
    // A prompt that arrives behind another menu waits for that menu to release navigation.
    return Presentation->HoldsModal(bFullscreen) && CanShowDock();
}

void UConstructStudioPanel::SyncPromptVisibility()
{
    Presentation->ShowPrompt(bApplicationActive && IsDockShown(), FPlatformTime::Seconds());
}

void UConstructStudioPanel::ApplicationActivationChanged(bool bActive)
{
    bApplicationActive = bActive;
    // A click that reactivates the window lands after this, so Run starts its guard again.
    SyncPromptVisibility();
}

void UConstructStudioPanel::ToggleStudio()
{
    if (!CanNavigate())
    {
        return;
    }
    if (Presentation->IsOpen())
    {
        CloseStudio();
    }
    else
    {
        OpenFromOwnedChunk();
    }
}

EConstructStudioOwnership UConstructStudioPanel::FindOwnedContext(int64& GridId, FText& GridLabel, FInt64Vector& Chunk) const
{
    GridId = 0;
    GridLabel = FText::GetEmpty();
    Chunk = FInt64Vector::ZeroValue;
    const APlayerController* Controller = GetOwningPlayer();
    const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    UWorld* World = GetWorld();
    const UConstructClaimsSubsystem* Claims = World ? World->GetSubsystem<UConstructClaimsSubsystem>() : nullptr;
    if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Pawn->GetActorLocation().ContainsNaN() || !Claims)
    {
        return EConstructStudioOwnership::NoPlayer;
    }
    FIntVector Cell;
    UCrowdyVoxelLibrary::WorldToVoxel(this, Pawn->GetActorLocation(), Chunk.X, Chunk.Y, Chunk.Z, Cell);
    const FConstructClaimLookup* Lookup = Claims->GetLookup(Chunk);
    if (!Lookup || !Claims->IsLatestLookup(Chunk, Lookup->Request))
    {
        return EConstructStudioOwnership::NotOwned;
    }
    if (Lookup->bPending)
    {
        return EConstructStudioOwnership::Pending;
    }
    if (!Lookup->bSucceeded || !Lookup->Claim.IsSet() || !Lookup->Claim->bOwnedByMe || Lookup->Claim->GridId <= 0)
    {
        return EConstructStudioOwnership::NotOwned;
    }
    GridId = Lookup->Claim->GridId;
    const FNumberFormattingOptions* Plain = &FNumberFormattingOptions::DefaultNoGrouping();
    GridLabel = FText::Format(LOCTEXT("ClaimChunk", "Chunk ({0}, {1}, {2})"), FText::AsNumber(Chunk.X, Plain), FText::AsNumber(Chunk.Y, Plain), FText::AsNumber(Chunk.Z, Plain));
    return EConstructStudioOwnership::Owned;
}

void UConstructStudioPanel::OpenFromOwnedChunk()
{
    if (!Dock || !CanNavigate() || bShuttingDown)
    {
        return;
    }
    int64 GridId = 0;
    FText GridLabel;
    FInt64Vector Chunk;
    const EConstructStudioOwnership Ownership = FindOwnedContext(GridId, GridLabel, Chunk);
    if (Ownership != EConstructStudioOwnership::Owned)
    {
        ShowToast(LOCTEXT("OwnClaimRequired", "Studio is available on a chunk you have claimed. Ownership must be confirmed first."));
        UConstructClaimsSubsystem* Claims = GetWorld() ? GetWorld()->GetSubsystem<UConstructClaimsSubsystem>() : nullptr;
        if (Ownership == EConstructStudioOwnership::NotOwned && Claims)
        {
            Claims->ConfirmChunk(Chunk);
        }
        return;
    }
    AppliedGridId = GridId;
    AppliedChunk = Chunk;
    const EConstructStudioDockReason Reason = Dock->Open(GridId, GridLabel);
    if (Reason != EConstructStudioDockReason::None)
    {
        AppliedGridId = 0;
        ShowToast(UConstructStudioDock::DescribeReason(Reason));
    }
    else if (Presentation->IsOpen())
    {
        Dock->SetUserFocus(GetOwningPlayer());
    }
}

void UConstructStudioPanel::RefreshOwnedChunk()
{
    if (!Dock || !Presentation->IsOpen() || bShuttingDown)
    {
        return;
    }
    int64 GridId = 0;
    FText Label;
    FInt64Vector Chunk;
    const EConstructStudioOwnership Ownership = FindOwnedContext(GridId, Label, Chunk);
    // Another menu may look up the applied chunk again; the dock waits for that answer.
    if (Ownership == EConstructStudioOwnership::Pending && AppliedGridId > 0 && Chunk == AppliedChunk)
    {
        return;
    }
    if (Ownership != EConstructStudioOwnership::Owned)
    {
        CloseStudio();
        ShowToast(LOCTEXT("ClaimLost", "Studio closed because ownership of this chunk is not confirmed."));
        return;
    }
    AppliedChunk = Chunk;
    if (GridId != AppliedGridId)
    {
        AppliedGridId = GridId;
        Dock->SetGrid(GridId, Label);
    }
}

void UConstructStudioPanel::CloseStudio()
{
    for (UConstructStudioCaptureJob* Capture : Captures)
    {
        if (Capture)
        {
            Capture->Cancel();
        }
    }
    Captures.Reset();
    if (Dock)
    {
        Dock->Close();
    }
    Presentation->Reset();
    AppliedGridId = 0;
    RestoreLayout();
    UpdateModal();
    RefreshContent();
}

void UConstructStudioPanel::StateChanged(EConstructStudioDockState State, EConstructStudioDockReason Reason)
{
    if (bShuttingDown)
    {
        return;
    }
    if (State == EConstructStudioDockState::Failed)
    {
        const FText Message = Dock && !Dock->GetReasonText().IsEmpty() ? Dock->GetReasonText() : UConstructStudioDock::DescribeReason(Reason);
        CloseStudio();
        ShowToast(Message);
        return;
    }
    if (State == EConstructStudioDockState::Closed)
    {
        for (UConstructStudioCaptureJob* Capture : Captures)
        {
            if (Capture)
            {
                Capture->Cancel();
            }
        }
        Captures.Reset();
        Presentation->Reset();
        AppliedGridId = 0;
        RestoreLayout();
    }
    else
    {
        Presentation->State = State;
        Presentation->Notice = State == EConstructStudioDockState::NeedsConsent && Dock ? Dock->GetConsentNotice() : FText::GetEmpty();
    }
    RefreshLayout();
    UpdateModal();
    RefreshContent();
}

void UConstructStudioPanel::DeployPrompted(const FConstructStudioDeployPrompt& Prompt)
{
    Presentation->Present(Prompt, FPlatformTime::Seconds(), bApplicationActive && IsDockShown());
    // Acquire our registration before focus can leave the browser for this native prompt.
    UpdateModal();
    RefreshContent();
}

void UConstructStudioPanel::PromptWithdrawn(int64 PromptId)
{
    Presentation->Withdraw(PromptId);
    UpdateModal();
    RefreshContent();
}

void UConstructStudioPanel::UsageChanged(const FConstructStudioUsage& Usage)
{
    if (Usage.Payer.IsEmpty())
    {
        Presentation->UsageText = FText::GetEmpty();
        return;
    }
    Presentation->UsageText = FText::Format(LOCTEXT("Usage", "Today: {0} requests | Charge: {1} | Limit: {2} | Paid by: {3}"), FText::AsNumber(Usage.TodayRequests),
        FText::FromString(Usage.TodayCharge), FText::FromString(Usage.DayLimit.IsEmpty() ? TEXT("none") : Usage.DayLimit), FText::FromString(Usage.Payer));
}

void UConstructStudioPanel::WarningRaised(const FText& Text)
{
    Presentation->Warning = Text.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("AgentWarning", "Agent: {0}"), Text);
}

void UConstructStudioPanel::FocusChanged(bool bFocused)
{
    Presentation->bBrowserFocused = bFocused;
    UpdateModal();
}

void UConstructStudioPanel::CloseRequested()
{
    CloseStudio();
}

void UConstructStudioPanel::GameplayAdmissionChanged(bool bAllowed)
{
    UpdateModal();
}

void UConstructStudioPanel::UpdateModal()
{
    if (!bConstructed || bShuttingDown)
    {
        return;
    }
    if (UConstructUIInputSubsystem* Owner = ModalOwner.Get())
    {
        // Keep the browser focused while registering its modal. Only a native prompt needs our root.
        UUserWidget* Focus = Presentation->HasNativePrompt() && !Presentation->bBrowserFocused ? this : nullptr;
        Owner->SetModal(StudioPanelModal, ShouldHoldModal(), Focus);
    }
}

void UConstructStudioPanel::AcceptProviderNotice()
{
    if (Dock && Presentation->State == EConstructStudioDockState::NeedsConsent && CanNavigate())
    {
        Dock->GiveProviderConsent();
    }
}

void UConstructStudioPanel::AnswerPrompt(int64 PromptId, bool bRun)
{
    if (!Dock || !Presentation->Prompt.IsSet() || Presentation->Prompt->PromptId != PromptId || !CanNavigate() || (bRun && !Presentation->CanRun(FPlatformTime::Seconds())))
    {
        return;
    }
    Presentation->Prompt.Reset();
    // Native prompts consume their own answer; the core's withdrawal event is not an acknowledgement.
    Dock->AnswerDeployPrompt(PromptId, bRun);
    UpdateModal();
    RefreshContent();
}

TSharedRef<SWidget> UConstructStudioPanel::MakeNativePrompt()
{
    if (Presentation->State == EConstructStudioDockState::NeedsConsent)
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().FillHeight(1)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()[StudioText(Presentation->Notice)]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
            [StudioButton(LOCTEXT("AcceptProvider", "I understand, start the agent"), FOnClicked::CreateWeakLambda(this, [this]() { AcceptProviderNotice(); return FReply::Handled(); }), true)];
    }
    if (!Presentation->Prompt.IsSet())
    {
        return SNullWidget::NullWidget;
    }
    const FConstructStudioDeployPrompt& Prompt = Presentation->Prompt.GetValue();
    FString Files;
    for (const FString& File : Prompt.ChangedFiles)
    {
        Files += File + TEXT("\n");
    }
    const FText Heading = FText::Format(LOCTEXT("DeployHeading", "{0}\nLand: {1} (ID {2})"),
        Prompt.Kind == EConstructStudioDeployKind::DraftTest ? LOCTEXT("Draft", "Run a draft test? This deploys code to this land.") : LOCTEXT("Live", "Deploy this project live?"),
        Prompt.GridLabel, FText::AsNumber(Prompt.GridId, &FNumberFormattingOptions::DefaultNoGrouping()));
    FString ProjectName = Prompt.ProjectName;
    for (TCHAR& Character : ProjectName)
    {
        if (FChar::IsLinebreak(Character))
        {
            Character = TEXT(' ');
        }
    }
    const FText Project = FText::Format(LOCTEXT("DeployProject", "Project: {0}"), FText::AsCultureInvariant(ProjectName));
    const FText Details = FText::Format(LOCTEXT("DeployDetails", "Project ID: {0}\nSaved revision: {1}\n{2}\n\nChanged files:\n{3}"),
        FText::AsCultureInvariant(Prompt.ProjectId), FText::AsCultureInvariant(Prompt.Revision),
        Prompt.bReachesVisitors ? LOCTEXT("Visitors", "Includes client code that can run for visitors.") : LOCTEXT("ServerOnly", "Server code only."),
        Files.IsEmpty() ? LOCTEXT("NoFiles", "No files changed in this session.") : FText::FromString(Files));
    const int64 PromptId = Prompt.PromptId;
    const TWeakObjectPtr<UConstructStudioPanel> WeakThis(this);
    TSharedRef<SButton> Run = StudioButton(LOCTEXT("Run", "Run"), FOnClicked::CreateWeakLambda(this, [this, PromptId]() { AnswerPrompt(PromptId, true); return FReply::Handled(); }), true);
    Run->SetEnabled(TAttribute<bool>::CreateLambda([WeakThis]()
    {
        const UConstructStudioPanel* Owner = WeakThis.Get();
        return Owner && Owner->CanNavigate() && Owner->Presentation->CanRun(FPlatformTime::Seconds());
    }));
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()[StudioText(Heading)]
            + SScrollBox::Slot()
            [
                SNew(STextBlock).Text(Project).TextStyle(&FConstructShellStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Construct.Text.Body")))
                .Clipping(EWidgetClipping::ClipToBounds).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            ]
            + SScrollBox::Slot()[StudioText(Details)]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
            [StudioButton(LOCTEXT("NotNow", "Not now"), FOnClicked::CreateWeakLambda(this, [this, PromptId]() { AnswerPrompt(PromptId, false); return FReply::Handled(); }))]
            + SHorizontalBox::Slot().FillWidth(1).Padding(6, 0, 0, 0)[Run]
        ];
}

void UConstructStudioPanel::RefreshContent()
{
    if (PromptBox)
    {
        PromptBox->SetContent(MakeNativePrompt());
        PromptBox->SetVisibility(Presentation->HasNativePrompt() ? EVisibility::Visible : EVisibility::Collapsed);
    }
}

void UConstructStudioPanel::RefreshLayout(float FallbackWindowWidth)
{
    const APlayerController* Controller = GetOwningPlayer();
    ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    UGameViewportClient* Viewport = Player ? Player->ViewportClient : nullptr;
    const float ActualWidth = Viewport && Viewport->Viewport ? static_cast<float>(Viewport->Viewport->GetSizeXY().X) : 0.0f;
    const float Width = ActualWidth > 0.0f ? ActualWidth : FMath::Max(FallbackWindowWidth, 0.0f);
    LastViewportWidth = Width;
    LastDpiScale = GetWorld() ? UWidgetLayoutLibrary::GetViewportScale(this) : 1.0f;
    const FConstructStudioLayout Layout = FConstructStudioLayout::Calculate(Width, RequestedFraction);
    bFullscreen = Layout.bFullscreen;
    CurrentDockPixels = Layout.DockPixels;
    bLastNavigationAllowed = CanShowDock();
    if (Presentation->IsOpen() && Viewport && bLastNavigationAllowed)
    {
        Inset->Apply(Viewport, Layout.GameFraction);
    }
    else
    {
        RestoreLayout();
    }
}

void UConstructStudioPanel::RestoreLayout()
{
    Inset->Restore();
}

float UConstructStudioPanel::DockSlateWidth() const
{
    return CurrentDockPixels / FMath::Max(LastDpiScale, 0.01f);
}

void UConstructStudioPanel::ResizeFromScreenPosition(FVector2D Position)
{
    if (!Root || bFullscreen || LastViewportWidth <= 0 || !CanNavigate())
    {
        return;
    }
    const FVector2D Local = Root->GetTickSpaceGeometry().AbsoluteToLocal(Position);
    const float Pixels = (Root->GetTickSpaceGeometry().GetLocalSize().X - Local.X) * FMath::Max(LastDpiScale, 0.01f);
    RequestedFraction = Pixels / LastViewportWidth;
    RefreshLayout();
    UpdateModal();
}

void UConstructStudioPanel::ShowToast(const FText& Message)
{
    Toast = Message;
    ToastUntil = FPlatformTime::Seconds() + 5.0;
}

void UConstructStudioPanel::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (!Toast.IsEmpty() && FPlatformTime::Seconds() >= ToastUntil)
    {
        Toast = FText::GetEmpty();
    }
    Captures.RemoveAll([](const UConstructStudioCaptureJob* Capture) { return !Capture || Capture->IsFinished(); });
    const ULocalPlayer* Player = GetOwningLocalPlayer();
    const UGameViewportClient* Viewport = Player ? Player->ViewportClient : nullptr;
    const float Scale = GetWorld() ? UWidgetLayoutLibrary::GetViewportScale(this) : 1.0f;
    const float ActualWidth = Viewport && Viewport->Viewport ? static_cast<float>(Viewport->Viewport->GetSizeXY().X) : 0.0f;
    const float Width = ActualWidth > 0.0f ? ActualWidth : static_cast<float>(Geometry.GetLocalSize().X * Scale);
    if (Width != LastViewportWidth || Scale != LastDpiScale || CanShowDock() != bLastNavigationAllowed)
    {
        RefreshLayout(Width);
        UpdateModal();
    }
    SyncPromptVisibility();
}

void UConstructStudioPanel::CaptureView_Implementation(const FString& Label, const FOnConstructStudioCaptured& OnDone)
{
    const bool bBusy = Captures.ContainsByPredicate([](const UConstructStudioCaptureJob* Capture) { return Capture && !Capture->IsFinished(); });
    UConstructStudioCaptureJob* Job = nullptr;
    if (!IsDockShown())
    {
        Job = UConstructStudioCaptureJob::RefuseLater(this, GetWorld(), Label, OnDone, TEXT("The Studio dock is not showing to the player, so no screenshot was taken."));
    }
    else if (bBusy)
    {
        Job = UConstructStudioCaptureJob::RefuseLater(this, GetWorld(), Label, OnDone, TEXT("A screenshot is already being captured."));
    }
    else
    {
        Job = UConstructStudioCaptureJob::Begin(this, GetOwningPlayer(), Label, OnDone);
    }
    if (Job)
    {
        Captures.Add(Job);
    }
}

void UConstructStudioPanel::Shutdown()
{
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    UnbindDock();
    if (Dock)
    {
        Dock->Close();
    }
    for (UConstructStudioCaptureJob* Capture : Captures)
    {
        if (Capture)
        {
            Capture->Cancel();
        }
    }
    Captures.Reset();
    if (ToggleInput)
    {
        ToggleInput->Detach();
        ToggleInput->OnToggle.Unbind();
        ToggleInput = nullptr;
    }
    if (UConstructUIInputSubsystem* Owner = ModalOwner.Get())
    {
        Owner->OnGameplayInputChanged.RemoveAll(this);
        Owner->SetModal(StudioPanelModal, false);
    }
    ModalOwner.Reset();
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(SlateActivationHandle);
    }
    SlateActivationHandle.Reset();
    FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivatedHandle);
    FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivatedHandle);
    DeactivatedHandle.Reset();
    ReactivatedHandle.Reset();
    bApplicationActive = true;
    RestoreLayout();
    Presentation->Reset();
    bConstructed = false;
}

void UConstructStudioPanel::NativeDestruct()
{
    Shutdown();
    Super::NativeDestruct();
}

void UConstructStudioPanel::ReleaseSlateResources(bool bReleaseChildren)
{
    Shutdown();
    Root.Reset();
    FrameBox.Reset();
    PromptBox.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

#undef LOCTEXT_NAMESPACE

