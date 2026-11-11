#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystem/CrowdyClaims.h"
#include "ConstructClaimPad.generated.h"

class UBoxComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UInputComponent;
class UConstructClaimNameRequest;
class UCrowdyServerObject;
class SWidget;
class UGameViewportClient;
struct FCrowdyServerCallResult;
enum class ECrowdyServerObjectStatus : uint8;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FConstructClaimDone, ECrowdyClaimResult, Result, int64, GridId, bool, bListed, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FConstructReleaseDone, ECrowdyClaimResult, Result, int64, GridId, bool, bUnlisted, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FConstructOwnerShown, int64, GridId, int64, OwnerUserId, bool, bOwnedByMe);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FConstructOwnerNamed, int64, OwnerUserId, const FString&, Name);

/** Offers actions only from a confirmed platform search. The platform and hub own their respective state. */
UCLASS()
class CONSTRUCTWORLD_API AConstructClaimPad : public AActor
{
    GENERATED_BODY()

public:
    AConstructClaimPad();

    UFUNCTION(BlueprintCallable, Category = "Construct|Claims")
    void Claim();
    UFUNCTION(BlueprintCallable, Category = "Construct|Claims")
    void Release();
    UFUNCTION(BlueprintCallable, Category = "Construct|Claims")
    void Refresh();
    UFUNCTION(BlueprintPure, Category = "Construct|Claims")
    bool GetShownOwner(int64& GridId, int64& OwnerUserId, bool& bOwnedByMe, FString& OwnerName) const;

    UPROPERTY(BlueprintAssignable, Category = "Construct|Claims")
    FConstructClaimDone OnClaimDone;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Claims")
    FConstructReleaseDone OnReleaseDone;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Claims")
    FConstructOwnerShown OnOwnerShown;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Claims")
    FConstructOwnerNamed OnOwnerNamed;

    FInt64Vector GetClaimChunk() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    friend class UConstructClaimsSubsystem;
    friend class UConstructClaimNameRequest;
    friend struct FConstructClaimsTestAccess;

    void ShowSearch(const FCrowdyChunkClaimInfo* Claim);
    void ResetSession();
    bool CanClaim() const;
    bool CanRelease() const;
    void UpdatePresentation();
    void NameReturned(UConstructClaimNameRequest* Request, const FString& Name);
    void ShowFeedback(const FString& Message);
    void HideFeedback();
    void PlatformClaimReturned(const FCrowdyChunkClaim& Reply);
    void PlatformReleaseReturned(const FCrowdyClaimRelease& Reply);
    void BeginHub();
    void HubStatusChanged(UCrowdyServerObject* Object, ECrowdyServerObjectStatus Status);
    void CallHub();
    void HubReturned(const FCrowdyServerCallResult& Reply);
    void HubTimedOut();
    void FinishAction(bool bHubSucceeded, const FString& Message, bool bRefresh = true);
    void ReleaseHub();
    void AcceptOffer();
    void ReleaseInput();

    UFUNCTION()
    void EnterPad(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 Body, bool bSweep, const FHitResult& Hit);
    UFUNCTION()
    void LeavePad(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 Body);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> Trigger;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Platform;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UTextRenderComponent> Sign;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Edges;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> MineMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> OtherMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UConstructClaimNameRequest> NameRequest;
    UPROPERTY(Transient)
    TObjectPtr<UInputComponent> OfferInput;

    TWeakObjectPtr<APlayerController> OfferController;
    TWeakObjectPtr<UCrowdyServerObject> Hub;
    FDelegateHandle HubStatusHandle;
    FTimerHandle RetryTimer;
    FTimerHandle HubTimer;
    FTimerHandle FeedbackTimer;
    TWeakObjectPtr<UGameViewportClient> FeedbackViewport;
    TSharedPtr<SWidget> FeedbackWidget;

    bool bKnown = false;
    int64 ShownGrid = 0;
    int64 ShownOwner = 0;
    bool bMine = false;
    FString ShownName;
    bool bBusy = false;
    bool bReleasing = false;
    bool bHubCalled = false;
    bool bEnding = false;
    bool bWaitingForPlatform = false;
    bool bActionSessionEnded = false;
    uint64 PlatformSerial = 0;
    int32 Retries = 0;
    uint64 ActionSerial = 0;
    FCrowdyChunkClaim ClaimReply;
    FCrowdyClaimRelease ReleaseReply;
};
