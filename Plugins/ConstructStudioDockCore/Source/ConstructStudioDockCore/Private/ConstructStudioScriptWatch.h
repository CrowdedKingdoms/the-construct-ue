#pragma once

#include "CoreMinimal.h"
#include "Subsystem/CrowdyModScriptTypes.h"
#include "UObject/Object.h"
#include "ConstructStudioScriptWatch.generated.h"

class UConstructStudioDock;
class UCrowdyModScripts;

/** Hears the world's Mod Scripts start and stop for one dock boot while the dock waits for a deployed CLIENT half to run. */
UCLASS(Transient)
class UConstructStudioScriptWatch : public UObject
{
	GENERATED_BODY()

public:
	/** Binds to Watched's starts and stops; Stop unbinds. */
	void Start(UCrowdyModScripts& Watched);
	void Stop();

	UFUNCTION()
	void Started(const FCrowdyModScript& Script);

	UFUNCTION()
	void Stopped(const FCrowdyModScript& Script, ECrowdyModScriptStop Reason);

	TWeakObjectPtr<UConstructStudioDock> Dock;
	TWeakObjectPtr<UCrowdyModScripts> Scripts;
	int64 Generation = 0;
};
