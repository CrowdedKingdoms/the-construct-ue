#include "ConstructPresenceOwnerRequest.h"

#include "ConstructPresencePawn.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystem/CrowdyGameSession.h"
#include "TimerManager.h"

void UConstructPresenceOwnerRequest::Initialize(AConstructPresencePawn* InPawn, UCrowdyGameSession* InSession, UCrowdyActorOwners* InSource, uint64 InGeneration)
{
    Pawn = InPawn;
    Session = InSession;
    Source = InSource;
    Instance = InPawn->GetGameInstance();
    World = InPawn->GetWorld();
    EntityId = InPawn->Entity->GetNetID();
    OwnerId = InPawn->Entity->GetWriterID();
    SessionUserId = InSession->GetUserID();
    SessionGameTokenId = InSession->GetSessionGameTokenID();
    Generation = InGeneration;
}

bool UConstructPresenceOwnerRequest::MatchesCurrent(const AConstructPresencePawn* InPawn) const
{
    const UCrowdyGameSession* CurrentSession = Session.Get();
    const UGameInstance* CurrentInstance = Instance.Get();
    return IsValid(InPawn) && Pawn.Get() == InPawn && !InPawn->bOwnerIdentityEnded &&
        !InPawn->IsActorBeingDestroyed() && !InPawn->IsHidden() && IsValid(InPawn->Entity) &&
        InPawn->Entity->GetNetID().IsValid() && InPawn->Entity->GetNetID() == EntityId &&
        InPawn->Entity->GetWriterID() == OwnerId && InPawn->OwnerLookupGeneration == Generation &&
        World.IsValid() && World.Get() == InPawn->GetWorld() && CurrentInstance &&
        CurrentInstance == InPawn->GetGameInstance() && CurrentSession &&
        CurrentSession->GetGameInstance() == CurrentInstance &&
        CurrentInstance->GetSubsystem<UCrowdyGameSession>() == CurrentSession &&
        CurrentSession->GetUserID() == SessionUserId &&
        CurrentSession->GetSessionGameTokenID() == SessionGameTokenId &&
        Source.IsValid() && CurrentInstance->GetSubsystem<UCrowdyActorOwners>() == Source.Get();
}

void UConstructPresenceOwnerRequest::Start()
{
    UCrowdyActorOwners* CurrentSource = Source.Get();
    if (!CurrentSource || !MatchesCurrent(Pawn.Get()))
    {
        return;
    }
    FOnActorOwnerResolved Reply;
    Reply.BindDynamic(this, &ThisClass::HandleResolved);
    CurrentSource->ResolveActorOwner(EntityId, Reply);
}

void UConstructPresenceOwnerRequest::HandleResolved(const FCrowdyActorOwner& Owner)
{
    if (bAnswered)
    {
        return;
    }
    bAnswered = true;
    if (AConstructPresencePawn* CurrentPawn = Pawn.Get())
    {
        CurrentPawn->HandleConfirmedOwner(this, Owner.Result == ECrowdyActorOwnerResult::Confirmed, Owner.UserId);
    }
}

int64 AConstructPresencePawn::GetConfirmedUserId()
{
    UWorld* World = GetWorld();
    UGameInstance* Instance = GetGameInstance();
    UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
    UCrowdyActorOwners* Source = Instance ? Instance->GetSubsystem<UCrowdyActorOwners>() : nullptr;
    if (bOwnerIdentityEnded || IsActorBeingDestroyed() || IsHidden() || !IsValid(Entity) ||
        !Entity->GetNetID().IsValid() || !World || !Session || !Source)
    {
        ResetConfirmedOwner();
        return 0;
    }
    if (OwnerRequest && !OwnerRequest->MatchesCurrent(this))
    {
        ResetConfirmedOwner();
    }
    if (OwnerRequest)
    {
        return ConfirmedOwnerUserId;
    }
    OwnerRequest = NewObject<UConstructPresenceOwnerRequest>(this);
    OwnerRequest->Initialize(this, Session, Source, OwnerLookupGeneration);
    OwnerRequest->Start();
    return 0;
}

void AConstructPresencePawn::HandleConfirmedOwner(UConstructPresenceOwnerRequest* Request, bool bConfirmed, int64 UserId)
{
    if (OwnerRequest != Request)
    {
        return;
    }
    if (!Request->MatchesCurrent(this))
    {
        ResetConfirmedOwner();
        return;
    }
    if (bConfirmed && UserId > 0)
    {
        ConfirmedOwnerUserId = UserId;
        return;
    }
    // Keep the answered receiver until the timer fires, so UI polling cannot bypass the rate limit.
    ConfirmedOwnerUserId = 0;
    GetWorld()->GetTimerManager().SetTimer(OwnerRetryTimer, this, &ThisClass::RetryConfirmedOwner, 10.0f, false);
}

void AConstructPresencePawn::RetryConfirmedOwner()
{
    ResetConfirmedOwner();
    GetConfirmedUserId();
}

void AConstructPresencePawn::ResetConfirmedOwner()
{
    ++OwnerLookupGeneration;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(OwnerRetryTimer);
    }
    OwnerRetryTimer.Invalidate();
    OwnerRequest = nullptr;
    ConfirmedOwnerUserId = 0;
}
