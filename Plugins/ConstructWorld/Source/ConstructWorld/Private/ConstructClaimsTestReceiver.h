#pragma once

#include "CoreMinimal.h"
#include "Subsystem/CrowdyClaims.h"
#include "UObject/Object.h"
#include "ConstructClaimsTestReceiver.generated.h"

class AConstructClaimPad;

UCLASS(Transient, NotBlueprintable)
class UConstructClaimsTestReceiver : public UObject
{
    GENERATED_BODY()

public:
    int32 ClaimCount = 0;
    int32 ReleaseCount = 0;
    bool bListed = false;
    bool bUnlisted = false;
    ECrowdyClaimResult Result = ECrowdyClaimResult::Failed;
    int64 Grid = 0;
    FString Reason;
    TFunction<void()> ClaimHandler;

    UFUNCTION()
    void Claimed(ECrowdyClaimResult InResult, int64 GridId, bool bInListed, const FString& Message)
    {
        ++ClaimCount;
        Result = InResult;
        Grid = GridId;
        bListed = bInListed;
        Reason = Message;
        if (ClaimHandler)
        {
            ClaimHandler();
        }
    }

    UFUNCTION()
    void Released(ECrowdyClaimResult InResult, int64 GridId, bool bInUnlisted, const FString& Message)
    {
        ++ReleaseCount;
        Result = InResult;
        Grid = GridId;
        bUnlisted = bInUnlisted;
        Reason = Message;
    }
};
