#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "ConstructHubSubsystem.generated.h"

class APlayerController;
class UCrowdyServerObject;
class UCrowdyServerObjectDefinition;
class UConstructHubStatusWidget;
class UGameInstance;
class ULocalPlayer;
enum class ECrowdyServerObjectStatus : uint8;

USTRUCT()
struct FConstructHubLocalPlayer
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UConstructHubStatusWidget> Widget;

    TWeakObjectPtr<APlayerController> Controller;
    FDelegateHandle ControllerChangedHandle;
};

/** Installs the status line in each local viewport and follows the signed-in user's Hub objects. */
UCLASS()
class CONSTRUCTHUB_API UConstructHubSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FConstructHubTestAccess;
#endif

    struct FWatchedObject
    {
        TWeakObjectPtr<UCrowdyServerObject> Object;
        FDelegateHandle StatusHandle;
        FDelegateHandle ValuesHandle;
    };

    void WatchPlayer(ULocalPlayer* Player);
    void ForgetPlayer(ULocalPlayer* Player);
    void ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player);
    void RefreshServerObjects();
    void FollowObject(FWatchedObject& Watch, UCrowdyServerObjectDefinition* Definition, const FString& InstanceId);
    void ReleaseServerObjects();
    void ReleaseObject(FWatchedObject& Watch);
    void RefreshWidgets();
    void ObjectStatusChanged(UCrowdyServerObject* Object, ECrowdyServerObjectStatus Status);
    void ObjectValuesChanged(UCrowdyServerObject* Object, TConstArrayView<FString> Fields);

    UFUNCTION()
    void SignedIn(bool bSuccess, FString Message);

    UFUNCTION()
    void SignedOut(bool bSuccess);

    UFUNCTION()
    void AccessEnded(const FString& Reason, const FString& BlockedUntil);

    UPROPERTY()
    TObjectPtr<UCrowdyServerObjectDefinition> WorldDefinition;

    UPROPERTY()
    TObjectPtr<UCrowdyServerObjectDefinition> ProgressDefinition;

    UPROPERTY(Transient)
    TMap<TObjectPtr<ULocalPlayer>, FConstructHubLocalPlayer> Players;

    TWeakObjectPtr<UGameInstance> Instance;
    FDelegateHandle PlayerAddedHandle;
    FDelegateHandle PlayerRemovedHandle;
    FWatchedObject WorldWatch;
    FWatchedObject ProgressWatch;
    int64 BoundUserId = 0;
    bool bShuttingDown = false;
};
