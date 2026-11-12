#include "ConstructModMenu.h"

#include "ConstructClaimPad.h"
#include "ConstructClaimsSubsystem.h"
#include "ConstructModPanel.h"
#include "ConstructUIInputSubsystem.h"
#include "CrowdyExecCodec.h"
#include "CrowdyServerObject.h"
#include "CrowdyServerObjectDefinition.h"
#include "CrowdyServerObjectSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "StructUtils/PropertyBag.h"
#include "Subsystem/CrowdyGameSession.h"
#include "TimerManager.h"
#include "Widgets/SBoxPanel.h"

bool UConstructModMenu::IsCurrent(uint64 ForGeneration) const
{
    // The world owner invalidates this generation on every session/controller lifetime signal.
    return bOpen && Generation == ForGeneration && BoundWorld.IsValid();
}

void UConstructModMenu::Open()
{
    if (bOpen || bClosing)
    {
        return;
    }
    BoundWorld = GetWorld();
    UGameInstance* Game = GetGameInstance();
    const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
    UserId = Session ? Session->GetUserID() : 0;
    if (!Session || Session->HasAccessEnded() || UserId <= 0)
    {
        return;
    }
    InputOwner = GetOwningLocalPlayer();
    bOpen = true;
    SetIsFocusable(true);
    AddToPlayerScreen(30);
    if (ULocalPlayer* Player = GetOwningLocalPlayer())
    {
        Player->GetSubsystem<UConstructUIInputSubsystem>()->SetModal(TEXT("Construct.Mods"), true, this);
    }
    ObserveClaims(GetWorld()->GetSubsystem<UConstructClaimsSubsystem>());
    Discover();
}

void UConstructModMenu::Close()
{
    if (!bOpen)
    {
        return;
    }
    bClosing = true;
    bOpen = false;
    bHubPending = false;
    HubError.Reset();
    ++Generation;
    ReleaseHub();
    if (UWorld* World = BoundWorld.Get())
    {
        World->GetTimerManager().ClearTimer(RetryTimer);
    }
    ObserveClaims(nullptr);
    ClosePanel();
    Rows.Reset();
    HubRows.Reset();
    PadChunks.Reset();
    ConfirmRequests.Reset();
    Confirmed.Reset();
    if (ULocalPlayer* Player = InputOwner.Get())
    {
        if (UConstructUIInputSubsystem* Input = Player->GetSubsystem<UConstructUIInputSubsystem>())
        {
            Input->SetModal(TEXT("Construct.Mods"), false);
        }
    }
    InputOwner.Reset();
    BoundWorld.Reset();
    RemoveFromParent();
    bClosing = false;
}

void UConstructModMenu::NativeDestruct()
{
    Close();
    Super::NativeDestruct();
}

FReply UConstructModMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape)
    {
        Close();
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void UConstructModMenu::Discover()
{
    if (!bOpen)
    {
        return;
    }
    ++Generation;
    ReleaseHub();
    BoundWorld->GetTimerManager().ClearTimer(RetryTimer);
    bRetried = false;
    bHubPending = true;
    HubError.Reset();
    ConfirmRequests.Reset();
    Confirmed.Reset();
    LookupFloor = Confirmations.IsValid() ? Confirmations->GetLookupSequence() + 1 : MAX_uint64;
    PadChunks.Reset();
    for (TActorIterator<AConstructClaimPad> It(BoundWorld.Get()); It; ++It)
    {
        PadChunks.AddUnique(It->GetClaimChunk());
    }
    RebuildCandidates();
    ConfirmCandidates();
    BeginHub();
}

void UConstructModMenu::RebuildCandidates()
{
    PadChunks.Sort([](const FInt64Vector& A, const FInt64Vector& B)
    {
        return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z;
    });
    HubRows.Sort([](const FConstructHubClaim& A, const FConstructHubClaim& B)
    {
        return A.GridId < B.GridId;
    });
    TArray<FInt64Vector> Candidates = PadChunks;
    for (const FConstructHubClaim& Claim : HubRows)
    {
        Candidates.AddUnique(FInt64Vector(Claim.Chunk.X, Claim.Chunk.Y, Claim.Chunk.Z));
    }
    Omitted = FMath::Max(0, Candidates.Num() - 64);
    Rows.Reset();
    for (int32 Index = 0; Index < FMath::Min(64, Candidates.Num()); ++Index)
    {
        const FInt64Vector Chunk = Candidates[Index];
        if (const FConstructModLand* Existing = Confirmed.Find(Chunk))
        {
            Rows.Add(*Existing);
        }
        else
        {
            FConstructModLand Row;
            Row.Chunk = Chunk;
            Row.LookupFloor = LookupFloor;
            Rows.Add(MoveTemp(Row));
        }
    }
    RebuildRows();
}

void UConstructModMenu::ConfirmCandidates()
{
    UConstructClaimsSubsystem* Owner = Confirmations.Get();
    if (!Owner || !IsCurrent(Generation))
    {
        return;
    }
    // The owner supplies currency, pad presentation and session/world-safe completion for both callers.
    for (const FConstructModLand& Row : Rows)
    {
        const FInt64Vector Chunk = Row.Chunk;
        if ((bHubPending && !PadChunks.Contains(Chunk)) || ConfirmRequests.Contains(Chunk))
        {
            continue;
        }
        const uint64 Request = Owner->ConfirmChunk(Chunk);
        if (Request != 0)
        {
            ConfirmRequests.Add(Chunk, Request);
        }
    }
}
UConstructModPanel* UConstructModMenu::OpenForGrid(int64 GridId)
{
    if (!IsCurrent(Generation) || GridId <= 0 || !CanSelectGrid(GridId))
    {
        return nullptr;
    }
    const FConstructModLand* Row = Rows.FindByPredicate([GridId](const FConstructModLand& Item)
    {
        return Item.bConfirmed && Item.GridId == GridId;
    });
    if (!Row || !CanOpenRow(*Row))
    {
        return nullptr;
    }
    if (Panel && OpenGrid == GridId)
    {
        return Panel;
    }
    const FInt64Vector Chunk = Row->Chunk;
    const uint64 ForGeneration = Generation;
    ClosePanel();
    if (!IsCurrent(ForGeneration))
    {
        return nullptr;
    }
    Panel = CreateWidget<UConstructModPanel>(GetOwningPlayer());
    if (!Panel)
    {
        return nullptr;
    }
    OpenGrid = GridId;
    Panel->BindGrid(GridId, Chunk);
    if (PanelBox)
    {
        PanelBox->AddSlot().FillHeight(1)[Panel->TakeWidget()];
    }
    return Panel;
}

UConstructModPanel* UConstructModMenu::OpenForPad(AConstructClaimPad& Pad)
{
    // The pad supplied the latest platform ownership answer, so no account-wide registry read is needed.
    if (bClosing || !Pad.bLatestSearchSucceeded || !Pad.bKnown || !Pad.bMine || Pad.ShownGrid <= 0 || Pad.bEnding)
    {
        return nullptr;
    }
    if (!bOpen)
    {
        BoundWorld = GetWorld();
        UGameInstance* Game = GetGameInstance();
        const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
        UserId = Session ? Session->GetUserID() : 0;
        if (!Session || Session->HasAccessEnded() || UserId <= 0 || Pad.ShownOwner != UserId)
        {
            return nullptr;
        }
        InputOwner = GetOwningLocalPlayer();
        ObserveClaims(GetWorld()->GetSubsystem<UConstructClaimsSubsystem>());
        bOpen = true;
        ++Generation;
        SetIsFocusable(true);
        AddToPlayerScreen(30);
        if (ULocalPlayer* Player = GetOwningLocalPlayer())
        {
            Player->GetSubsystem<UConstructUIInputSubsystem>()->SetModal(TEXT("Construct.Mods"), true, this);
        }
    }
    if (!IsCurrent(Generation) || Pad.ShownOwner != UserId || Pad.GetWorld() != BoundWorld.Get())
    {
        return nullptr;
    }
    FConstructModLand Row;
    Row.Chunk = Pad.GetClaimChunk();
    Row.GridId = Pad.ShownGrid;
    Row.bPending = false;
    Row.bConfirmed = true;
    if (FConstructModLand* Existing = Rows.FindByPredicate([&Row](const FConstructModLand& Item) { return Item.Chunk == Row.Chunk; }))
    {
        *Existing = Row;
    }
    else
    {
        Rows.Add(Row);
    }
    Confirmed.Add(Row.Chunk, Row);
    // Any older menu lookup for this same chunk must not overwrite the newer pad answer.
    ConfirmRequests.Add(Row.Chunk, MAX_uint64);
    UConstructModPanel* Result = OpenForGrid(Row.GridId);
    if (Result)
    {
        if (AConstructClaimPad* Previous = BoundPad.Get())
        {
            Previous->OnOwnerShown.RemoveDynamic(this, &ThisClass::PadOwnerChanged);
            Previous->OnEndPlay.RemoveDynamic(this, &ThisClass::PadEnded);
        }
        BoundPad = &Pad;
        Pad.OnOwnerShown.AddUniqueDynamic(this, &ThisClass::PadOwnerChanged);
        Pad.OnEndPlay.AddUniqueDynamic(this, &ThisClass::PadEnded);
    }
    RebuildRows();
    return Result;
}

void UConstructModMenu::PadOwnerChanged(int64 GridId, int64 OwnerUserId, bool bOwnedByMe)
{
    if (Panel && (GridId != OpenGrid || OwnerUserId != UserId || !bOwnedByMe))
    {
        ClosePanel();
    }
}

void UConstructModMenu::PadEnded(AActor*, EEndPlayReason::Type)
{
    ClosePanel();
}

void UConstructModMenu::ClosePanel()
{
    if (AConstructClaimPad* Pad = BoundPad.Get())
    {
        Pad->OnOwnerShown.RemoveDynamic(this, &ThisClass::PadOwnerChanged);
        Pad->OnEndPlay.RemoveDynamic(this, &ThisClass::PadEnded);
    }
    BoundPad.Reset();
    OpenGrid = 0;
    if (Panel)
    {
        Panel->CloseBinding();
        Panel->RemoveFromParent();
        Panel = nullptr;
    }
    if (PanelBox)
    {
        PanelBox->ClearChildren();
    }
}

void UConstructModMenu::BeginHub()
{
    if (!IsCurrent(Generation))
    {
        return;
    }
    UGameInstance* Game = GetGameInstance();
    UCrowdyServerObjectSubsystem* Objects = Game ? Game->GetSubsystem<UCrowdyServerObjectSubsystem>() : nullptr;
    auto* Definition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructClaims.CSO_ConstructClaims"));
    FString Error;
    UCrowdyServerObject* Object = Objects && Definition ? Objects->Acquire(Definition, TEXT("main"), this, Error) : nullptr;
    if (!Object)
    {
        HubFailed(Error.IsEmpty() ? TEXT("The claim registry is unavailable.") : Error);
        return;
    }
    ++HubAttempt;
    Hub = Object;
    bHubCalled = false;
    StatusHandle = Object->OnStatusChanged.AddUObject(this, &ThisClass::HubStatusChanged);
    const uint64 ForGeneration = Generation, ForAttempt = HubAttempt;
    const TWeakObjectPtr<UConstructModMenu> WeakThis(this);
    BoundWorld->GetTimerManager().SetTimer(HubTimer, FTimerDelegate::CreateLambda([WeakThis, ForGeneration, ForAttempt]()
    {
        if (UConstructModMenu* Self = WeakThis.Get(); Self && Self->IsCurrent(ForGeneration) && Self->HubAttempt == ForAttempt)
        {
            Self->HubFailed(TEXT("The claim registry timed out."));
        }
    }), 30.0f, false);
    HubStatusChanged(Object, Object->GetStatus());
}

void UConstructModMenu::HubStatusChanged(UCrowdyServerObject* Object, ECrowdyServerObjectStatus Status)
{
    if (!IsCurrent(Generation) || Hub.Get() != Object || bHubCalled)
    {
        return;
    }
    if (Status == ECrowdyServerObjectStatus::Failed)
    {
        HubFailed(Object->GetFailureReason());
    }
    else if (Status == ECrowdyServerObjectStatus::Ready)
    {
        const FName Function(TEXT("ListClaims"));
        FInstancedPropertyBag Params = Object->MakeParams(Function);
        if (Params.SetValueBool(TEXT("bOnlyChunk"), false) != EPropertyBagResult::Success || Params.SetValueBool(TEXT("bOnlyMine"), true) != EPropertyBagResult::Success || Params.SetValueInt32(TEXT("Limit"), 1000) != EPropertyBagResult::Success)
        {
            HubFailed(TEXT("The claim registry inputs do not match."));
            return;
        }
        bHubCalled = true;
        const uint64 ForGeneration = Generation, ForAttempt = HubAttempt;
        const TWeakObjectPtr<UConstructModMenu> WeakThis(this);
        Object->Call(Function, Params, [WeakThis, ForGeneration, ForAttempt](const FCrowdyServerCallResult& Reply)
        {
            if (UConstructModMenu* Self = WeakThis.Get())
            {
                Self->HubReturned(ForGeneration, ForAttempt, Reply);
            }
        });
    }
}

void UConstructModMenu::HubReturned(uint64 ForGeneration, uint64 ForAttempt, const FCrowdyServerCallResult& Reply)
{
    if (!IsCurrent(ForGeneration) || HubAttempt != ForAttempt)
    {
        return;
    }
    ReleaseHub();
    if (!Reply.IsSuccess())
    {
        if (!bRetried && Reply.Outcome == ECrowdyServerCallOutcome::Denied && Reply.bRetryable)
        {
            bRetried = true;
            const TWeakObjectPtr<UConstructModMenu> WeakThis(this);
            BoundWorld->GetTimerManager().SetTimer(RetryTimer, FTimerDelegate::CreateLambda([WeakThis, ForGeneration]()
            {
                if (UConstructModMenu* Self = WeakThis.Get(); Self && Self->IsCurrent(ForGeneration))
                {
                    Self->BeginHub();
                }
            }), 1.0f, false);
        }
        else
        {
            HubFailed(Reply.Reason);
        }
        return;
    }
    const FInstancedPropertyBag Values = CrowdyExec::ToList(Reply.Reply);
    const auto Claims = Values.GetArrayRef(TEXT("Claims"));
    if (!Claims.HasValue())
    {
        HubFailed(TEXT("The claim registry reply does not match."));
        return;
    }
    TArray<FConstructHubClaim> FreshRows;
    for (int32 Index = 0; Index < Claims.GetValue().Num(); ++Index)
    {
        const auto Row = Claims.GetValue().GetValueStruct<FConstructHubClaim>(Index);
        if (!Row.HasValue() || !Row.GetValue())
        {
            HubFailed(TEXT("The claim registry row does not match."));
            return;
        }
        FreshRows.Add(*Row.GetValue());
    }
    HubRows = MoveTemp(FreshRows);
    bHubPending = false;
    HubError.Reset();
    RebuildCandidates();
    ConfirmCandidates();
}

void UConstructModMenu::HubFailed(const FString& Message)
{
    ReleaseHub();
    bHubPending = false;
    HubError = Message.IsEmpty() ? TEXT("The claim registry could not be read.") : Message;
    RebuildCandidates();
    ConfirmCandidates();
}

void UConstructModMenu::ReleaseHub()
{
    ++HubAttempt;
    if (UWorld* World = BoundWorld.Get())
    {
        World->GetTimerManager().ClearTimer(HubTimer);
    }
    if (UCrowdyServerObject* Object = Hub.Get())
    {
        Object->OnStatusChanged.Remove(StatusHandle);
        UGameInstance* Game = GetGameInstance();
        if (UCrowdyServerObjectSubsystem* Objects = Game ? Game->GetSubsystem<UCrowdyServerObjectSubsystem>() : nullptr)
        {
            Objects->Release(Object, this);
        }
    }
    Hub.Reset();
    StatusHandle.Reset();
}
bool UConstructModMenu::CanSelectGrid(int64 GridId) const
{
    return !Panel || GridId == OpenGrid || Panel->MutationKind == UConstructModPanel::EMutation::None;
}
void UConstructModMenu::ObserveClaims(UConstructClaimsSubsystem* Owner)
{
    if (UConstructClaimsSubsystem* Previous = Confirmations.Get())
    {
        Previous->OnLookupChanged.Remove(LookupHandle);
    }
    Confirmations = Owner;
    LookupHandle.Reset();
    if (Owner)
    {
        LookupHandle = Owner->OnLookupChanged.AddUObject(this, &ThisClass::LookupChanged);
    }
}

void UConstructModMenu::LookupChanged(FInt64Vector Chunk)
{
    ClaimsChanged(Chunk.X, Chunk.Y, Chunk.Z);
}

void UConstructModMenu::ClaimsChanged(int64 X, int64 Y, int64 Z)
{
    UConstructClaimsSubsystem* Owner = Confirmations.Get();
    if (!IsCurrent(Generation) || !Owner)
    {
        return;
    }
    const FInt64Vector Chunk(X, Y, Z);
    FConstructModLand* Row = Rows.FindByPredicate([Chunk](const FConstructModLand& Item) { return Item.Chunk == Chunk; });
    const FConstructClaimLookup* Lookup = Owner->GetLookup(Chunk);
    if (!Row || !Lookup || Lookup->Request < Row->LookupFloor)
    {
        return;
    }
    Row->bPending = Lookup->bPending;
    const FCrowdyChunkClaimInfo* Claim = Lookup->Claim.IsSet() ? &Lookup->Claim.GetValue() : nullptr;
    Row->bConfirmed = !Lookup->bPending && Lookup->bSucceeded && Claim && Claim->bOwnedByMe && Claim->OwnerUserId == UserId && Claim->GridId > 0;
    Row->GridId = Row->bConfirmed ? Claim->GridId : 0;
    Row->Message = Lookup->bSucceeded ? TEXT("Not owned by you.") : Lookup->Message.IsEmpty() ? TEXT("Ownership could not be confirmed.") : Lookup->Message;
    Confirmed.Add(Chunk, *Row);
    if (!Lookup->bPending && Lookup->bSucceeded && Panel && Panel->Chunk == Chunk && (!Row->bConfirmed || Row->GridId != OpenGrid))
    {
        ClosePanel();
    }
    RebuildRows();
}
bool UConstructModMenu::CanOpenRow(const FConstructModLand& Row) const
{
    const UConstructClaimsSubsystem* Owner = Confirmations.Get();
    const FConstructClaimLookup* Lookup = Owner ? Owner->GetLookup(Row.Chunk) : nullptr;
    if (!Row.bConfirmed || !Lookup || Lookup->Request < Row.LookupFloor || Lookup->bPending || !Lookup->bSucceeded || !Lookup->Claim.IsSet())
    {
        return false;
    }
    const FCrowdyChunkClaimInfo& Claim = Lookup->Claim.GetValue();
    return Claim.bOwnedByMe && Claim.OwnerUserId == UserId && Claim.GridId > 0 && Claim.GridId == Row.GridId;
}