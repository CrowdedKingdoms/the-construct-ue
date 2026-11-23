#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructPaintControls.generated.h"

class AConstructPaintCanvas;
class APlayerController;
class SWidget;
class UGameViewportClient;
class UInputComponent;
class ULocalPlayer;

/** Local Paint input and palette, enabled by placing this actor in the Paint level. */
UCLASS()
class CONSTRUCTWORLD_API AConstructPaintControls final : public AActor
{
    GENERATED_BODY()

public:
    AConstructPaintControls();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    void AttachToController(APlayerController* Controller);
    void ReleasePresentation();
    TSharedRef<SWidget> BuildPalette();
    bool CanPaint() const;
    void PaintUnderCursor();

    UPROPERTY(EditInstanceOnly, Category = "Construct|Paint")
    TObjectPtr<AConstructPaintCanvas> Canvas;

    UPROPERTY(EditInstanceOnly, Category = "Construct|Paint")
    TObjectPtr<AActor> Floor;

    UPROPERTY(Transient)
    TObjectPtr<UInputComponent> PaintInput;

    TWeakObjectPtr<ULocalPlayer> LocalPlayer;
    TWeakObjectPtr<APlayerController> InputController;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    FDelegateHandle ControllerChangedHandle;
    TSharedPtr<SWidget> Palette;
    int32 SelectedType = 1;
};