#pragma once

#include "CoreMinimal.h"
#include "Subsystem/CrowdySafety.h"
#include "ConstructWebcamSafetyRequest.generated.h"

class UConstructWebcamPanel;

enum class EConstructWebcamSafetyAction : uint8
{
    ReadSettings,
    ChangeScope,
    Consent,
    Block,
    Report
};

/** One retained UI request. The SDK owns transport and completion. */
UCLASS()
class UConstructWebcamSafetyRequest : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UConstructWebcamPanel> Panel;
    uint64 Generation = 0;
    EConstructWebcamSafetyAction Action = EConstructWebcamSafetyAction::ReadSettings;
    UFUNCTION()
    void SettingsReceived(const FCrowdyCommunicationSettings& Settings);
    UFUNCTION()
    void Done();
    UFUNCTION()
    void ConsentReceived(ECrowdyConsentStatus Status);
    UFUNCTION()
    void Failed(const FCrowdySafetyError& Error);
};
