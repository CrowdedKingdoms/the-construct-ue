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
	bool bShared = false;
	bool bAnswered = false;
};
