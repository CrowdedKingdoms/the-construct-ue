#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructVoiceSubsystem.generated.h"

class APlayerController;
class UGameInstance;
class ULocalPlayer;
struct FConstructVoiceInstallationTestAccess;

/** Installs optional Construct voice on local controllers, using one immutable choice per runtime world. */
UCLASS()
class CONSTRUCTVOICE_API UConstructVoiceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Construct|Voice")
	bool IsVoiceEnabled() const
	{
		return bEnabled;
	}

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend struct FConstructVoiceInstallationTestAccess;

	void LocalPlayerAdded(ULocalPlayer* Player);
	void LocalPlayerRemoved(ULocalPlayer* Player);
	void PlayerControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player);
	void InstallForController(APlayerController* Controller, ULocalPlayer* Player);

	TWeakObjectPtr<UGameInstance> ObservedInstance;
	TMap<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<class UConstructVoiceComponent>> ActiveComponents;
	TMap<TWeakObjectPtr<ULocalPlayer>, FDelegateHandle> ControllerChangedHandles;
	FDelegateHandle LocalPlayerAddedHandle;
	FDelegateHandle LocalPlayerRemovedHandle;
	bool bEnabled = false;
};
