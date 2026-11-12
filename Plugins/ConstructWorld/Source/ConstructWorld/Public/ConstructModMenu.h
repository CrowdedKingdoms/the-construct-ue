#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConstructHubTypes.h"
#include "Subsystem/CrowdyClaims.h"
#include "ConstructModMenu.generated.h"

class UConstructClaimsSubsystem;
class ULocalPlayer;
class AConstructClaimPad;
class UCrowdyServerObject;
class UConstructModPanel;
class SVerticalBox;
struct FCrowdyServerCallResult;
enum class ECrowdyServerObjectStatus : uint8;

struct FConstructModLand
{
    FInt64Vector Chunk = FInt64Vector::ZeroValue;
    int64 GridId = 0;
    uint64 LookupFloor = 0;
    bool bConfirmed = false;
    bool bPending = true;
    FString Message;
};

/** Known land is only a discovery hint. Each openable row has a fresh platform confirmation. */
UCLASS()
class CONSTRUCTWORLD_API UConstructModMenu : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Construct|Mods")
    UConstructModPanel* OpenForGrid(int64 GridId);

    void Open();
    void Close();
    UConstructModPanel* OpenForPad(AConstructClaimPad& Pad);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual void NativeDestruct() override;

private:
    friend struct FConstructModsLifecycleTestAccess;
    friend class UConstructModsSubsystem;
    friend struct FConstructModsTestAccess;
    void Discover();
    void RebuildCandidates();
    void ConfirmCandidates();
    void BeginHub();
    void HubStatusChanged(UCrowdyServerObject* Object, ECrowdyServerObjectStatus Status);
    void HubReturned(uint64 ForGeneration, uint64 ForAttempt, const FCrowdyServerCallResult& Reply);
    void HubFailed(const FString& Message);
    void ReleaseHub();
    void RebuildRows();
    void ClosePanel();
    void ClaimsChanged(int64 X, int64 Y, int64 Z);
    void ObserveClaims(UConstructClaimsSubsystem* Owner);
    void LookupChanged(FInt64Vector Chunk);
    bool IsCurrent(uint64 ForGeneration) const;
    bool CanSelectGrid(int64 GridId) const;
    bool CanOpenRow(const FConstructModLand& Row) const;
    UFUNCTION()
    void PadOwnerChanged(int64 GridId, int64 OwnerUserId, bool bOwnedByMe);
    UFUNCTION()
    void PadEnded(AActor* Actor, EEndPlayReason::Type Reason);

    UPROPERTY(Transient)
    TObjectPtr<UConstructModPanel> Panel;
    TWeakObjectPtr<AConstructClaimPad> BoundPad;
    TWeakObjectPtr<UWorld> BoundWorld;
    TWeakObjectPtr<ULocalPlayer> InputOwner;
    TWeakObjectPtr<UCrowdyServerObject> Hub;
    FDelegateHandle StatusHandle;
    FDelegateHandle LookupHandle;
    TWeakObjectPtr<UConstructClaimsSubsystem> Confirmations;
    FTimerHandle RetryTimer;
    FTimerHandle HubTimer;
    TSharedPtr<SVerticalBox> LandBox;
    TSharedPtr<SVerticalBox> PanelBox;
    TArray<FInt64Vector> PadChunks;
    TArray<FConstructHubClaim> HubRows;
    TArray<FConstructModLand> Rows;
    TMap<FInt64Vector, uint64> ConfirmRequests;
    TMap<FInt64Vector, FConstructModLand> Confirmed;
    uint64 LookupFloor = 0;
    uint64 HubAttempt = 0;
    uint64 Generation = 0;
    int64 UserId = 0;
    int64 OpenGrid = 0;
    int32 Omitted = 0;
    bool bClosing = false;
    bool bOpen = false;
    bool bHubPending = false;
    bool bHubCalled = false;
    bool bRetried = false;
    FString HubError;
};