#include "ConstructPresenceGameMode.h"

#include "ConstructPresencePawn.h"

AConstructPresenceGameMode::AConstructPresenceGameMode()
{
	DefaultPawnClass = AConstructPresencePawn::StaticClass();
}
