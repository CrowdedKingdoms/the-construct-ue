#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConstructStudioDockBridge.generated.h"

class UConstructStudioDock;

/**
 * The dock page's only way into the game, bound for each load of the page. Any script on the dock's origin can call it,
 * so it answers only while its dock holds it and the main frame is on the dock's origin, and trusts nothing it is sent.
 */
UCLASS(Transient, NotBlueprintable, NotBlueprintType)
class UConstructStudioDockBridge final : public UObject
{
	GENERATED_BODY()

public:
	/** The boot material for the harness as JSON, without the token; an error object once unbound or off the origin. */
	UFUNCTION()
	FString Boot();

	/** One frame from the page as JSON; answers {"ok":true}, or an error object once unbound or off the origin. */
	UFUNCTION()
	FString Send(const FString& Frame);

	TWeakObjectPtr<UConstructStudioDock> Dock;
};
