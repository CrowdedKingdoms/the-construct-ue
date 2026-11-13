#pragma once

#include "CoreMinimal.h"
#include "Subsystem/CrowdySafety.h"
#include "UObject/Object.h"
#include "ConstructWebcamSettingsReply.generated.h"

/** Receives one communication settings read and hands it to a callback once, whichever way it ended. */
UCLASS(Transient)
class UConstructWebcamSettingsReply : public UObject
{
	GENERATED_BODY()

public:
	using FAnswer = TFunction<void(const FCrowdyCommunicationSettings* Settings, const FCrowdySafetyError* Error)>;

	/** Asks Safety for the player's settings. The caller keeps the reply alive; a dropped reply never answers. */
	static UConstructWebcamSettingsReply* Read(UCrowdySafety& Safety, UObject* Outer, FAnswer Answer);

private:
	UFUNCTION()
	void Succeeded(const FCrowdyCommunicationSettings& Settings);
	UFUNCTION()
	void Failed(const FCrowdySafetyError& Error);
	void Finish(const FCrowdyCommunicationSettings* Settings, const FCrowdySafetyError* Error);

	FAnswer Answer;
};
