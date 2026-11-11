#include "ConstructClaimPad.h"
#include "ConstructClaimsSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "ConstructClaimNameRequest.h"
#include "ConstructClaimsTestReceiver.h"
#include "CrowdyServerObjectTypes.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include "StructUtils/PropertyBag.h"

struct FConstructClaimsTestAccess
{
    static void Show(AConstructClaimPad& Pad, const FCrowdyChunkClaimInfo* Claim)
    {
        Pad.ShowSearch(Claim);
    }
    static bool CanClaim(const AConstructClaimPad& Pad)
    {
        return Pad.CanClaim();
    }
    static bool CanRelease(const AConstructClaimPad& Pad)
    {
        return Pad.CanRelease();
    }
    static bool Initial(UConstructClaimsSubsystem& Subsystem, bool bSignedIn, FInt64Vector& Low, FInt64Vector& High)
    {
        return Subsystem.PrepareInitialSearch(bSignedIn, Low, High);
    }
    static bool Bounds(const UConstructClaimsSubsystem& Subsystem, FInt64Vector& Low, FInt64Vector& High)
    {
        return Subsystem.GetPadBounds(Low, High);
    }
    static void Answer(UConstructClaimsSubsystem& Subsystem, FInt64Vector Low, FInt64Vector High, uint64 Serial, const FCrowdyClaimSearch& Reply)
    {
        Subsystem.SearchReturned(Low, High, Serial, Reply);
    }
    static void Issued(UConstructClaimsSubsystem& Subsystem, FInt64Vector Chunk, uint64 Serial)
    {
        Subsystem.LatestSearch.Add(Chunk, Serial);
    }
    static const TArray<TObjectPtr<UStaticMeshComponent>>& Edges(const AConstructClaimPad& Pad)
    {
        return Pad.Edges;
    }
    static void BeginClaim(AConstructClaimPad& Pad)
    {
        Pad.bBusy = true;
        Pad.bReleasing = false;
        Pad.ClaimReply.Result = ECrowdyClaimResult::Success;
        Pad.ClaimReply.GridId = 900000000000000001LL;
    }
    static void BeginRelease(AConstructClaimPad& Pad)
    {
        Pad.bBusy = true;
        Pad.bReleasing = true;
        Pad.ReleaseReply.Result = ECrowdyClaimResult::Success;
        Pad.ReleaseReply.GridId = 900000000000000001LL;
    }
    static void Hub(AConstructClaimPad& Pad, const FCrowdyServerCallResult& Reply)
    {
        Pad.HubReturned(Reply);
    }
    static void WaitForPlatform(AConstructClaimPad& Pad)
    {
        Pad.bBusy = true;
        Pad.bWaitingForPlatform = true;
    }
    static void Platform(AConstructClaimPad& Pad, const FCrowdyChunkClaim& Reply)
    {
        Pad.PlatformClaimReturned(Reply);
    }
    static void Reset(AConstructClaimPad& Pad)
    {
        Pad.ResetSession();
    }
    static float RetryDelay(AConstructClaimPad& Pad)
    {
        return Pad.GetWorldTimerManager().GetTimerRemaining(Pad.RetryTimer);
    }
    static int32 Retries(const AConstructClaimPad& Pad)
    {
        return Pad.Retries;
    }
    static bool Busy(const AConstructClaimPad& Pad)
    {
        return Pad.bBusy;
    }
    static bool Releasing(const AConstructClaimPad& Pad)
    {
        return Pad.bReleasing;
    }
    static void ClearRetry(AConstructClaimPad& Pad)
    {
        Pad.GetWorldTimerManager().ClearTimer(Pad.RetryTimer);
    }
    static void CompleteName(AConstructClaimPad& Pad, int64 Owner, const FString& Name)
    {
        Pad.NameRequest = NewObject<UConstructClaimNameRequest>(&Pad);
        Pad.NameRequest->OwnerId = Owner;
        Pad.NameReturned(Pad.NameRequest, Name);
    }
    static void StaleName(AConstructClaimPad& Pad, UConstructClaimNameRequest* Request)
    {
        Pad.NameReturned(Request, TEXT("Stale"));
    }
};

namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    struct FClaimsFixture
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
        AConstructClaimPad* Pad = World->SpawnActor<AConstructClaimPad>();
        UConstructClaimsSubsystem* Search = NewObject<UConstructClaimsSubsystem>(World);
        UConstructClaimsTestReceiver* Receiver = NewObject<UConstructClaimsTestReceiver>(Pad);

        FClaimsFixture()
        {
            Pad->OnClaimDone.AddDynamic(Receiver, &UConstructClaimsTestReceiver::Claimed);
            Pad->OnReleaseDone.AddDynamic(Receiver, &UConstructClaimsTestReceiver::Released);
        }
        ~FClaimsFixture()
        {
            FConstructClaimsTestAccess::ClearRetry(*Pad);
            World->DestroyWorld(false);
        }
        FCrowdyChunkClaimInfo Owner(bool bMine) const
        {
            FCrowdyChunkClaimInfo Claim;
            Claim.GridId = 900000000000000001LL;
            Claim.OwnerUserId = 900000000000000002LL;
            Claim.bOwnedByMe = bMine;
            const FInt64Vector Chunk = Pad->GetClaimChunk();
            Claim.ChunkX = Chunk.X;
            Claim.ChunkY = Chunk.Y;
            Claim.ChunkZ = Chunk.Z;
            return Claim;
        }
        void Deliver(ECrowdyClaimResult Result, const FCrowdyChunkClaimInfo* Claim = nullptr) const
        {
            const FInt64Vector Chunk = Pad->GetClaimChunk();
            FConstructClaimsTestAccess::Issued(*Search, Chunk, 1);
            FCrowdyClaimSearch Reply;
            Reply.Result = Result;
            Reply.Message = TEXT("Synthetic refusal");
            if (Claim)
            {
                Reply.Claims.Add(*Claim);
            }
            FConstructClaimsTestAccess::Answer(*Search, Chunk, Chunk, 1, Reply);
        }
    };

    FCrowdyServerCallResult IdempotentReply(const TCHAR* Flag)
    {
        FCrowdyServerCallResult Reply;
        Reply.Outcome = ECrowdyServerCallOutcome::Success;
        FInstancedPropertyBag Values;
        Values.AddProperty(FName(Flag), EPropertyBagPropertyType::Bool);
        Values.SetValueBool(FName(Flag), false);
        Reply.Reply.InitializeAs(Values.GetPropertyBagStruct(), Values.GetValue().GetMemory());
        return Reply;
    }

    FCrowdyServerCallResult HubReply(ECrowdyServerCallOutcome Outcome, const TCHAR* Reason = TEXT(""))
    {
        FCrowdyServerCallResult Reply;
        Reply.Outcome = Outcome;
        Reply.Reason = Reason;
        return Reply;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimLocation, "Construct.Claims.Location", Flags)
bool FConstructClaimLocation::RunTest(const FString&)
{
    FClaimsFixture F;
    F.Pad->SetActorLocation(FVector(-1, -1601, 1610));
    TestEqual(TEXT("The claim follows the pad's negative and elevated location"), F.Pad->GetClaimChunk(), FInt64Vector(-1, -2, 1));
    F.Pad->SetActorLocation(FVector(103200, 800, 10));
    TestEqual(TEXT("Moving the pad changes its chunk"), F.Pad->GetClaimChunk(), FInt64Vector(64, 0, 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimStates, "Construct.Claims.States", Flags)
bool FConstructClaimStates::RunTest(const FString&)
{
    FClaimsFixture F;
    TestFalse(TEXT("Unknown cannot claim"), FConstructClaimsTestAccess::CanClaim(*F.Pad));
    TestFalse(TEXT("Unknown cannot release"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
    F.Deliver(ECrowdyClaimResult::Success);
    TestTrue(TEXT("A confirmed free chunk offers Claim"), FConstructClaimsTestAccess::CanClaim(*F.Pad));
    auto Claim = F.Owner(true);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    TestFalse(TEXT("Mine cannot claim again"), FConstructClaimsTestAccess::CanClaim(*F.Pad));
    TestTrue(TEXT("Mine can release"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
    Claim.bOwnedByMe = false;
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    TestFalse(TEXT("Another's cannot claim"), FConstructClaimsTestAccess::CanClaim(*F.Pad));
    TestFalse(TEXT("Another's cannot release"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimInitialFailure, "Construct.Claims.InitialSearchFailure", Flags)
bool FConstructClaimInitialFailure::RunTest(const FString&)
{
    FClaimsFixture F;
    F.Deliver(ECrowdyClaimResult::Failed);
    int64 Grid, Owner;
    bool Mine;
    FString Name;
    TestFalse(TEXT("A failed initial search proves no ownership"), F.Pad->GetShownOwner(Grid, Owner, Mine, Name));
    F.Pad->Claim();
    F.Pad->Release();
    TestFalse(TEXT("Neither action starts from unknown"), FConstructClaimsTestAccess::Busy(*F.Pad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimRefreshFailure, "Construct.Claims.RefreshFailure", Flags)
bool FConstructClaimRefreshFailure::RunTest(const FString&)
{
    FClaimsFixture F;
    auto Claim = F.Owner(true);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    F.Deliver(ECrowdyClaimResult::Refused);
    int64 Grid, Owner;
    bool Mine;
    FString Name;
    TestTrue(TEXT("Failed refresh retains confirmed ownership"), F.Pad->GetShownOwner(Grid, Owner, Mine, Name));
    TestEqual(TEXT("Grid is preserved"), Grid, Claim.GridId);
    TestTrue(TEXT("Mine is preserved"), Mine);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimBounds, "Construct.Claims.OneSearchBounds", Flags)
bool FConstructClaimBounds::RunTest(const FString&)
{
    FClaimsFixture F;
    F.Pad->SetActorLocation(FVector(-800, -800, 20));
    auto* B = F.World->SpawnActor<AConstructClaimPad>();
    B->SetActorLocation(FVector(-800, 800, 20));
    auto* C = F.World->SpawnActor<AConstructClaimPad>();
    C->SetActorLocation(FVector(800, -800, 20));
    FInt64Vector Low, High;
    TestTrue(TEXT("Pads supply one bounding search"), FConstructClaimsTestAccess::Bounds(*F.Search, Low, High));
    TestEqual(TEXT("All low edges are included"), Low, FInt64Vector(-1, -1, 0));
    TestEqual(TEXT("All high edges are included"), High, FInt64Vector(0, 0, 0));
    TestFalse(TEXT("No search before sign-in"), FConstructClaimsTestAccess::Initial(*F.Search, false, Low, High));
    TestTrue(TEXT("Sign-in starts one shared search"), FConstructClaimsTestAccess::Initial(*F.Search, true, Low, High));
    TestFalse(TEXT("The next start does not issue a second search"), FConstructClaimsTestAccess::Initial(*F.Search, true, Low, High));
    FCrowdyClaimSearch Reply;
    Reply.Result = ECrowdyClaimResult::Success;
    for (AConstructClaimPad* Pad : {F.Pad, B, C})
    {
        FConstructClaimsTestAccess::Issued(*F.Search, Pad->GetClaimChunk(), 1);
    }
    FConstructClaimsTestAccess::Answer(*F.Search, Low, High, 1, Reply);
    for (AConstructClaimPad* Pad : {F.Pad, B, C})
    {
        TestTrue(TEXT("The single answer reaches every pad"), FConstructClaimsTestAccess::CanClaim(*Pad));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimRefusedRecord, "Construct.Claims.RecordRefusal", Flags)
bool FConstructClaimRefusedRecord::RunTest(const FString&)
{
    FClaimsFixture F;
    auto Claim = F.Owner(true);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    FConstructClaimsTestAccess::BeginClaim(*F.Pad);
    FConstructClaimsTestAccess::Hub(*F.Pad, HubReply(ECrowdyServerCallOutcome::ServerError, TEXT("the registry is full")));
    TestEqual(TEXT("One claim completion"), F.Receiver->ClaimCount, 1);
    TestEqual(TEXT("Platform success is preserved"), F.Receiver->Result, ECrowdyClaimResult::Success);
    TestEqual(TEXT("The real grid remains in the result"), F.Receiver->Grid, Claim.GridId);
    TestFalse(TEXT("The refused record is not listed"), F.Receiver->bListed);
    TestFalse(TEXT("Hub refusal does not enter the release path"), FConstructClaimsTestAccess::Releasing(*F.Pad));
    TestEqual(TEXT("No release completion was emitted"), F.Receiver->ReleaseCount, 0);
    TestTrue(TEXT("Confirmed platform ownership is retained"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimIdempotentRecord, "Construct.Claims.IdempotentRecord", Flags)
bool FConstructClaimIdempotentRecord::RunTest(const FString&)
{
    FClaimsFixture F;
    FConstructClaimsTestAccess::BeginClaim(*F.Pad);
    // An unchanged matching record succeeds with bRecorded false.
    FConstructClaimsTestAccess::Hub(*F.Pad, IdempotentReply(TEXT("bRecorded")));
    TestTrue(TEXT("Already recorded is listed"), F.Receiver->bListed);
    TestEqual(TEXT("Only one completion"), F.Receiver->ClaimCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimIdempotentRelease, "Construct.Claims.IdempotentRelease", Flags)
bool FConstructClaimIdempotentRelease::RunTest(const FString&)
{
    FClaimsFixture F;
    FConstructClaimsTestAccess::BeginRelease(*F.Pad);
    // An already-absent record succeeds with bReleased false.
    FConstructClaimsTestAccess::Hub(*F.Pad, IdempotentReply(TEXT("bReleased")));
    TestTrue(TEXT("Already absent is unlisted"), F.Receiver->bUnlisted);
    TestEqual(TEXT("Only one completion"), F.Receiver->ReleaseCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimRetries, "Construct.Claims.RecordRetries", Flags)
bool FConstructClaimRetries::RunTest(const FString&)
{
    FClaimsFixture F;
    FConstructClaimsTestAccess::BeginClaim(*F.Pad);
    auto Reply = HubReply(ECrowdyServerCallOutcome::Denied, TEXT("cooldown"));
    Reply.bRetryable = true;
    FConstructClaimsTestAccess::Hub(*F.Pad, Reply);
    TestEqual(TEXT("Cooldown schedules the first retry"), FConstructClaimsTestAccess::Retries(*F.Pad), 1);
    TestEqual(TEXT("No premature completion"), F.Receiver->ClaimCount, 0);
    TestEqual(TEXT("The retry waits two seconds"), FConstructClaimsTestAccess::RetryDelay(*F.Pad), 2.0f);
    Reply = HubReply(ECrowdyServerCallOutcome::ServerError, TEXT("the claim could not be checked"));
    FConstructClaimsTestAccess::Hub(*F.Pad, Reply);
    TestEqual(TEXT("Read refusal schedules the second retry"), FConstructClaimsTestAccess::Retries(*F.Pad), 2);
    FConstructClaimsTestAccess::Hub(*F.Pad, Reply);
    TestEqual(TEXT("Third refusal completes exactly once"), F.Receiver->ClaimCount, 1);
    TestFalse(TEXT("Retry exhaustion leaves the claim unlisted"), F.Receiver->bListed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimBox, "Construct.Claims.BoxAndName", Flags)
bool FConstructClaimBox::RunTest(const FString&)
{
    FClaimsFixture F;
    F.Pad->SetActorLocation(FVector(-800, -800, 20));
    F.Pad->SetActorRotation(FRotator(0, 27, 0));
    F.Pad->SetActorScale3D(FVector(2, 3, 4));
    FConstructClaimsTestAccess::BeginClaim(*F.Pad);
    FConstructClaimsTestAccess::Hub(*F.Pad, HubReply(ECrowdyServerCallOutcome::Success));
    for (UStaticMeshComponent* Edge : FConstructClaimsTestAccess::Edges(*F.Pad))
    {
        TestFalse(TEXT("Hub success alone draws no box"), Edge->IsVisible());
    }
    auto Claim = F.Owner(true);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    int64 Grid, Owner;
    bool Mine;
    FString Name;
    F.Pad->GetShownOwner(Grid, Owner, Mine, Name);
    TestEqual(TEXT("An unresolved name uses the fallback"), Name, FString(TEXT("a player")));
    FBox Bounds(ForceInit);
    for (UStaticMeshComponent* Edge : FConstructClaimsTestAccess::Edges(*F.Pad))
    {
        TestTrue(TEXT("Every edge is visible from the search answer"), Edge->IsVisible());
        TestEqual(TEXT("The box has no collision"), Edge->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
        Bounds += Edge->GetComponentLocation();
    }
    TestEqual(TEXT("Box minimum is the world chunk corner"), Bounds.Min, FVector(-1600, -1600, 0));
    TestEqual(TEXT("Box maximum is the opposite world chunk corner"), Bounds.Max, FVector(0, 0, 1600));
    FConstructClaimsTestAccess::CompleteName(*F.Pad, Claim.OwnerUserId, TEXT("Synthetic owner"));
    F.Pad->GetShownOwner(Grid, Owner, Mine, Name);
    TestEqual(TEXT("The completed name replaces the fallback"), Name, FString(TEXT("Synthetic owner")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimGetter, "Construct.Claims.UnknownAndFreeGetter", Flags)
bool FConstructClaimGetter::RunTest(const FString&)
{
    FClaimsFixture F;
    int64 Grid = -1, Owner = -1;
    bool Mine = true;
    FString Name = TEXT("stale");
    TestFalse(TEXT("Unknown returns false"), F.Pad->GetShownOwner(Grid, Owner, Mine, Name));
    F.Deliver(ECrowdyClaimResult::Success);
    TestTrue(TEXT("Confirmed free returns true"), F.Pad->GetShownOwner(Grid, Owner, Mine, Name));
    TestEqual(TEXT("Free grid is zero"), Grid, int64(0));
    TestEqual(TEXT("Free owner is zero"), Owner, int64(0));
    TestFalse(TEXT("Free is not mine"), Mine);
    TestTrue(TEXT("Free has no name"), Name.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimReleaseOffer, "Construct.Claims.ReleaseOnlyMine", Flags)
bool FConstructClaimReleaseOffer::RunTest(const FString&)
{
    FClaimsFixture F;
    auto Claim = F.Owner(false);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    TestFalse(TEXT("Another owner's claim offers no release"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
    F.Pad->Release();
    TestFalse(TEXT("Release ignores another owner's claim"), FConstructClaimsTestAccess::Busy(*F.Pad));
    Claim.bOwnedByMe = true;
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    TestTrue(TEXT("The local owner's claim offers release"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimStaleSearch, "Construct.Claims.StaleSearch", Flags)
bool FConstructClaimStaleSearch::RunTest(const FString&)
{
    FClaimsFixture F;
    auto Claim = F.Owner(true);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    const FInt64Vector Chunk = F.Pad->GetClaimChunk();
    FConstructClaimsTestAccess::Issued(*F.Search, Chunk, 2);
    FCrowdyClaimSearch Old;
    Old.Result = ECrowdyClaimResult::Success;
    FConstructClaimsTestAccess::Answer(*F.Search, Chunk, Chunk, 1, Old);
    TestTrue(TEXT("An older empty search cannot erase the newer ownership"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimStaleName, "Construct.Claims.StaleName", Flags)
bool FConstructClaimStaleName::RunTest(const FString&)
{
    FClaimsFixture F;
    auto Claim = F.Owner(true);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    auto* Old = NewObject<UConstructClaimNameRequest>(F.Pad);
    Old->OwnerId = Claim.OwnerUserId;
    F.Deliver(ECrowdyClaimResult::Success);
    FConstructClaimsTestAccess::StaleName(*F.Pad, Old);
    int64 Grid, Owner;
    bool Mine;
    FString Name;
    F.Pad->GetShownOwner(Grid, Owner, Mine, Name);
    TestTrue(TEXT("A stale name cannot label a free chunk"), Name.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimCancel, "Construct.Claims.SessionCancellation", Flags)
bool FConstructClaimCancel::RunTest(const FString&)
{
    FClaimsFixture F;
    auto Claim = F.Owner(true);
    F.Deliver(ECrowdyClaimResult::Success, &Claim);
    FConstructClaimsTestAccess::BeginClaim(*F.Pad);
    bool bKnownDuringCompletion = true;
    F.Receiver->ClaimHandler = [&]()
    {
        int64 Grid, Owner;
        bool Mine;
        FString Name;
        bKnownDuringCompletion = F.Pad->GetShownOwner(Grid, Owner, Mine, Name);
        TestFalse(TEXT("No release is offered during cancellation"), FConstructClaimsTestAccess::CanRelease(*F.Pad));
        F.Pad->Release();
    };
    FConstructClaimsTestAccess::Reset(*F.Pad);
    TestFalse(TEXT("Completion observes cleared ownership"), bKnownDuringCompletion);
    TestFalse(TEXT("Cancellation leaves no active action"), FConstructClaimsTestAccess::Busy(*F.Pad));
    TestEqual(TEXT("Cancellation finishes the pending claim once"), F.Receiver->ClaimCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructClaimPendingSession, "Construct.Claims.PendingSessionAnswer", Flags)
bool FConstructClaimPendingSession::RunTest(const FString&)
{
    FClaimsFixture F;
    FConstructClaimsTestAccess::WaitForPlatform(*F.Pad);
    FConstructClaimsTestAccess::Reset(*F.Pad);
    TestEqual(TEXT("Sign-out does not invent a platform result"), F.Receiver->ClaimCount, 0);
    TestTrue(TEXT("The real platform answer remains pending"), FConstructClaimsTestAccess::Busy(*F.Pad));
    FCrowdyChunkClaim Reply;
    Reply.Result = ECrowdyClaimResult::Success;
    Reply.GridId = 900000000000000001LL;
    FConstructClaimsTestAccess::Platform(*F.Pad, Reply);
    TestEqual(TEXT("The actual answer completes once"), F.Receiver->ClaimCount, 1);
    TestEqual(TEXT("The SDK result is preserved"), F.Receiver->Result, Reply.Result);
    TestEqual(TEXT("The SDK grid is preserved"), F.Receiver->Grid, Reply.GridId);
    TestFalse(TEXT("The old session does not list a claim"), F.Receiver->bListed);
    TestEqual(TEXT("The old session cannot start hub work"), F.Receiver->Reason, FString(TEXT("The sign-in session ended.")));
    return true;
}

#endif
