#pragma once

#include "Blueprint/UserWidget.h"
#include "ConstructHubStatusWidget.generated.h"

class SBorder;
class STextBlock;

/** The world pulse and player level shown on the local player's HUD. */
UCLASS()
class CONSTRUCTHUB_API UConstructHubStatusWidget final : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetHubStatus(TOptional<int64> Pulses, TOptional<int64> Level);
    const FText& GetStatusText() const { return StatusText; }

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    void UpdatePresentation();

    TSharedPtr<SBorder> Indicator;
    TSharedPtr<STextBlock> Label;
    FText StatusText;
    bool bConnected = false;
};
