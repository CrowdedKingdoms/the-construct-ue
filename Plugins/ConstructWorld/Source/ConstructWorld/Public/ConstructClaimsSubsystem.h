#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystem/CrowdyClaims.h"
#include "ConstructClaimsSubsystem.generated.h"

class AConstructClaimPad;

/** Currency and presentation of a Construct ownership lookup, not a grant of authority. */
struct FConstructClaimLookup
{
    uint64 Request = 0;
    bool bPending = true;
    bool bSucceeded = false;
    TOptional<FCrowdyChunkClaimInfo> Claim;
    FString Message;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FConstructClaimLookupChanged, FInt64Vector);

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

    // Pads and menu confirmations share request ordering, including failures and pending reads.
    uint64 BeginChunkLookup(FInt64Vector Chunk);
    uint64 ConfirmChunk(FInt64Vector Chunk);
    uint64 GetLookupSequence() const { return Serial; }
    bool IsLatestLookup(FInt64Vector Chunk, uint64 Request) const;
    void FinishChunkLookup(FInt64Vector Chunk, uint64 Request, const FCrowdyClaimSearch& Result);
    const FConstructClaimLookup* GetLookup(FInt64Vector Chunk) const;
    FConstructClaimLookupChanged OnLookupChanged;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    friend struct FConstructClaimsTestAccess;
    friend struct FConstructModsTestAccess;

    UFUNCTION()
    void SignedIn(bool bSuccess, FString Message);
    UFUNCTION()
    void SignedOut(bool bSuccess);
    UFUNCTION()
    void AccessEnded(const FString& Reason, const FString& BlockedUntil);

    void StartSearch();
    bool PrepareInitialSearch(bool bSignedIn, FInt64Vector& Low, FInt64Vector& High);
    void InvalidateSession();
    uint64 Search(FInt64Vector Low, FInt64Vector High);
    void SearchReturned(FInt64Vector Low, FInt64Vector High, uint64 Request, const FCrowdyClaimSearch& Result);
    bool GetPadBounds(FInt64Vector& Low, FInt64Vector& High) const;

    void MarkLookup(FInt64Vector Chunk, uint64 Request);
    TMap<FInt64Vector, FConstructClaimLookup> Lookups;
    TMap<FInt64Vector, uint64> LatestSearch;
    uint64 Serial = 0;
    uint64 Session = 0;
    bool bStarted = false;
    bool bLive = true;
    bool bSessionInvalidated = false;
};
