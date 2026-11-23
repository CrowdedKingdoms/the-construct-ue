#include "ConstructClaimPad.h"

#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "ConstructModsSubsystem.h"
#include "ConstructClaimNameRequest.h"
#include "ConstructClaimsSubsystem.h"
#include "ConstructChunkDrawing.h"
#include "ConstructHubTypes.h"
#include "ConstructUIInputSubsystem.h"
#include "CrowdyServerObject.h"
#include "CrowdyServerObjectDefinition.h"
#include "CrowdyServerObjectSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Subsystem/CrowdyPlayers.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

AConstructClaimPad::AConstructClaimPad()
{
    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("ClaimTrigger"));
    SetRootComponent(Trigger);
    Trigger->SetBoxExtent(FVector(180, 180, 100));
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Trigger->SetGenerateOverlapEvents(true);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mine(*ConstructChunkDrawing::GetBlockMaterialPath(6));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Other(*ConstructChunkDrawing::GetBlockMaterialPath(3));
    MineMaterial = Mine.Object;
    OtherMaterial = Other.Object;
    Platform = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClaimPlatform"));
    Platform->SetupAttachment(Trigger);
    Platform->SetStaticMesh(Cylinder.Object);
    Platform->SetRelativeScale3D(FVector(3.6, 3.6, 0.1));
    Platform->SetRelativeLocation(FVector(0, 0, -15));
    Platform->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ClaimSign"));
    Sign->SetupAttachment(Trigger);
    Sign->SetRelativeLocation(FVector(0, 0, 150));
    Sign->SetRelativeRotation(FRotator(0, 180, 0));
    Sign->SetHorizontalAlignment(EHTA_Center);
    Sign->SetWorldSize(26);
    Sign->SetText(FText::FromString(TEXT("Claim pad\nOwnership unknown")));

    for (int32 Index = 0; Index < 12; ++Index)
    {
        UStaticMeshComponent* Edge = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("ClaimEdge%d"), Index));
        Edge->SetupAttachment(Trigger);
        Edge->SetAbsolute(true, true, true);
        Edge->SetStaticMesh(Cube.Object);
        Edge->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Edge->SetCastShadow(false);
        Edge->SetVisibility(false);
        Edges.Add(Edge);
    }
}

FInt64Vector AConstructClaimPad::GetClaimChunk() const
{
    FInt64Vector Chunk;
    FIntVector Cell;
    UCrowdyVoxelLibrary::WorldToVoxel(this, GetActorLocation(), Chunk.X, Chunk.Y, Chunk.Z, Cell);
    return Chunk;
}

void AConstructClaimPad::BeginPlay()
{
    Super::BeginPlay();
    Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::EnterPad);
    Trigger->OnComponentEndOverlap.AddDynamic(this, &ThisClass::LeavePad);
    UpdatePresentation();
}

void AConstructClaimPad::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding = true;
    ++ActionSerial;
    NameRequest = nullptr;
    GetWorldTimerManager().ClearAllTimersForObject(this);
    ReleaseHub();
    ReleaseInput();
    HideFeedback();
    Super::EndPlay(Reason);
}

bool AConstructClaimPad::GetShownOwner(int64& GridId, int64& OwnerUserId, bool& bOwnedByMe, FString& OwnerName) const
{
    GridId = ShownGrid;
    OwnerUserId = ShownOwner;
    bOwnedByMe = bMine;
    OwnerName = ShownName;
    return bKnown;
}

bool AConstructClaimPad::CanClaim() const
{
    return bKnown && ShownGrid == 0 && !bBusy;
}

bool AConstructClaimPad::CanRelease() const
{
    return bKnown && ShownGrid > 0 && bMine && !bBusy;
}

void AConstructClaimPad::ShowSearch(const FCrowdyChunkClaimInfo* Claim)
{
    const int64 PreviousOwner = ShownOwner;
    bLatestSearchSucceeded = true;
    bKnown = true;
    ShownGrid = Claim ? Claim->GridId : 0;
    ShownOwner = Claim ? Claim->OwnerUserId : 0;
    bMine = Claim && Claim->bOwnedByMe;
    const bool bReadName = ShownOwner > 0 && (ShownOwner != PreviousOwner || ShownName.IsEmpty());
    if (!Claim)
    {
        ShownName.Reset();
        NameRequest = nullptr;
    }
    else if (bReadName)
    {
        ShownName = TEXT("a player");
        NameRequest = nullptr;
    }
    UpdatePresentation();
    OnOwnerShown.Broadcast(ShownGrid, ShownOwner, bMine);
    if (!bReadName || bEnding)
    {
        return;
    }

    UCrowdyPlayers* Players = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyPlayers>() : nullptr;
    if (!Players)
    {
        OnOwnerNamed.Broadcast(ShownOwner, ShownName);
        return;
    }
    NameRequest = NewObject<UConstructClaimNameRequest>(this);
    NameRequest->Pad = this;
    NameRequest->OwnerId = ShownOwner;
    FOnPlayerProfileRead Success;
    Success.BindDynamic(NameRequest, &UConstructClaimNameRequest::Succeeded);
    FOnPlayersError Failure;
    Failure.BindDynamic(NameRequest, &UConstructClaimNameRequest::Failed);
    Players->GetPlayerName(ShownOwner, Success, Failure);
}

void UConstructClaimNameRequest::Succeeded(FCrowdyPlayerProfile Profile)
{
    if (AConstructClaimPad* Target = Pad.Get())
    {
        Target->NameReturned(this, Profile.UserID == OwnerId ? Profile.Gamertag : FString());
    }
}

void UConstructClaimNameRequest::Failed(FCrowdyAccountError, FString)
{
    if (AConstructClaimPad* Target = Pad.Get())
    {
        Target->NameReturned(this, FString());
    }
}

void AConstructClaimPad::NameReturned(UConstructClaimNameRequest* Request, const FString& Name)
{
    if (Request != NameRequest || Request->OwnerId != ShownOwner || bEnding)
    {
        return;
    }
    ShownName = Name.IsEmpty() ? TEXT("a player") : Name;
    NameRequest = nullptr;
    UpdatePresentation();
    OnOwnerNamed.Broadcast(ShownOwner, ShownName);
}

void AConstructClaimPad::ResetSession()
{
    ++ActionSerial;
    bLatestSearchSucceeded = false;
    bKnown = false;
    ShownGrid = ShownOwner = 0;
    bMine = false;
    ShownName.Reset();
    NameRequest = nullptr;
    bActionSessionEnded = true;
    if (bBusy && !bWaitingForPlatform)
    {
        FinishAction(false, TEXT("The sign-in session ended."), false);
    }
    UpdatePresentation();
    OnOwnerShown.Broadcast(0, 0, false);
}

void AConstructClaimPad::UpdatePresentation()
{
    FString DisplayName;
    for (const TCHAR Character : ShownName)
    {
        if (Character == TEXT('\r') || Character == TEXT('\n') || DisplayName.Len() >= 64)
        {
            break;
        }
        if (!FChar::IsControl(Character) && Character != TEXT('<') && Character != TEXT('>'))
        {
            DisplayName.AppendChar(Character);
        }
    }
    if (!DisplayName.IsEmpty() && DisplayName[DisplayName.Len() - 1] >= 0xD800 && DisplayName[DisplayName.Len() - 1] <= 0xDBFF)
    {
        DisplayName.LeftChopInline(1);
    }
    FString State = TEXT("Ownership unknown");
    if (bKnown)
    {
        State = ShownGrid == 0 ? TEXT("Free\nE: Claim this chunk") : (bMine ? (TEXT("Your chunk\n") + DisplayName + TEXT("\nE: Release")) : TEXT("Claimed by\n") + DisplayName);
    }
    if (bBusy)
    {
        State += TEXT("\nWorking...");
    }
    Sign->SetText(FText::FromString(State));
    Sign->SetXScale(1.0f);
    const double TextWidth = Sign->GetTextLocalSize().Y;
    if (TextWidth > 300.0)
    {
        Sign->SetXScale(static_cast<float>(300.0 / TextWidth));
    }
    const FColor Colour = bMine ? FColor(75, 220, 150) : FColor(240, 175, 75);
    Sign->SetTextRenderColor(bKnown && ShownGrid > 0 ? Colour : FColor::White);

    const FInt64Vector Chunk = GetClaimChunk();
    const FVector Origin = UCrowdyVoxelLibrary::GetChunkOrigin(this, Chunk.X, Chunk.Y, Chunk.Z);
    const double Size = UCrowdyVoxelLibrary::GetVoxelSize(this) * 16.0;
    UMaterialInterface* Material = bMine ? MineMaterial : OtherMaterial;
    int32 Index = 0;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        for (int32 A = 0; A < 2; ++A)
        {
            for (int32 B = 0; B < 2; ++B)
            {
                FVector Position = Origin;
                Position[Axis] += Size * 0.5;
                Position[(Axis + 1) % 3] += Size * A;
                Position[(Axis + 2) % 3] += Size * B;
                FVector Scale(0.035);
                Scale[Axis] = Size / 100.0;
                UStaticMeshComponent* Edge = Edges[Index++];
                Edge->SetWorldLocationAndRotation(Position, FRotator::ZeroRotator);
                Edge->SetWorldScale3D(Scale);
                Edge->SetMaterial(0, Material);
                Edge->SetVisibility(bKnown && ShownGrid > 0);
            }
        }
    }
}

void AConstructClaimPad::Refresh()
{
    if (UConstructClaimsSubsystem* Claims = GetWorld()->GetSubsystem<UConstructClaimsSubsystem>())
    {
        Claims->Refresh(*this);
    }
}

void AConstructClaimPad::Claim()
{
    if (!CanClaim())
    {
        return;
    }
    UCrowdyClaims* Claims = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyClaims>() : nullptr;
    if (!Claims)
    {
        ShowFeedback(TEXT("Sign in before claiming a chunk."));
        return;
    }
    bBusy = true;
    bReleasing = false;
    Retries = 0;
    ClaimReply = {};
    const uint64 Request = ++ActionSerial;
    PlatformSerial = Request;
    bWaitingForPlatform = true;
    bActionSessionEnded = false;
    UpdatePresentation();
    const FInt64Vector Chunk = GetClaimChunk();
    const TWeakObjectPtr<AConstructClaimPad> WeakThis(this);
    Claims->ClaimChunk(Chunk.X, Chunk.Y, Chunk.Z, [WeakThis, Request](const FCrowdyChunkClaim& Reply)
    {
        if (AConstructClaimPad* Self = WeakThis.Get(); Self && !Self->bEnding && Self->PlatformSerial == Request && Self->bWaitingForPlatform)
        {
            Self->PlatformClaimReturned(Reply);
        }
    });
}

void AConstructClaimPad::Release()
{
    if (!CanRelease())
    {
        return;
    }
    UCrowdyClaims* Claims = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyClaims>() : nullptr;
    if (!Claims)
    {
        ShowFeedback(TEXT("Sign in before releasing a chunk."));
        return;
    }
    bBusy = true;
    bReleasing = true;
    ReleaseReply = {};
    const uint64 Request = ++ActionSerial;
    PlatformSerial = Request;
    bWaitingForPlatform = true;
    bActionSessionEnded = false;
    UpdatePresentation();
    const TWeakObjectPtr<AConstructClaimPad> WeakThis(this);
    Claims->ReleaseClaim(ShownGrid, [WeakThis, Request](const FCrowdyClaimRelease& Reply)
    {
        if (AConstructClaimPad* Self = WeakThis.Get(); Self && !Self->bEnding && Self->PlatformSerial == Request && Self->bWaitingForPlatform)
        {
            Self->PlatformReleaseReturned(Reply);
        }
    });
}

void AConstructClaimPad::PlatformClaimReturned(const FCrowdyChunkClaim& Reply)
{
    bWaitingForPlatform = false;
    ClaimReply = Reply;
    if (bActionSessionEnded)
    {
        FinishAction(false, TEXT("The sign-in session ended."), false);
        return;
    }
    if (Reply.Result == ECrowdyClaimResult::Success)
    {
        BeginHub();
    }
    else
    {
        FinishAction(false, Reply.Message);
    }
}

void AConstructClaimPad::PlatformReleaseReturned(const FCrowdyClaimRelease& Reply)
{
    bWaitingForPlatform = false;
    ReleaseReply = Reply;
    if (bActionSessionEnded)
    {
        FinishAction(false, TEXT("The sign-in session ended."), false);
        return;
    }
    if (Reply.Result == ECrowdyClaimResult::Success)
    {
        BeginHub();
    }
    else
    {
        FinishAction(false, Reply.Message);
    }
}

void AConstructClaimPad::BeginHub()
{
    UGameInstance* Game = GetGameInstance();
    UCrowdyServerObjectSubsystem* Objects = Game ? Game->GetSubsystem<UCrowdyServerObjectSubsystem>() : nullptr;
    auto* Definition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructClaims.CSO_ConstructClaims"));
    FString Error;
    UCrowdyServerObject* Object = Objects && Definition ? Objects->Acquire(Definition, TEXT("main"), this, Error) : nullptr;
    if (!Object)
    {
        FinishAction(false, Error.IsEmpty() ? TEXT("The hub is unavailable.") : Error);
        return;
    }
    Hub = Object;
    bHubCalled = false;
    HubStatusHandle = Object->OnStatusChanged.AddUObject(this, &ThisClass::HubStatusChanged);
    GetWorldTimerManager().SetTimer(HubTimer, this, &ThisClass::HubTimedOut, 15.0f);
    HubStatusChanged(Object, Object->GetStatus());
}

void AConstructClaimPad::HubStatusChanged(UCrowdyServerObject* Object, ECrowdyServerObjectStatus Status)
{
    if (!bBusy || bHubCalled || Hub.Get() != Object)
    {
        return;
    }
    if (Status == ECrowdyServerObjectStatus::Ready)
    {
        CallHub();
    }
    else if (Status == ECrowdyServerObjectStatus::Failed)
    {
        FinishAction(false, Object->GetFailureReason());
    }
}

void AConstructClaimPad::CallHub()
{
    UCrowdyServerObject* Object = Hub.Get();
    if (!Object || !bBusy)
    {
        return;
    }
    bHubCalled = true;
    GetWorldTimerManager().SetTimer(HubTimer, this, &ThisClass::HubTimedOut, 15.0f);
    const FName Function = bReleasing ? TEXT("ReleaseClaim") : TEXT("RecordClaim");
    FInstancedPropertyBag Params = Object->MakeParams(Function);
    if (Params.SetValueInt64(TEXT("GridId"), bReleasing ? ReleaseReply.GridId : ClaimReply.GridId) != EPropertyBagResult::Success)
    {
        FinishAction(false, TEXT("The hub definition has no valid grid input."));
        return;
    }
    if (!bReleasing)
    {
        FConstructHubChunk Chunk;
        Chunk.X = ClaimReply.ChunkX;
        Chunk.Y = ClaimReply.ChunkY;
        Chunk.Z = ClaimReply.ChunkZ;
        if (Params.SetValueStruct(TEXT("Chunk"), Chunk) != EPropertyBagResult::Success)
        {
            FinishAction(false, TEXT("The hub definition has no valid chunk input."));
            return;
        }
    }
    const uint64 Request = ActionSerial;
    const TWeakObjectPtr<AConstructClaimPad> WeakThis(this);
    Object->Call(Function, Params, [WeakThis, Request](const FCrowdyServerCallResult& Reply)
    {
        if (AConstructClaimPad* Self = WeakThis.Get(); Self && !Self->bEnding && Self->bBusy && Self->ActionSerial == Request)
        {
            Self->HubReturned(Reply);
        }
    });
}

void AConstructClaimPad::HubReturned(const FCrowdyServerCallResult& Reply)
{
    GetWorldTimerManager().ClearTimer(HubTimer);
    const bool bCanRetry = (Reply.Outcome == ECrowdyServerCallOutcome::Denied && Reply.bRetryable)
        || (Reply.Outcome == ECrowdyServerCallOutcome::ServerError && Reply.Reason == TEXT("the claim could not be checked"));
    if (!Reply.IsSuccess() && !bReleasing && bCanRetry && Retries < 2)
    {
        ++Retries;
        GetWorldTimerManager().SetTimer(RetryTimer, this, &ThisClass::CallHub, 2.0f);
        return;
    }
    FinishAction(Reply.IsSuccess(), Reply.Reason);
}

void AConstructClaimPad::HubTimedOut()
{
    FinishAction(false, TEXT("The hub did not answer."));
}

void AConstructClaimPad::ReleaseHub()
{
    GetWorldTimerManager().ClearTimer(RetryTimer);
    GetWorldTimerManager().ClearTimer(HubTimer);
    if (UCrowdyServerObject* Object = Hub.Get())
    {
        Object->OnStatusChanged.Remove(HubStatusHandle);
        UCrowdyServerObjectSubsystem* Objects = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyServerObjectSubsystem>() : nullptr;
        if (Objects)
        {
            Objects->Release(Object, this);
        }
    }
    Hub.Reset();
    HubStatusHandle.Reset();
}

void AConstructClaimPad::FinishAction(bool bHubSucceeded, const FString& Message, bool bRefresh)
{
    if (!bBusy)
    {
        return;
    }
    ReleaseHub();
    const bool bWasRelease = bReleasing;
    const ECrowdyClaimResult Result = bWasRelease ? ReleaseReply.Result : ClaimReply.Result;
    const int64 GridId = bWasRelease ? ReleaseReply.GridId : ClaimReply.GridId;
    const bool bIndexed = Result == ECrowdyClaimResult::Success && bHubSucceeded;
    FString ReportMessage = Message;
    if (ReportMessage.IsEmpty())
    {
        ReportMessage = bWasRelease ? ReleaseReply.Message : ClaimReply.Message;
    }
    bBusy = false;
    UpdatePresentation();
    if (Result != ECrowdyClaimResult::Success || !bIndexed)
    {
        FString Feedback = ReportMessage;
        if (!bWasRelease && Result == ECrowdyClaimResult::Success)
        {
            Feedback = TEXT("Claimed; not listed in the hub yet");
        }
        else if (Result == ECrowdyClaimResult::AlreadyClaimed || Result == ECrowdyClaimResult::NotClaimable)
        {
            Feedback = TEXT("Someone has claimed this chunk");
        }
        ShowFeedback(Feedback);
    }
    if (bWasRelease)
    {
        OnReleaseDone.Broadcast(Result, GridId, bIndexed, ReportMessage);
    }
    else
    {
        OnClaimDone.Broadcast(Result, GridId, bIndexed, ReportMessage);
    }
    if (!bEnding && bRefresh)
    {
        Refresh();
    }
}

void AConstructClaimPad::EnterPad(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    APawn* Pawn = Cast<APawn>(Other);
    APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (!Controller || !Controller->IsLocalController() || Controller->GetPawn() != Pawn)
    {
        return;
    }
    ReleaseInput();
    OfferController = Controller;
    OfferInput = NewObject<UInputComponent>(this);
    OfferInput->Priority = 40;
    OfferInput->RegisterComponent();
    OfferInput->BindKey(EKeys::E, IE_Pressed, this, &ThisClass::AcceptOffer);
    Controller->PushInputComponent(OfferInput);
    Refresh();
}

void AConstructClaimPad::LeavePad(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
    if (OfferController.IsValid() && OfferController->GetPawn() == Other && !Trigger->IsOverlappingActor(Other))
    {
        ReleaseInput();
    }
}

void AConstructClaimPad::ReleaseInput()
{
    if (APlayerController* Controller = OfferController.Get(); Controller && OfferInput)
    {
        Controller->PopInputComponent(OfferInput);
    }
    if (OfferInput)
    {
        OfferInput->DestroyComponent();
    }
    OfferInput = nullptr;
    OfferController.Reset();
}

void AConstructClaimPad::AcceptOffer()
{
    APlayerController* Controller = OfferController.Get();
    ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    UConstructUIInputSubsystem* Input = Player ? Player->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
    if (!Input || !Input->IsGameplayInputAllowed() || !Controller->GetPawn() || !Trigger->IsOverlappingActor(Controller->GetPawn()))
    {
        return;
    }
    if (CanClaim())
    {
        Claim();
    }
    else if (CanRelease())
    {
        Release();
    }
}

void AConstructClaimPad::ShowFeedback(const FString& Message)
{
    HideFeedback();
    UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
    if (!Viewport)
    {
        return;
    }
    FeedbackWidget = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(20, 20, 20, 90)
        [
            SNew(SBorder).Padding(FMargin(14, 8)).BorderBackgroundColor(FLinearColor(0.1f, 0.02f, 0.01f))
            [
                SNew(STextBlock).Text(FText::FromString(Message.IsEmpty() ? TEXT("The request could not complete.") : Message))
            ]
        ];
    FeedbackViewport = Viewport;
    Viewport->AddViewportWidgetContent(FeedbackWidget.ToSharedRef(), 25);
    GetWorldTimerManager().SetTimer(FeedbackTimer, this, &ThisClass::HideFeedback, 4.0f);
}

void AConstructClaimPad::HideFeedback()
{
    if (UGameViewportClient* Viewport = FeedbackViewport.Get(); Viewport && FeedbackWidget.IsValid())
    {
        Viewport->RemoveViewportWidgetContent(FeedbackWidget.ToSharedRef());
    }
    FeedbackWidget.Reset();
    FeedbackViewport.Reset();
}

UConstructModPanel* AConstructClaimPad::OpenMods()
{
    UConstructModsSubsystem* Menus = GetWorld() ? GetWorld()->GetSubsystem<UConstructModsSubsystem>() : nullptr;
    return Menus ? Menus->OpenForPad(*this) : nullptr;
}
