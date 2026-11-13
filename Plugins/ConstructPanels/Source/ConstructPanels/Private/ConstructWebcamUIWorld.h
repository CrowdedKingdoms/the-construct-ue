#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructWebcamUIWorld.generated.h"

class UConstructWebcamPanel;
class ULocalPlayer;

USTRUCT()
struct FConstructWebcamPlayerUI
{
    GENERATED_BODY()
    UPROPERTY(Transient)
    TWeakObjectPtr<ULocalPlayer> Player;
    UPROPERTY(Transient)
    TWeakObjectPtr<APlayerController> Controller;
    UPROPERTY(Transient)
    TObjectPtr<UConstructWebcamPanel> Panel;
    FDelegateHandle ControllerHandle;
};

/** World-owned presentation; persistent camera and feed services remain separate. */
UCLASS()
class UConstructWebcamUIWorld : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
private:
    void AddPlayer(ULocalPlayer* Player);
    void RemovePlayer(ULocalPlayer* Player);
    void ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player);
    void RefreshPlayers();
    void CloseAll();
    UFUNCTION()
    void ControllerEnded(AActor* Actor, EEndPlayReason::Type Reason);
    UFUNCTION()
    void SignedOut(bool bSuccess);
    UFUNCTION()
    void SignedIn(bool bSuccess, FString Message);
    UFUNCTION()
    void AccessEnded(const FString& Reason, const FString& Until);
    UPROPERTY(Transient)
    TArray<FConstructWebcamPlayerUI> Players;
    FTimerHandle RefreshTimer;
    FDelegateHandle AddedHandle;
    FDelegateHandle RemovedHandle;
};
