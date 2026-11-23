#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "ConstructStudioDockTypes.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Object.h"
#include "ConstructStudioCaptureTestListener.generated.h"

UCLASS(Transient, Hidden, HideDropdown, NotBlueprintable, NotBlueprintType)
class UConstructStudioCaptureTestListener final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY()
    TObjectPtr<UMaterialInterface> UnlitNameMaterial;

    UPROPERTY()
    FConstructStudioCapture Capture;

    int32 Answers = 0;
    bool bOnGameThread = false;

    UFUNCTION()
    void Captured(const FConstructStudioCapture& InCapture)
    {
        Capture = InCapture;
        ++Answers;
        bOnGameThread = IsInGameThread();
    }
};

UCLASS(Transient, Hidden, HideDropdown, NotBlueprintable, NotBlueprintType)
class UConstructStudioCaptureTestWidgetComponent final : public UWidgetComponent
{
    GENERATED_BODY()

public:
    void DrawOnce()
    {
        UpdateWidget();
        DrawWidgetToRenderTarget(1.0f / 60.0f);
    }
};
