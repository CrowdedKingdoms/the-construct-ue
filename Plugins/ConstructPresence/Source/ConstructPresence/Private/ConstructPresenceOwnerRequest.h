#pragma once

#include "CoreMinimal.h"
#include "Subsystem/CrowdyActorOwners.h"
#include "ConstructPresenceOwnerRequest.generated.h"

class AConstructPresencePawn;
class UCrowdyGameSession;
class UGameInstance;
class UWorld;

/** One retained delegate receiver, scoped to the exact pawn and session that asked. */
UCLASS()
class UConstructPresenceOwnerRequest : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(AConstructPresencePawn* InPawn, UCrowdyGameSession* InSession, UCrowdyActorOwners* InSource, uint64 InGeneration);
    bool MatchesCurrent(const AConstructPresencePawn* InPawn) const;
    void Start();

private:
    UFUNCTION()
    void HandleResolved(const FCrowdyActorOwner& Owner);

    TWeakObjectPtr<AConstructPresencePawn> Pawn;
    TWeakObjectPtr<UCrowdyGameSession> Session;
    TWeakObjectPtr<UGameInstance> Instance;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UCrowdyActorOwners> Source;
    FGuid EntityId;
    FGuid OwnerId;
    uint64 Generation = 0;
    int64 SessionUserId = 0;
    int64 SessionGameTokenId = 0;
    bool bAnswered = false;
};
