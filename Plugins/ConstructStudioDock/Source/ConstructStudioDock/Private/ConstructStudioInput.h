#pragma once

#include "CoreMinimal.h"
#include "Components/InputComponent.h"
#include "ConstructStudioInput.generated.h"

class APlayerController;

UCLASS()
class UConstructStudioInput : public UInputComponent
{
    GENERATED_BODY()

public:
    void Attach(APlayerController* Controller);
    void Detach();
    FSimpleDelegate OnToggle;

private:
    void Toggle();
    TWeakObjectPtr<APlayerController> AttachedController;
};

