#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDockTypes.h"
#include "UObject/Object.h"
#include "ConstructStudioCaptureRequest.generated.h"

class UConstructStudioDock;

/** One CaptureView call: the target of its OnDone, which takes only the first answer and only for its own boot. */
UCLASS(Transient)
class UConstructStudioCaptureRequest : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void Done(const FConstructStudioCapture& Capture);

	TWeakObjectPtr<UConstructStudioDock> Dock;
	int64 Generation = 0;

	/** The agent's studio.screenshot request this answers; empty for the player's share and a run's screenshot. */
	FString RequestId;
	bool bShared = false;

	/** The screenshot a draft test or deploy answer carries once its CLIENT half runs. */
	bool bForRun = false;
	bool bAnswered = false;
};
