#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConstructVoiceActivationTestReceiver.generated.h"

class APlayerController;
class UActorComponent;
class ULocalPlayer;

/** Private receiver for real component-activation lifecycle regressions. */
UCLASS(Transient, NotBlueprintable)
class UConstructVoiceActivationTestReceiver : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> Replacement;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayer> Player;

	bool bInvoked = false;

	UFUNCTION()
	void ReplaceController(UActorComponent* Component, bool bReset);
};
