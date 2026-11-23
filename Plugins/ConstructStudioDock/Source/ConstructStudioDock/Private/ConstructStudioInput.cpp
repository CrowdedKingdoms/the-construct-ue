#include "ConstructStudioInput.h"

#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

void UConstructStudioInput::Attach(APlayerController* Controller)
{
    Detach();
    if (!Controller || !Controller->IsLocalController())
    {
        return;
    }
    AttachedController = Controller;
    Priority = 65;
    bBlockInput = false;
    FInputKeyBinding& Binding = BindKey(EKeys::M, IE_Pressed, this, &ThisClass::Toggle);
    Binding.bConsumeInput = false;
    Controller->PushInputComponent(this);
}

void UConstructStudioInput::Detach()
{
    if (APlayerController* Controller = AttachedController.Get())
    {
        Controller->PopInputComponent(this);
    }
    AttachedController.Reset();
    KeyBindings.Reset();
}

void UConstructStudioInput::Toggle()
{
    OnToggle.ExecuteIfBound();
}

