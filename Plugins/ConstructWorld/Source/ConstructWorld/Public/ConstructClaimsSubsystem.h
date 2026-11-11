#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystem/CrowdyClaims.h"
#include "ConstructClaimsSubsystem.generated.h"

class AConstructClaimPad;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FConstructClaimsShown, int64, ChunkX, int64, ChunkY, int64, ChunkZ);

/** Confirmed platform search results for this world's pads, never a source of claim authority. */
UCLASS()
class CONSTRUCTWORLD_API UConstructClaimsSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    UPROPERTY(BlueprintAssignable, Category = "Construct|Claims")
    FConstructClaimsShown OnClaimsShown;

    void Refresh(AConstructClaimPad& Pad);

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    friend struct FConstructClaimsTestAccess;

    UFUNCTION()
    void SignedIn(bool bSuccess, FString Message);
    UFUNCTION()
    void SignedOut(bool bSuccess);
    UFUNCTION()
    void AccessEnded(const FString& Reason, const FString& BlockedUntil);

    void StartSearch();
    bool PrepareInitialSearch(bool bSignedIn, FInt64Vector& Low, FInt64Vector& High);
    void InvalidateSession();
    void Search(FInt64Vector Low, FInt64Vector High);
    void SearchReturned(FInt64Vector Low, FInt64Vector High, uint64 Request, const FCrowdyClaimSearch& Result);
    bool GetPadBounds(FInt64Vector& Low, FInt64Vector& High) const;

    TMap<FInt64Vector, uint64> LatestSearch;
    uint64 Serial = 0;
    uint64 Session = 0;
    bool bStarted = false;
    bool bLive = true;
    bool bSessionInvalidated = false;
};
