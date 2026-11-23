#pragma once

#include "CoreMinimal.h"
#include "Subsystem/CrowdyAccountTypes.h"
#include "UObject/Object.h"
#include "ConstructClaimNameRequest.generated.h"

class AConstructClaimPad;

/** A single name lookup, so an old owner's completion cannot rename a newer claim. */
UCLASS()
class UConstructClaimNameRequest final : public UObject
{
    GENERATED_BODY()

public:
    TWeakObjectPtr<AConstructClaimPad> Pad;
    int64 OwnerId = 0;

    UFUNCTION()
    void Succeeded(FCrowdyPlayerProfile Profile);
    UFUNCTION()
    void Failed(FCrowdyAccountError Error, FString Message);
};
