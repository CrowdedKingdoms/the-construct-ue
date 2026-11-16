#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConstructModScriptPresentation.h"
#include "ConstructModScriptPanel.generated.h"

class SBox;
class SOverlay;
class SConstructChoiceList;
class UConstructModScriptInput;
class UConstructUIInputSubsystem;

/** Plain-text script cards and shell-owned modal consent and running-list controls. */
UCLASS()
class CONSTRUCTMODSCRIPTS_API UConstructModScriptPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetPresentation(TSharedRef<FConstructModScriptPresentation> Source);
    void ToggleRunningList();
    void CloseRunningList();
    void Shutdown();
    void RefreshPresentation();
    void RefreshHud();
    bool IsRunningListOpen() const { return bListOpen; }
    UConstructModScriptInput* GetPointerInput() const { return PointerInput; }

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
    TSharedRef<SWidget> MakeHud();
    TSharedRef<SWidget> MakeConsent(const FConstructScriptConsent& Prompt);
    TSharedRef<SWidget> MakeRunningList();
    bool CanNavigate() const;
    UFUNCTION()
    void GameplayAdmissionChanged(bool bAllowed);
    void ReleaseModal();
    void UpdateModal();
    TSharedPtr<FConstructModScriptPresentation> Presentation;
    TSharedPtr<SOverlay> Root;
    TSharedPtr<SBox> HudBox;
    TSharedPtr<SBox> DialogBox;
    TSharedPtr<SConstructChoiceList> ConsentChoices;
    TWeakPtr<FConstructModScriptPresentation> ConsentChoiceSource;
    int32 ConsentChoiceRequest = INDEX_NONE;
    TWeakObjectPtr<UConstructUIInputSubsystem> ModalOwner;
    FDelegateHandle ChangedHandle;
    FDelegateHandle HudChangedHandle;
    UPROPERTY(Transient)
    TObjectPtr<UConstructModScriptInput> PointerInput;
    bool bListOpen = false;
    bool bConstructed = false;
    bool bPresentingConsent = false;
    bool bPresentingList = false;
};
