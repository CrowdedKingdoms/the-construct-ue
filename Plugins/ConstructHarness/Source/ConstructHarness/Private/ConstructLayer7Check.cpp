#include "ConstructLayer7Check.h"

#include "ConstructClaimPad.h"
#include "ConstructHarnessLog.h"
#include "ConstructHubTypes.h"
#include "CrowdyServerObject.h"
#include "CrowdyServerObjectDefinition.h"
#include "CrowdyServerObjectSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
	const TCHAR* const Layer7MapName = TEXT("L_Layer7Check");
	constexpr double Layer7PollSeconds = 0.05;
	constexpr double Layer7TimeoutSeconds = 20.0 * 60.0;
	// A claim or release covers the platform call, the hub call with its retries, and the refresh after.
	constexpr double Layer7ActionSeconds = 60.0;
	// How long a look waits for the name read after the owner is shown; the judge fails a missing name line.
	constexpr double Layer7NameSeconds = 15.0;

	// The chunk reserved for this check (Internal/Contracts.md C7 item 8).
	const FInt64Vector Layer7Chunk(37, -64, 0);

	FString Layer7Line(const TSharedRef<FJsonObject>& Object)
	{
		FString Line;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		FJsonSerializer::Serialize(Object, Writer);
		return Line;
	}

	FString Layer7ClaimResultName(const ECrowdyClaimResult Result)
	{
		return StaticEnum<ECrowdyClaimResult>()->GetNameStringByValue(static_cast<int64>(Result));
	}
}

EConstructLayer7ClaimPath UConstructLayer7CheckSubsystem::ChooseClaimPath(const bool bKnown, const int64 GridId, const bool bOwnedByMe)
{
	if (!bKnown || bOwnedByMe)
	{
		return EConstructLayer7ClaimPath::None;
	}
	return GridId == 0 ? EConstructLayer7ClaimPath::Pad : EConstructLayer7ClaimPath::Direct;
}

TArray<FString> UConstructLayer7CheckSubsystem::CompleteLines(const FString& Text, const int32 FirstNew)
{
	// Node replaces "wait" with the first instruction, so "wait" takes no index: the first instruction is index 0.
	TArray<FString> Pieces;
	Text.ParseIntoArray(Pieces, TEXT("\n"), false);
	TArray<FString> Lines;
	for (int32 Index = FMath::Max(FirstNew, 0); Index < Pieces.Num() - 1; ++Index)
	{
		const FString Line = Pieces[Index].TrimStartAndEnd();
		if (Line == TEXT("wait"))
		{
			break;
		}
		Lines.Add(Line);
	}
	return Lines;
}

FString UConstructLayer7CheckSubsystem::ShownJson(const int64 GridId, const int64 OwnerUserId, const bool bOwnedByMe)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("event"), TEXT("shown"));
	Object->SetStringField(TEXT("grid"), LexToString(GridId));
	Object->SetStringField(TEXT("owner"), LexToString(OwnerUserId));
	Object->SetBoolField(TEXT("mine"), bOwnedByMe);
	return Layer7Line(Object);
}

FString UConstructLayer7CheckSubsystem::NamedJson(const int64 OwnerUserId, const FString& Name)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("event"), TEXT("named"));
	Object->SetStringField(TEXT("owner"), LexToString(OwnerUserId));
	Object->SetStringField(TEXT("name"), Name);
	return Layer7Line(Object);
}

FString UConstructLayer7CheckSubsystem::RecordJson(const int64 GridId, const ECrowdyServerCallOutcome Outcome, const FString& Reason)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("event"), TEXT("record"));
	Object->SetStringField(TEXT("grid"), LexToString(GridId));
	Object->SetStringField(TEXT("outcome"), StaticEnum<ECrowdyServerCallOutcome>()->GetNameStringByValue(static_cast<int64>(Outcome)));
	Object->SetStringField(TEXT("reason"), Reason);
	return Layer7Line(Object);
}

bool UConstructLayer7CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* OuterWorld = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && OuterWorld && OuterWorld->WorldType == EWorldType::Game;
}

void UConstructLayer7CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetGameInstance())
	{
		return;
	}
	const bool bAsked = FParse::Value(FCommandLine::Get(), TEXT("Layer7CheckControl="), ControlPath);
	if (InWorld.GetMapName() != Layer7MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer7Check.
		if (bAsked)
		{
			Fail(FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer7MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);
	// Tools/Layer7Check writes both files before launch, so a path cut short at a space fails here, not at the first line.
	if (!bAsked || ControlPath.IsEmpty() || !FParse::Value(FCommandLine::Get(), TEXT("Layer7CheckResults="), ResultsPath)
		|| !FPaths::FileExists(ControlPath) || !FPaths::FileExists(ResultsPath))
	{
		Fail(TEXT("-Layer7CheckControl and -Layer7CheckResults are needed; start the check with Tools/Layer7Check"));
		return;
	}
	for (TActorIterator<AConstructClaimPad> It(&InWorld); It; ++It)
	{
		Pad = *It;
		break;
	}
	AConstructClaimPad* ClaimPad = Pad.Get();
	if (!ClaimPad)
	{
		Fail(TEXT("the map needs a placed AConstructClaimPad"));
		return;
	}
	Chunk = ClaimPad->GetClaimChunk();
	if (Chunk != Layer7Chunk)
	{
		Fail(FString::Printf(TEXT("the pad stands in chunk %lld %lld %lld, not the check's chunk"), Chunk.X, Chunk.Y, Chunk.Z));
		return;
	}
	ClaimPad->OnOwnerShown.AddUniqueDynamic(this, &ThisClass::HandleOwnerShown);
	ClaimPad->OnOwnerNamed.AddUniqueDynamic(this, &ThisClass::HandleOwnerNamed);
	ClaimPad->OnClaimDone.AddUniqueDynamic(this, &ThisClass::HandleClaimDone);
	ClaimPad->OnReleaseDone.AddUniqueDynamic(this, &ThisClass::HandleReleaseDone);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: started"));
}

void UConstructLayer7CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0. Node ends a run by
	// stopping the process, which never gets here.
	if (TickerHandle.IsValid())
	{
		Fail(TEXT("the check's world ended before Node stopped the client"));
	}
	ReleaseHub();
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer7CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Failed)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > Layer7TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out in stage %d, step %d"), static_cast<int32>(Stage), static_cast<int32>(Step)));
		return false;
	}
	if (Stage == EStage::WaitEntry)
	{
		return TickEntry();
	}
	if (Step == EStep::AwaitNamed && Now > StepDeadline)
	{
		Finish();
		return true;
	}
	if (Step != EStep::Idle && Now > StepDeadline)
	{
		Fail(FString::Printf(TEXT("step %d had no answer within its time"), static_cast<int32>(Step)));
		return false;
	}
	if (Step == EStep::Recording && !bRecordCalled)
	{
		CallRecord();
		return true;
	}
	if (Step == EStep::Idle && Now - LastPollAt >= Layer7PollSeconds)
	{
		LastPollAt = Now;
		PollControl();
	}
	return true;
}

bool UConstructLayer7CheckSubsystem::TickEntry()
{
	UWorld* CurrentWorld = GetWorld();
	UGameInstance* Game = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = Game ? Game->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	const AConstructClaimPad* ClaimPad = Pad.Get();
	if (!Auth || !SDK || !Session || !ClaimPad)
	{
		Fail(TEXT("the Crowdy SDK subsystems or the claim pad are missing"));
		return false;
	}
	int64 GridId = 0;
	int64 OwnerUserId = 0;
	bool bOwnedByMe = false;
	FString OwnerName;
	if (!Auth->IsSignedIn() || SDK->GetUDPConnectionState() != EUDPConnectionState::Connected || Session->GetUserID() == 0
		|| !HasVisiblePeer() || !ClaimPad->GetShownOwner(GridId, OwnerUserId, bOwnedByMe, OwnerName))
	{
		return true;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: stimulus landed: signed in, connected, pad shown"));
	Stage = EStage::Running;
	return true;
}

bool UConstructLayer7CheckSubsystem::HasVisiblePeer() const
{
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const UCrowdyEntityComponent* Component = It->FindComponentByClass<UCrowdyEntityComponent>();
		if (Component && Component->GetRole() == ECrowdyRole::RemoteProxy && !It->IsHidden())
		{
			return true;
		}
	}
	return false;
}

void UConstructLayer7CheckSubsystem::PollControl()
{
	// Allows a write, or Node's write of this file could land inside a read and fail on the sharing violation.
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ControlPath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite))
	{
		return;
	}
	const TArray<FString> Lines = CompleteLines(Text, LinesDone);
	if (Lines.Num() == 0)
	{
		return;
	}
	++LinesDone;
	Act(Lines[0]);
}

void UConstructLayer7CheckSubsystem::Act(const FString& Line)
{
	AConstructClaimPad* ClaimPad = Pad.Get();
	if (!ClaimPad)
	{
		Fail(TEXT("the claim pad went away"));
		return;
	}
	if (Line == TEXT("claim"))
	{
		StartClaim();
		return;
	}
	if (Line == TEXT("look"))
	{
		Begin(EStep::Looking, Layer7ActionSeconds);
		ClaimPad->Refresh();
		return;
	}
	if (Line != TEXT("release"))
	{
		Fail(FString::Printf(TEXT("the control line \"%s\" is not one this check reads"), *Line.Left(80)));
		return;
	}
	int64 GridId = 0;
	int64 OwnerUserId = 0;
	bool bOwnedByMe = false;
	FString OwnerName;
	if (!ClaimPad->GetShownOwner(GridId, OwnerUserId, bOwnedByMe, OwnerName) || GridId == 0 || !bOwnedByMe)
	{
		Fail(TEXT("release: the pad does not show this player's claim"));
		return;
	}
	Begin(EStep::Releasing, Layer7ActionSeconds);
	ClaimPad->Release();
}

void UConstructLayer7CheckSubsystem::StartClaim()
{
	AConstructClaimPad* ClaimPad = Pad.Get();
	int64 GridId = 0;
	int64 OwnerUserId = 0;
	bool bOwnedByMe = false;
	FString OwnerName;
	const bool bKnown = ClaimPad->GetShownOwner(GridId, OwnerUserId, bOwnedByMe, OwnerName);
	const EConstructLayer7ClaimPath Path = ChooseClaimPath(bKnown, GridId, bOwnedByMe);
	if (Path == EConstructLayer7ClaimPath::Pad)
	{
		Begin(EStep::Claiming, Layer7ActionSeconds);
		ClaimPad->Claim();
		return;
	}
	UCrowdyClaims* Claims = GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyClaims>();
	if (Path == EConstructLayer7ClaimPath::None || !Claims)
	{
		Fail(TEXT("claim: the pad shows no chunk this player could claim"));
		return;
	}
	TargetGrid = GridId;
	Begin(EStep::SdkClaiming, Layer7ActionSeconds);
	const uint64 Serial = StepSerial;
	TWeakObjectPtr<UConstructLayer7CheckSubsystem> WeakThis(this);
	Claims->ClaimChunk(Chunk.X, Chunk.Y, Chunk.Z, [WeakThis, Serial](const FCrowdyChunkClaim& Reply)
	{
		UConstructLayer7CheckSubsystem* Self = WeakThis.Get();
		if (Self && Self->StepSerial == Serial && Self->Step == EStep::SdkClaiming)
		{
			Self->HandleSdkClaim(Reply);
		}
	});
}

void UConstructLayer7CheckSubsystem::HandleSdkClaim(const FCrowdyChunkClaim& Reply)
{
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: sdk-claim %s"), *Layer7ClaimResultName(Reply.Result));
	UCrowdyServerObjectSubsystem* Objects = GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyServerObjectSubsystem>();
	// Loaded here, never in a constructor: see UConstructHubSubsystem. Config/DefaultGame.ini always cooks the folder.
	ClaimsDefinition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructClaims.CSO_ConstructClaims"));
	FString Error;
	Hub = Objects && ClaimsDefinition ? Objects->Acquire(ClaimsDefinition, TEXT("main"), this, Error) : nullptr;
	if (!Hub)
	{
		Fail(FString::Printf(TEXT("the claims hub could not be acquired: %s"), *Error));
		return;
	}
	bRecordCalled = false;
	Begin(EStep::Recording, Layer7ActionSeconds);
}

void UConstructLayer7CheckSubsystem::CallRecord()
{
	if (!Hub)
	{
		Fail(TEXT("the claims hub went away before the record"));
		return;
	}
	const ECrowdyServerObjectStatus Status = Hub->GetStatus();
	if (Status == ECrowdyServerObjectStatus::Failed)
	{
		Fail(FString::Printf(TEXT("the claims hub failed: %s"), *Hub->GetFailureReason()));
		return;
	}
	if (Status != ECrowdyServerObjectStatus::Ready)
	{
		return;
	}
	bRecordCalled = true;
	FInstancedPropertyBag Params = Hub->MakeParams(TEXT("RecordClaim"));
	FConstructHubChunk HubChunk;
	HubChunk.X = Chunk.X;
	HubChunk.Y = Chunk.Y;
	HubChunk.Z = Chunk.Z;
	Params.SetValueInt64(TEXT("GridId"), TargetGrid);
	Params.SetValueStruct(TEXT("Chunk"), HubChunk);
	const uint64 Serial = StepSerial;
	TWeakObjectPtr<UConstructLayer7CheckSubsystem> WeakThis(this);
	Hub->Call(TEXT("RecordClaim"), Params, [WeakThis, Serial](const FCrowdyServerCallResult& Result)
	{
		UConstructLayer7CheckSubsystem* Self = WeakThis.Get();
		if (Self && Self->StepSerial == Serial && Self->Step == EStep::Recording)
		{
			Self->HandleRecord(Result);
		}
	});
}

void UConstructLayer7CheckSubsystem::HandleRecord(const FCrowdyServerCallResult& Result)
{
	const FString OutcomeName = StaticEnum<ECrowdyServerCallOutcome>()->GetNameStringByValue(static_cast<int64>(Result.Outcome));
	WriteResult(RecordJson(TargetGrid, Result.Outcome, Result.Reason));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: record %s"), *OutcomeName);
	ReleaseHub();
	Finish();
}

void UConstructLayer7CheckSubsystem::ReleaseHub()
{
	UCrowdyServerObject* Object = Hub;
	Hub = nullptr;
	UWorld* CurrentWorld = GetWorld();
	UGameInstance* Game = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	UCrowdyServerObjectSubsystem* Objects = Game ? Game->GetSubsystem<UCrowdyServerObjectSubsystem>() : nullptr;
	if (Object && Objects)
	{
		Objects->Release(Object, this);
	}
}

void UConstructLayer7CheckSubsystem::HandleOwnerShown(const int64 GridId, const int64 OwnerUserId, const bool bOwnedByMe)
{
	if (Stage != EStage::Running)
	{
		return;
	}
	WriteResult(ShownJson(GridId, OwnerUserId, bOwnedByMe));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: shown grid %lld mine %d"), GridId, bOwnedByMe ? 1 : 0);
	if (Step == EStep::AwaitShown)
	{
		Finish();
		return;
	}
	if (Step != EStep::Looking)
	{
		return;
	}
	if (OwnerUserId == 0)
	{
		Finish();
		return;
	}
	Begin(EStep::AwaitNamed, Layer7NameSeconds);
}

void UConstructLayer7CheckSubsystem::HandleOwnerNamed(const int64 OwnerUserId, const FString& Name)
{
	if (Stage != EStage::Running)
	{
		return;
	}
	WriteResult(NamedJson(OwnerUserId, Name));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: named"));
	if (Step == EStep::AwaitNamed)
	{
		Finish();
	}
}

void UConstructLayer7CheckSubsystem::HandleClaimDone(const ECrowdyClaimResult Result, const int64 GridId, const bool bListed, const FString& Message)
{
	if (Stage != EStage::Running || Step != EStep::Claiming)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: claim %s grid %lld listed %d"), *Layer7ClaimResultName(Result), GridId, bListed ? 1 : 0);
	// The pad asks for a refresh after every claim; its next shown line ends the instruction.
	Begin(EStep::AwaitShown, Layer7ActionSeconds);
}

void UConstructLayer7CheckSubsystem::HandleReleaseDone(const ECrowdyClaimResult Result, const int64 GridId, const bool bUnlisted, const FString& Message)
{
	if (Stage != EStage::Running || Step != EStep::Releasing)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: release %s unlisted %d"), *Layer7ClaimResultName(Result), bUnlisted ? 1 : 0);
	Begin(EStep::AwaitShown, Layer7ActionSeconds);
}

void UConstructLayer7CheckSubsystem::Begin(const EStep Next, const double Seconds)
{
	Step = Next;
	++StepSerial;
	StepDeadline = FPlatformTime::Seconds() + Seconds;
}

void UConstructLayer7CheckSubsystem::Finish()
{
	Step = EStep::Idle;
	++StepSerial;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer7Check: done"));
}

void UConstructLayer7CheckSubsystem::WriteResult(const FString& Line)
{
	if (!FFileHelper::SaveStringToFile(Line + TEXT("\n"), *ResultsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
	{
		Fail(TEXT("the results file could not be written"));
	}
}

void UConstructLayer7CheckSubsystem::Fail(const FString& Why)
{
	if (Stage == EStage::Failed)
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer7Check: FAIL: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
