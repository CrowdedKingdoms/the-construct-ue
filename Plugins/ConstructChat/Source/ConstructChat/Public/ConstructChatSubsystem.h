#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructChatSubsystem.generated.h"

class APlayerController;
class UConstructChatComponent;
class UGameInstance;
class ULocalPlayer;

/** Installs chat only for this world's local players; never owns platform transport. */
UCLASS()
class CONSTRUCTCHAT_API UConstructChatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	friend class FConstructChatInstallerTest;
	friend class FConstructChatControllerReplacementTest;

	struct FPlayerObservation
	{
		FDelegateHandle ControllerChangedHandle;
		TWeakObjectPtr<UConstructChatComponent> Component;
	};

	void WatchPlayer(ULocalPlayer* Player);
	void ForgetPlayer(ULocalPlayer* Player);
	void ControllerChanged(APlayerController* Controller, TWeakObjectPtr<ULocalPlayer> Player);
	TWeakObjectPtr<UGameInstance> Instance;
	TMap<TWeakObjectPtr<ULocalPlayer>, FPlayerObservation> PlayerObservations;
	FDelegateHandle PlayerAddedHandle;
	FDelegateHandle PlayerRemovedHandle;
	bool bShuttingDown = false;
};
