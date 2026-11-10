#include "ConstructProgramCatalogSubsystem.h"

#include "ConstructProgramLevels.h"
#include "ConstructProgramPad.h"
#include "CrowdyExecCodec.h"
#include "CrowdyServerObject.h"
#include "CrowdyServerObjectDefinition.h"
#include "CrowdyServerObjectSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "StructUtils/PropertyBag.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

const TSoftObjectPtr<UWorld>& UConstructProgramCatalogSubsystem::GetHolodeck()
{
    static const TSoftObjectPtr<UWorld> Holodeck{FSoftObjectPath(ConstructProgramLevels::Holodeck)};
    return Holodeck;
}

bool UConstructProgramCatalogSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UConstructProgramCatalogSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    UGameInstance* Instance = InWorld.GetGameInstance();
    if (!Instance || InWorld.GetNetMode() == NM_DedicatedServer
        || UWorld::RemovePIEPrefix(InWorld.GetPackage()->GetName()) != GetHolodeck().ToSoftObjectPath().GetLongPackageName())
    {
        return;
    }

    Game = Instance;
    if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
    {
        SDK->OnLogin.AddUniqueDynamic(this, &ThisClass::SignedIn);
        SDK->OnRegister.AddUniqueDynamic(this, &ThisClass::SignedIn);
        SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::SignedOut);
    }
    if (UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>())
    {
        Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
    }
    StartCatalog();
}

void UConstructProgramCatalogSubsystem::Deinitialize()
{
    bFinished = true;
    ReleaseObject();
    if (UGameInstance* Instance = Game.Get())
    {
        if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
        {
            SDK->OnLogin.RemoveDynamic(this, &ThisClass::SignedIn);
            SDK->OnRegister.RemoveDynamic(this, &ThisClass::SignedIn);
            SDK->OnLogout.RemoveDynamic(this, &ThisClass::SignedOut);
        }
        if (UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>())
        {
            Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
        }
    }
    Super::Deinitialize();
}

void UConstructProgramCatalogSubsystem::SignedIn(bool bSuccess, FString)
{
    if (bSuccess)
    {
        StartCatalog();
    }
}

void UConstructProgramCatalogSubsystem::SignedOut(bool bSuccess)
{
    if (bSuccess)
    {
        InvalidateSession();
    }
}

void UConstructProgramCatalogSubsystem::AccessEnded(const FString&, const FString&)
{
    InvalidateSession();
}

void UConstructProgramCatalogSubsystem::InvalidateSession()
{
    bSessionActive = false;
    // A call already sent for this visit stays terminal even if the session ended before the answer.
    bFinished |= bCalled;
    ReleaseObject();
    RefreshPads();
}

void UConstructProgramCatalogSubsystem::StartCatalog()
{
    UGameInstance* Instance = Game.Get();
    if (!Instance)
    {
        return;
    }

    const UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>();
    if (!Session || Session->HasAccessEnded() || Session->GetUserID() <= 0)
    {
        return;
    }

    bSessionActive = true;
    if (bFinished)
    {
        RefreshPads();
        return;
    }
    if (CatalogObject.IsValid())
    {
        return;
    }

    UCrowdyServerObjectSubsystem* Objects = Instance->GetSubsystem<UCrowdyServerObjectSubsystem>();
    auto* Definition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructWorld.CSO_ConstructWorld"));
    FString Error;
    UCrowdyServerObject* Object = Objects && Definition ? Objects->Acquire(Definition, TEXT("main"), this, Error) : nullptr;
    if (!Object)
    {
        FinishCatalog({});
        return;
    }

    CatalogObject = Object;
    StatusHandle = Object->OnStatusChanged.AddUObject(this, &ThisClass::StatusChanged);
    StatusChanged(Object, Object->GetStatus());
}

void UConstructProgramCatalogSubsystem::StatusChanged(UCrowdyServerObject* Object, ECrowdyServerObjectStatus Status)
{
    if (bFinished || bCalled || Object != CatalogObject.Get())
    {
        return;
    }
    if (Status == ECrowdyServerObjectStatus::Failed)
    {
        FinishCatalog({});
    }
    else if (Status == ECrowdyServerObjectStatus::Ready)
    {
        bCalled = true;
        const FName Function(TEXT("ListPrograms"));
        const TWeakObjectPtr<UConstructProgramCatalogSubsystem> WeakThis(this);
        Object->Call(Function, Object->MakeParams(Function), [WeakThis](const FCrowdyServerCallResult& Result)
        {
            if (UConstructProgramCatalogSubsystem* Self = WeakThis.Get(); Self && !Self->bFinished)
            {
                Self->CatalogReturned(Result);
            }
        });
    }
}

void UConstructProgramCatalogSubsystem::CatalogReturned(const FCrowdyServerCallResult& Result)
{
    if (!Result.IsSuccess())
    {
        FinishCatalog({});
        return;
    }

    const FInstancedPropertyBag Reply = CrowdyExec::ToList(Result.Reply);
    const auto Programs = Reply.GetArrayRef(TEXT("Programs"));
    if (!Programs.HasValue())
    {
        FinishCatalog({});
        return;
    }

    TArray<FConstructHubProgram> Rows;
    const auto& Items = Programs.GetValue();
    Rows.Reserve(Items.Num());
    for (int32 Index = 0; Index < Items.Num(); ++Index)
    {
        const auto Program = Items.GetValueStruct<FConstructHubProgram>(Index);
        if (!Program.HasValue() || !Program.GetValue())
        {
            FinishCatalog({});
            return;
        }
        Rows.Add(*Program.GetValue());
    }
    FinishCatalog(MoveTemp(Rows));
}

void UConstructProgramCatalogSubsystem::FinishCatalog(TOptional<TArray<FConstructHubProgram>> NewCatalog)
{
    bFinished = true;
    Catalog = MoveTemp(NewCatalog);
    ReleaseObject();
    RefreshPads();
}

void UConstructProgramCatalogSubsystem::RefreshPads() const
{
    for (TActorIterator<AConstructProgramPad> It(GetWorld()); It; ++It)
    {
        ApplyToPad(**It);
    }
}

void UConstructProgramCatalogSubsystem::ApplyToPad(AConstructProgramPad& Pad) const
{
    if (bSessionActive && Catalog.IsSet())
    {
        Pad.ApplyCatalog(Catalog.GetValue(), true);
    }
    else
    {
        Pad.ApplyCatalog({}, false);
    }
}

void UConstructProgramCatalogSubsystem::ReleaseObject()
{
    if (UCrowdyServerObject* Object = CatalogObject.Get())
    {
        Object->OnStatusChanged.Remove(StatusHandle);
        if (UGameInstance* Instance = Game.Get())
        {
            if (UCrowdyServerObjectSubsystem* Objects = Instance->GetSubsystem<UCrowdyServerObjectSubsystem>())
            {
                Objects->Release(Object, this);
            }
        }
    }
    CatalogObject.Reset();
    StatusHandle.Reset();
}
