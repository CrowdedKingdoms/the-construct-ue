#include "ConstructClaimsSubsystem.h"

#include "ConstructClaimPad.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "TimerManager.h"

namespace
{
    bool Inside(FInt64Vector Chunk, FInt64Vector Low, FInt64Vector High)
    {
        return Chunk.X >= Low.X && Chunk.Y >= Low.Y && Chunk.Z >= Low.Z
            && Chunk.X <= High.X && Chunk.Y <= High.Y && Chunk.Z <= High.Z;
    }
}

bool UConstructClaimsSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UConstructClaimsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    UGameInstance* Game = InWorld.GetGameInstance();
    if (!Game || InWorld.GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    if (UCrowdySDKSubsystem* SDK = Game->GetSubsystem<UCrowdySDKSubsystem>())
    {
        SDK->OnLogin.AddUniqueDynamic(this, &ThisClass::SignedIn);
        SDK->OnRegister.AddUniqueDynamic(this, &ThisClass::SignedIn);
        SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::SignedOut);
    }
    if (UCrowdyGameSession* SessionSubsystem = Game->GetSubsystem<UCrowdyGameSession>())
    {
        SessionSubsystem->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
    }
    // Collect the level's pads after their BeginPlay, rather than searching separately for each pad.
    InWorld.GetTimerManager().SetTimerForNextTick(this, &ThisClass::StartSearch);
}

void UConstructClaimsSubsystem::Deinitialize()
{
    bLive = false;
    InvalidateSession();
    UGameInstance* Game = GetWorld()->GetGameInstance();
    if (Game)
    {
        if (UCrowdySDKSubsystem* SDK = Game->GetSubsystem<UCrowdySDKSubsystem>())
        {
            SDK->OnLogin.RemoveDynamic(this, &ThisClass::SignedIn);
            SDK->OnRegister.RemoveDynamic(this, &ThisClass::SignedIn);
            SDK->OnLogout.RemoveDynamic(this, &ThisClass::SignedOut);
        }
        if (UCrowdyGameSession* SessionSubsystem = Game->GetSubsystem<UCrowdyGameSession>())
        {
            SessionSubsystem->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
        }
    }
    Super::Deinitialize();
}

void UConstructClaimsSubsystem::SignedIn(bool bSuccess, FString)
{
    if (bSuccess)
    {
        bSessionInvalidated = false;
        // A restored session can start the first search before its app token exists; search again now it does.
        bStarted = false;
        StartSearch();
    }
}

void UConstructClaimsSubsystem::SignedOut(bool bSuccess)
{
    if (bSuccess)
    {
        InvalidateSession();
    }
}

void UConstructClaimsSubsystem::AccessEnded(const FString&, const FString&)
{
    InvalidateSession();
}

void UConstructClaimsSubsystem::InvalidateSession()
{
    ++Session;
    bSessionInvalidated = true;
    bStarted = false;
    LatestSearch.Reset();
    Lookups.Reset();
    for (TActorIterator<AConstructClaimPad> It(GetWorld()); It; ++It)
    {
        It->ResetSession();
    }
}

bool UConstructClaimsSubsystem::GetPadBounds(FInt64Vector& Low, FInt64Vector& High) const
{
    bool bFound = false;
    for (TActorIterator<AConstructClaimPad> It(GetWorld()); It; ++It)
    {
        const FInt64Vector Chunk = It->GetClaimChunk();
        if (!bFound)
        {
            Low = High = Chunk;
            bFound = true;
        }
        else
        {
            Low.X = FMath::Min(Low.X, Chunk.X);
            Low.Y = FMath::Min(Low.Y, Chunk.Y);
            Low.Z = FMath::Min(Low.Z, Chunk.Z);
            High.X = FMath::Max(High.X, Chunk.X);
            High.Y = FMath::Max(High.Y, Chunk.Y);
            High.Z = FMath::Max(High.Z, Chunk.Z);
        }
    }
    return bFound;
}

void UConstructClaimsSubsystem::StartSearch()
{
    const UGameInstance* Game = GetWorld()->GetGameInstance();
    const UCrowdyGameSession* Auth = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
    FInt64Vector Low, High;
    if (PrepareInitialSearch(Auth && !Auth->HasAccessEnded() && Auth->GetUserID() > 0, Low, High))
    {
        Search(Low, High);
    }
}

bool UConstructClaimsSubsystem::PrepareInitialSearch(bool bSignedIn, FInt64Vector& Low, FInt64Vector& High)
{
    if (!bLive || bSessionInvalidated || bStarted || !bSignedIn || !GetPadBounds(Low, High))
    {
        return false;
    }
    bStarted = true;
    return true;
}

void UConstructClaimsSubsystem::Refresh(AConstructClaimPad& Pad)
{
    if (!bLive || !bStarted)
    {
        StartSearch();
        return;
    }
    const FInt64Vector Chunk = Pad.GetClaimChunk();
    Search(Chunk, Chunk);
}

uint64 UConstructClaimsSubsystem::BeginChunkLookup(FInt64Vector Chunk)
{
    if (!bLive || bSessionInvalidated)
    {
        return 0;
    }
    const uint64 Request = ++Serial;
    MarkLookup(Chunk, Request);
    return Request;
}

void UConstructClaimsSubsystem::MarkLookup(FInt64Vector Chunk, uint64 Request)
{
    LatestSearch.Add(Chunk, Request);
    FConstructClaimLookup Lookup;
    Lookup.Request = Request;
    Lookups.Add(Chunk, MoveTemp(Lookup));
    for (TActorIterator<AConstructClaimPad> It(GetWorld()); It; ++It)
    {
        if (It->GetClaimChunk() == Chunk)
        {
            It->bLatestSearchSucceeded = false;
        }
    }
    OnLookupChanged.Broadcast(Chunk);
}

bool UConstructClaimsSubsystem::IsLatestLookup(FInt64Vector Chunk, uint64 Request) const
{
    return bLive && !bSessionInvalidated && Request != 0 && LatestSearch.FindRef(Chunk) == Request;
}

const FConstructClaimLookup* UConstructClaimsSubsystem::GetLookup(FInt64Vector Chunk) const
{
    return Lookups.Find(Chunk);
}

void UConstructClaimsSubsystem::FinishChunkLookup(FInt64Vector Chunk, uint64 Request, const FCrowdyClaimSearch& Result)
{
    SearchReturned(Chunk, Chunk, Request, Result);
}

uint64 UConstructClaimsSubsystem::Search(FInt64Vector Low, FInt64Vector High)
{
    UGameInstance* Game = GetWorld()->GetGameInstance();
    UCrowdyClaims* Claims = Game ? Game->GetSubsystem<UCrowdyClaims>() : nullptr;
    if (!Claims || !bLive || bSessionInvalidated)
    {
        return 0;
    }
    const uint64 Request = ++Serial;
    const uint64 ForSession = Session;
    TArray<FInt64Vector> Chunks;
    if (Low == High)
    {
        Chunks.Add(Low);
    }
    for (const auto& Pair : LatestSearch)
    {
        if (Inside(Pair.Key, Low, High))
        {
            Chunks.AddUnique(Pair.Key);
        }
    }
    for (TActorIterator<AConstructClaimPad> It(GetWorld()); It; ++It)
    {
        const FInt64Vector Chunk = It->GetClaimChunk();
        if (Inside(Chunk, Low, High))
        {
            Chunks.AddUnique(Chunk);
        }
    }
    for (const FInt64Vector Chunk : Chunks)
    {
        MarkLookup(Chunk, Request);
    }
    const TWeakObjectPtr<UConstructClaimsSubsystem> WeakThis(this);
    Claims->FindClaims(Low.X, Low.Y, Low.Z, High.X, High.Y, High.Z, [WeakThis, Low, High, Request, ForSession](const FCrowdyClaimSearch& Reply)
    {
        if (UConstructClaimsSubsystem* Self = WeakThis.Get(); Self && Self->bLive && Self->Session == ForSession)
        {
            Self->SearchReturned(Low, High, Request, Reply);
        }
    });
    return Request;
}

void UConstructClaimsSubsystem::SearchReturned(FInt64Vector Low, FInt64Vector High, uint64 Request, const FCrowdyClaimSearch& Result)
{
    TArray<FInt64Vector> Current;
    for (const auto& Pair : LatestSearch)
    {
        if (Inside(Pair.Key, Low, High) && IsLatestLookup(Pair.Key, Request))
        {
            Current.Add(Pair.Key);
        }
    }
    bool bShowedFailure = false;
    for (const FInt64Vector Chunk : Current)
    {
        if (!IsLatestLookup(Chunk, Request))
        {
            continue;
        }
        const FCrowdyChunkClaimInfo* Found = Result.Claims.FindByPredicate([Chunk](const FCrowdyChunkClaimInfo& Claim)
        {
            return FInt64Vector(Claim.ChunkX, Claim.ChunkY, Claim.ChunkZ) == Chunk;
        });
        FConstructClaimLookup Lookup;
        Lookup.Request = Request;
        Lookup.bPending = false;
        Lookup.bSucceeded = Result.Result == ECrowdyClaimResult::Success;
        Lookup.Message = Result.Message;
        if (Lookup.bSucceeded && Found)
        {
            Lookup.Claim = *Found;
        }
        Lookups.Add(Chunk, MoveTemp(Lookup));
        for (TActorIterator<AConstructClaimPad> It(GetWorld()); It; ++It)
        {
            if (It->GetClaimChunk() != Chunk || !IsLatestLookup(Chunk, Request))
            {
                continue;
            }
            if (Result.Result == ECrowdyClaimResult::Success)
            {
                It->ShowSearch(Found);
            }
            else
            {
                It->bLatestSearchSucceeded = false;
                if (!bShowedFailure)
                {
                    It->ShowFeedback(Result.Message.IsEmpty() ? TEXT("Could not check this chunk.") : Result.Message);
                    bShowedFailure = true;
                }
            }
        }
        if (IsLatestLookup(Chunk, Request))
        {
            OnLookupChanged.Broadcast(Chunk);
            if (Result.Result == ECrowdyClaimResult::Success && IsLatestLookup(Chunk, Request))
            {
                OnClaimsShown.Broadcast(Chunk.X, Chunk.Y, Chunk.Z);
            }
        }
    }
}
uint64 UConstructClaimsSubsystem::ConfirmChunk(FInt64Vector Chunk)
{
    // A menu requests the lookup; the world owner finishes it even if that menu closes.
    return Search(Chunk, Chunk);
}