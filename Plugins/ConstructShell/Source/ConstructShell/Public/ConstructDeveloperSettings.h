#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ConstructDeveloperSettings.generated.h"

/** Project choices for optional Construct runtime features. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Crowdy Construct"))
class CONSTRUCTSHELL_API UConstructDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UConstructDeveloperSettings();

	/** Makes Construct voice available in newly started runtime worlds. Microphone and listening remain player choices. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Voice", meta = (DisplayName = "Enable Voice"))
	bool bEnableVoice = false;

	/** Makes Construct webcams available in newly started runtime worlds. The camera and showing others' video remain player choices. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Webcam", meta = (DisplayName = "Enable Webcam"))
	bool bEnableWebcam = false;
};
