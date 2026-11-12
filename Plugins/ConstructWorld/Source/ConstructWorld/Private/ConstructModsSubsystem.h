#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructModsSubsystem.generated.h"

class ULocalPlayer;
class UConstructModMenu;
class UConstructModPanel;
class AConstructClaimPad;

USTRUCT()
struct FConstructModsLocalUI
{
    GENERATED_BODY()
    UPROPERTY(Transient)
    TObjectPtr<UConstructModMenu> Menu;
    TWeakObjectPtr<APlayerController> Controller;
    FDelegateHandle OpenHandle;
    FDelegateHandle ControllerHandle;
};

/** World owns local menus and tears them down before session or controller replacement. */
UCLASS()
class UConstructModsSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
    UConstructModPanel* OpenForPad(AConstructClaimPad& Pad);
private:
    friend struct FConstructModsLifecycleTestAccess;
    friend struct FConstructModsTestAccess;
    void AddPlayer(ULocalPlayer* Player);
    void RemovePlayer(ULocalPlayer* Player);
    void ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player);
    void OpenRequested(TWeakObjectPtr<ULocalPlayer> Player);
    UConstructModMenu* GetMenu(ULocalPlayer* Player);
    void CloseAll();
    UFUNCTION()
    void ControllerEnded(AActor* Actor, EEndPlayReason::Type Reason);
    UFUNCTION()
    void SignedIn(bool bSuccess, FString Message);
    UFUNCTION()
    void SignedOut(bool bSuccess);
    UFUNCTION()
    void AccessEnded(const FString& Code, const FString& Message);
    UPROPERTY(Transient)
    TMap<TObjectPtr<ULocalPlayer>, FConstructModsLocalUI> Players;
    FDelegateHandle AddedHandle;
    FDelegateHandle RemovedHandle;
};