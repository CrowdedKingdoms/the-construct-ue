#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConstructStudioDockHost.h"
#include "ConstructStudioDockTypes.h"
#include "Templates/PimplPtr.h"
#include "ConstructStudioPanel.generated.h"

class UConstructStudioDock;
class UConstructStudioCaptureJob;
class UConstructStudioInput;
class UConstructUIInputSubsystem;
class SBox;
class SOverlay;
struct FConstructStudioPresentation;
struct FConstructStudioViewportInset;

/** What the latest ownership lookup says about the chunk the player stands in. */
enum class EConstructStudioOwnership : uint8
{
    Owned,
    Pending,
    NotOwned,
    NoPlayer,
};

/** The local Studio frame and native human approval, composed around the core's real widget. */
UCLASS()
class CONSTRUCTSTUDIODOCK_API UConstructStudioPanel : public UUserWidget, public IConstructStudioDockHost
{
    GENERATED_BODY()

public:
    UConstructStudioPanel(const FObjectInitializer& Initializer);
    virtual ~UConstructStudioPanel() override;

    UFUNCTION(BlueprintCallable, Category = "Construct|Studio")
    void ToggleStudio();
    UFUNCTION(BlueprintCallable, Category = "Construct|Studio")
    void OpenFromOwnedChunk();
    UFUNCTION(BlueprintCallable, Category = "Construct|Studio")
    void CloseStudio();
    UFUNCTION(BlueprintPure, Category = "Construct|Studio")
    UConstructStudioDock* GetDock() const { return Dock; }

    virtual void CaptureView_Implementation(const FString& Label, const FOnConstructStudioCaptured& OnDone) override;
    void Shutdown();
    void RefreshOwnedChunk();
    void ResizeFromScreenPosition(FVector2D Position);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    friend struct FConstructStudioPanelTestAccess;
    void BindDock();
    void UnbindDock();
    void RefreshContent();
    void RefreshLayout(float FallbackWindowWidth = 0.0f);
    void UpdateModal();
    void RestoreLayout();
    void ShowToast(const FText& Message);
    bool CanNavigate() const;
    bool CanShowDock() const;
    bool IsDockShown() const;
    bool ShouldHoldModal() const;
    void SyncPromptVisibility();
    void ApplicationActivationChanged(bool bActive);
    EConstructStudioOwnership FindOwnedContext(int64& GridId, FText& GridLabel, FInt64Vector& Chunk) const;
    void AcceptProviderNotice();
    void AnswerPrompt(int64 PromptId, bool bRun);
    TSharedRef<SWidget> MakeNativePrompt();
    float DockSlateWidth() const;

    UFUNCTION()
    void StateChanged(EConstructStudioDockState State, EConstructStudioDockReason Reason);
    UFUNCTION()
    void DeployPrompted(const FConstructStudioDeployPrompt& Prompt);
    UFUNCTION()
    void PromptWithdrawn(int64 PromptId);
    UFUNCTION()
    void UsageChanged(const FConstructStudioUsage& Usage);
    UFUNCTION()
    void WarningRaised(const FText& Text);
    UFUNCTION()
    void FocusChanged(bool bFocused);
    UFUNCTION()
    void CloseRequested();
    UFUNCTION()
    void GameplayAdmissionChanged(bool bAllowed);

    UPROPERTY(Transient)
    TObjectPtr<UConstructStudioDock> Dock;
    UPROPERTY(Transient)
    TObjectPtr<UConstructStudioInput> ToggleInput;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UConstructStudioCaptureJob>> Captures;
    TWeakObjectPtr<UConstructUIInputSubsystem> ModalOwner;
    // Pimpl, not unique, pointers: the generated constructor destroys them where only their forward declarations are seen.
    TPimplPtr<FConstructStudioPresentation> Presentation;
    TPimplPtr<FConstructStudioViewportInset> Inset;
    TSharedPtr<SOverlay> Root;
    TSharedPtr<SBox> FrameBox;
    TSharedPtr<SBox> PromptBox;
    FText Toast;
    FDelegateHandle SlateActivationHandle;
    FDelegateHandle DeactivatedHandle;
    FDelegateHandle ReactivatedHandle;
    FInt64Vector AppliedChunk = FInt64Vector::ZeroValue;
    double ToastUntil = 0.0;
    float RequestedFraction = 0.52f;
    float LastViewportWidth = -1.0f;
    float LastDpiScale = -1.0f;
    float CurrentDockPixels = 0.0f;
    int64 AppliedGridId = 0;
    bool bLastNavigationAllowed = false;
    bool bApplicationActive = true;
    bool bFullscreen = false;
    bool bBound = false;
    bool bConstructed = false;
    bool bShuttingDown = false;
};

