#pragma once

#include "CoreMinimal.h"
#include "ConstructShellFramework.h"
#include "ConstructPresenceGameMode.generated.h"

/** Reuses the shell's admission and sign-in flow with an SDK presence pawn. */
UCLASS(Blueprintable)
class CONSTRUCTPRESENCE_API AConstructPresenceGameMode : public AConstructShellGameMode
{
	GENERATED_BODY()

public:
	AConstructPresenceGameMode();
};
