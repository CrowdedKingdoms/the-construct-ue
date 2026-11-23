#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDockTypes.h"
#include "UObject/Interface.h"
#include "ConstructStudioDockHost.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class CONSTRUCTSTUDIODOCKCORE_API UConstructStudioDockHost : public UInterface
{
	GENERATED_BODY()
};

/** What the game answers for the Studio dock, set on the dock with SetHost before Open. */
class CONSTRUCTSTUDIODOCKCORE_API IConstructStudioDockHost
{
	GENERATED_BODY()

public:
	/**
	 * Captures the game view for the agent. Call OnDone later, exactly once, on the game thread, with the capture or a
	 * refusal; a Blueprint host calls it through FinishCapture. Once the dock closes, an answer is dropped.
	 */
	UFUNCTION(BlueprintNativeEvent, Category="Construct|Studio Dock")
	void CaptureView(const FString& Label, const FOnConstructStudioCaptured& OnDone);
};
