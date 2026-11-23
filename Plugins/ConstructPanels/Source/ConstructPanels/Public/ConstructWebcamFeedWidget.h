#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConstructWebcamTypes.h"
#include "Styling/SlateBrush.h"
#include "ConstructWebcamFeedWidget.generated.h"

class AConstructPresencePawn;
class UConstructWebcamPanel;

/** A current video frame and its safety actions, anchored over one presence pawn. */
UCLASS()
class CONSTRUCTPANELS_API UConstructWebcamFeedWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void BindFeed(UConstructWebcamPanel* Panel, AConstructPresencePawn* Pawn);
    void SetFeed(const FConstructWebcamFeed& Feed);
    bool HasPicture() const { return Brush.GetResourceObject() != nullptr; }
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TWeakObjectPtr<UConstructWebcamPanel> Panel;
    TWeakObjectPtr<AConstructPresencePawn> Pawn;
    UPROPERTY(Transient)
    FSlateBrush Brush;
};
