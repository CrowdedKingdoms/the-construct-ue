#pragma once

#include "ConstructHubTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructProgramCatalogSubsystem.generated.h"

class AConstructProgramPad;
class UCrowdyServerObject;
class UGameInstance;
enum class ECrowdyServerObjectStatus : uint8;
struct FCrowdyServerCallResult;

UCLASS()
class CONSTRUCTWORLD_API UConstructProgramCatalogSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    static const TSoftObjectPtr<UWorld>& GetHolodeck();
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
    void ApplyToPad(AConstructProgramPad& Pad) const;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    UFUNCTION()
    void SignedIn(bool bSuccess, FString Message);

    UFUNCTION()
    void SignedOut(bool bSuccess);

    UFUNCTION()
    void AccessEnded(const FString& Reason, const FString& BlockedUntil);

    void StartCatalog();
    void StatusChanged(UCrowdyServerObject* Object, ECrowdyServerObjectStatus Status);
    void CatalogReturned(const FCrowdyServerCallResult& Result);
    void FinishCatalog(TOptional<TArray<FConstructHubProgram>> NewCatalog);
    void InvalidateSession();
    void RefreshPads() const;
    void ReleaseObject();

    TWeakObjectPtr<UGameInstance> Game;
    TWeakObjectPtr<UCrowdyServerObject> CatalogObject;
    FDelegateHandle StatusHandle;
    TOptional<TArray<FConstructHubProgram>> Catalog;
    bool bSessionActive = false;
    bool bFinished = false;
    bool bCalled = false;
};
