#include "ConstructLayer4Check.h"

#include "ConstructHarnessLog.h"
#include "CrowdyExecCodec.h"
#include "CrowdyServerObject.h"
#include "CrowdyServerObjectDefinition.h"
#include "CrowdyServerObjectSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
	const TCHAR* const Layer4MapName = TEXT("L_Layer4Check");
	constexpr double Layer4PollSeconds = 0.05;
	// Each player waits 1 s between two successful calls of one claim function, whichever path makes them.
	constexpr double Layer4OpSpacingSeconds = 1.2;
	constexpr double Layer4AnswerSeconds = 30.0;
	constexpr double Layer4TimeoutSeconds = 20.0 * 60.0;

	int64 ReadInt64(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Text;
		int64 Value = 0;
		return Object->TryGetStringField(Field, Text) ? (LexTryParseString(Value, *Text) ? Value : 0) : 0;
	}

	// A refused reply is an empty bag, so a missing field reads as its default.
	int64 BagInt64(const FInstancedPropertyBag& Bag, const FName Name)
	{
		const auto Value = Bag.GetValueInt64(Name);
		return Value.HasValue() ? Value.GetValue() : 0;
	}

	int32 BagInt32(const FInstancedPropertyBag& Bag, const FName Name)
	{
		const auto Value = Bag.GetValueInt32(Name);
		return Value.HasValue() ? Value.GetValue() : 0;
	}

	bool BagBool(const FInstancedPropertyBag& Bag, const FName Name)
	{
		const auto Value = Bag.GetValueBool(Name);
		return Value.HasValue() && Value.GetValue();
	}
}

bool UConstructLayer4CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* OuterWorld = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && OuterWorld && OuterWorld->WorldType == EWorldType::Game;
}

void UConstructLayer4CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetGameInstance())
	{
		return;
	}
	const bool bAsked = FParse::Value(FCommandLine::Get(), TEXT("Layer4CheckControl="), ControlPath);
	if (InWorld.GetMapName() != Layer4MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer4Check.
		if (bAsked)
		{
			Fail(FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer4MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);
	if (!bAsked || ControlPath.IsEmpty()
		|| !FParse::Value(FCommandLine::Get(), TEXT("Layer4CheckResults="), ResultsPath) || ResultsPath.IsEmpty()
		|| !FParse::Value(FCommandLine::Get(), TEXT("Layer4CheckPlan="), PlanPath) || PlanPath.IsEmpty())
	{
		Fail(TEXT("-Layer4CheckControl, -Layer4CheckResults and -Layer4CheckPlan are needed; start the check with Tools/Layer4Check"));
		return;
	}
	// Tools/Layer4Check writes these before launch, so a path cut short at a space fails here, not at go.
	if (!FPaths::FileExists(ControlPath) || !FPaths::FileExists(ResultsPath) || !LoadPlan())
	{
		Fail(TEXT("the control, results or plan file could not be read"));
		return;
	}

	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	NativeReporter = InWorld.SpawnActor<AConstructLayer4CheckProbe>(AConstructLayer4CheckProbe::StaticClass(), Spawn);
	if (!NativeReporter)
	{
		Fail(TEXT("the C++ path's reporter could not be spawned"));
		return;
	}
	NativeReporter->OnAnswer.AddUObject(this, &ThisClass::HandleAnswer);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer4Check: started, %d steps"), Steps.Num());
}

void UConstructLayer4CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0.
	if (TickerHandle.IsValid() && Stage != EStage::Done)
	{
		Fail(TEXT("the check's world ended before it finished"));
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer4CheckSubsystem::LoadPlan()
{
	FString Text;
	TSharedPtr<FJsonObject> Plan;
	if (!FFileHelper::LoadFileToString(Text, *PlanPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan) || !Plan.IsValid())
	{
		return false;
	}
	const auto ReadOps = [](const TSharedPtr<FJsonObject>& Step, const TCHAR* Path, TArray<FCheckOp>& OutOps)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Step->TryGetArrayField(Path, Items))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Item : *Items)
		{
			const TSharedPtr<FJsonObject> Fields = Item->AsObject();
			FCheckOp Op;
			Op.Id = Fields->GetStringField(TEXT("id"));
			Op.Kind = Fields->GetStringField(TEXT("kind"));
			Op.GridId = ReadInt64(Fields, TEXT("grid"));
			const TSharedPtr<FJsonObject>* Chunk = nullptr;
			if (Fields->TryGetObjectField(TEXT("chunk"), Chunk))
			{
				Op.Chunk.X = ReadInt64(*Chunk, TEXT("X"));
				Op.Chunk.Y = ReadInt64(*Chunk, TEXT("Y"));
				Op.Chunk.Z = ReadInt64(*Chunk, TEXT("Z"));
			}
			Fields->TryGetBoolField(TEXT("onlyChunk"), Op.bOnlyChunk);
			Fields->TryGetBoolField(TEXT("onlyMine"), Op.bOnlyMine);
			Fields->TryGetNumberField(TEXT("limit"), Op.Limit);
			Fields->TryGetStringField(TEXT("instance"), Op.InstanceId);
			Fields->TryGetBoolField(TEXT("quick"), Op.bQuick);
			OutOps.Add(MoveTemp(Op));
		}
	};
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Plan->TryGetArrayField(TEXT("steps"), Items))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Item : *Items)
	{
		const TSharedPtr<FJsonObject> Fields = Item->AsObject();
		FCheckStep Step;
		Fields->TryGetNumberField(TEXT("step"), Step.Number);
		ReadOps(Fields, TEXT("cpp"), Step.Native);
		ReadOps(Fields, TEXT("bp"), Step.Blueprint);
		Steps.Add(MoveTemp(Step));
	}
	return Steps.Num() > 0;
}

bool UConstructLayer4CheckSubsystem::ReadControl(const FString& Expected) const
{
	// Allows a write, or Node's next write of this file could land inside a read and fail on the sharing violation.
	FString Text;
	return FFileHelper::LoadFileToString(Text, *ControlPath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite) && Text.TrimStartAndEnd() == Expected;
}

bool UConstructLayer4CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > Layer4TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out in stage %d"), static_cast<int32>(Stage)));
		return false;
	}
	if (Stage == EStage::WaitEntry || Stage == EStage::WaitHub)
	{
		return TickEntry();
	}
	if (Stage == EStage::WaitAnswer)
	{
		if (Now > AnswerDeadline)
		{
			Fail(FString::Printf(TEXT("no answer within %.0f s on step %d"), Layer4AnswerSeconds, Steps[StepIndex].Number));
			return false;
		}
		if (ReadingProgress)
		{
			FinishProgressRead();
		}
		return true;
	}
	if (Stage == EStage::RunStep)
	{
		TickStep(Now);
		return true;
	}
	if (Now - LastPollAt < Layer4PollSeconds)
	{
		return true;
	}
	LastPollAt = Now;
	if (Stage == EStage::WaitGo && ReadControl(TEXT("go")))
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer4Check: go"));
		Stage = EStage::RunStep;
		NextOpAt = Now;
		return true;
	}
	if (Stage == EStage::WaitNode && ReadControl(FString::Printf(TEXT("step %d done"), Steps[StepIndex].Number)))
	{
		++StepIndex;
		bBlueprintTurn = false;
		OpIndex = 0;
		if (!Steps.IsValidIndex(StepIndex))
		{
			Stage = EStage::Done;
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer4Check: done"));
			return false;
		}
		Stage = EStage::RunStep;
		NextOpAt = Now + Layer4OpSpacingSeconds;
	}
	return true;
}

bool UConstructLayer4CheckSubsystem::TickEntry()
{
	UWorld* CurrentWorld = GetWorld();
	UGameInstance* Game = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = Game ? Game->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	UCrowdyServerObjectSubsystem* Objects = Game ? Game->GetSubsystem<UCrowdyServerObjectSubsystem>() : nullptr;
	if (!Auth || !SDK || !Session || !Objects)
	{
		Fail(TEXT("the Crowdy SDK subsystems are missing"));
		return false;
	}

	if (Stage == EStage::WaitEntry)
	{
		if (!Auth->IsSignedIn() || SDK->GetUDPConnectionState() != EUDPConnectionState::Connected || Session->GetUserID() == 0)
		{
			return true;
		}
		SelfId = LexToString(Session->GetUserID());
		// Loaded here, never in a constructor: see UConstructHubSubsystem. Config/DefaultGame.ini always cooks the folder.
		WorldDefinition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructWorld.CSO_ConstructWorld"));
		ClaimsDefinition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructClaims.CSO_ConstructClaims"));
		ProgressDefinition = LoadObject<UCrowdyServerObjectDefinition>(nullptr, TEXT("/Game/ServerObjects/CSO_ConstructProgress.CSO_ConstructProgress"));
		FString Error;
		World = Objects->Acquire(WorldDefinition, TEXT("main"), this, Error);
		Claims = Objects->Acquire(ClaimsDefinition, TEXT("main"), this, Error);
		Progress = Objects->Acquire(ProgressDefinition, SelfId, this, Error);
		if (!World || !Claims || !Progress)
		{
			Fail(FString::Printf(TEXT("a hub object could not be acquired: %s"), *Error));
			return false;
		}
		Stage = EStage::WaitHub;
		return true;
	}

	for (const UCrowdyServerObject* Object : {World.Get(), Claims.Get(), Progress.Get()})
	{
		if (Object->GetStatus() == ECrowdyServerObjectStatus::Failed)
		{
			Fail(FString::Printf(TEXT("%s failed: %s"), *Object->GetDefinition()->TypeName, *Object->GetFailureReason()));
			return false;
		}
		if (Object->GetStatus() != ECrowdyServerObjectStatus::Ready)
		{
			return true;
		}
	}
	const bool bNeedsBlueprint = Steps.ContainsByPredicate([](const FCheckStep& Step) { return Step.Blueprint.Num() > 0; });
	for (TActorIterator<AConstructLayer4CheckProbe> Probe(GetWorld()); Probe && bNeedsBlueprint; ++Probe)
	{
		if (*Probe != NativeReporter)
		{
			BlueprintProbe = *Probe;
			BlueprintProbe->OnAnswer.AddUObject(this, &ThisClass::HandleAnswer);
			break;
		}
	}
	if (bNeedsBlueprint && !BlueprintProbe)
	{
		Fail(TEXT("no BP_Layer4CheckProbe is placed in the map"));
		return false;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer4Check: stimulus landed: signed in, connected, hub ready"));
	Stage = EStage::WaitGo;
	return true;
}

void UConstructLayer4CheckSubsystem::TickStep(double Now)
{
	const FCheckStep& Step = Steps[StepIndex];
	const TArray<FCheckOp>& Ops = bBlueprintTurn ? Step.Blueprint : Step.Native;
	if (!Ops.IsValidIndex(OpIndex))
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer4Check: step %d %s done"), Step.Number, bBlueprintTurn ? TEXT("bp") : TEXT("cpp"));
		OpIndex = 0;
		if (bBlueprintTurn)
		{
			Stage = EStage::WaitNode;
			LastPollAt = 0.0;
			return;
		}
		bBlueprintTurn = true;
		return;
	}
	const FCheckOp& Op = Ops[OpIndex];
	if (!Op.bQuick && Now < NextOpAt)
	{
		return;
	}
	Stage = EStage::WaitAnswer;
	AnswerDeadline = Now + Layer4AnswerSeconds;
	if (bBlueprintTurn)
	{
		RunBlueprint(Op);
	}
	else
	{
		RunNative(Op);
	}
}

void UConstructLayer4CheckSubsystem::CallHub(UCrowdyServerObject* Object, FName Function, const FInstancedPropertyBag& Params, TFunction<void(const FCrowdyServerCallResult&, const FInstancedPropertyBag&)> OnReply)
{
	TWeakObjectPtr<UConstructLayer4CheckSubsystem> WeakThis(this);
	Object->Call(Function, Params, [WeakThis, OnReply = MoveTemp(OnReply)](const FCrowdyServerCallResult& Result)
	{
		if (WeakThis.IsValid())
		{
			OnReply(Result, Result.IsSuccess() ? CrowdyExec::ToList(Result.Reply) : FInstancedPropertyBag());
		}
	});
}

void UConstructLayer4CheckSubsystem::RunNative(const FCheckOp& Op)
{
	AConstructLayer4CheckProbe* Reporter = NativeReporter;
	const int32 Index = OpIndex;
	if (Op.Kind == TEXT("world"))
	{
		const FInstancedPropertyBag State = World->GetStateList();
		Reporter->ReportWorld(Index, ECrowdyServerCallOutcome::Success, FString(), false,
			BagInt64(State, TEXT("Pulses")), BagInt64(State, TEXT("LastPulseAt")));
		return;
	}
	if (Op.Kind == TEXT("progress"))
	{
		ReadProgress(Op.InstanceId.IsEmpty() ? SelfId : Op.InstanceId);
		return;
	}
	if (Op.Kind == TEXT("programs"))
	{
		CallHub(World, TEXT("ListPrograms"), World->MakeParams(TEXT("ListPrograms")), [Reporter, Index](const FCrowdyServerCallResult& Result, const FInstancedPropertyBag& Reply)
		{
			TArray<FConstructHubProgram> Programs;
			if (const auto Items = Reply.GetArrayRef(TEXT("Programs")); Items.HasValue())
			{
				for (int32 Item = 0; Item < Items.GetValue().Num(); ++Item)
				{
					Programs.Add(*Items.GetValue().GetValueStruct<FConstructHubProgram>(Item).GetValue());
				}
			}
			Reporter->ReportPrograms(Index, Result.Outcome, Result.Reason, Result.bRetryable, Programs);
		});
		return;
	}
	if (Op.Kind == TEXT("listClaims"))
	{
		FInstancedPropertyBag Params = Claims->MakeParams(TEXT("ListClaims"));
		Params.SetValueBool(TEXT("bOnlyChunk"), Op.bOnlyChunk);
		Params.SetValueStruct(TEXT("Chunk"), Op.Chunk);
		Params.SetValueBool(TEXT("bOnlyMine"), Op.bOnlyMine);
		Params.SetValueInt32(TEXT("Limit"), Op.Limit);
		CallHub(Claims, TEXT("ListClaims"), Params, [Reporter, Index](const FCrowdyServerCallResult& Result, const FInstancedPropertyBag& Reply)
		{
			TArray<FConstructHubClaim> Found;
			if (const auto Items = Reply.GetArrayRef(TEXT("Claims")); Items.HasValue())
			{
				for (int32 Item = 0; Item < Items.GetValue().Num(); ++Item)
				{
					Found.Add(*Items.GetValue().GetValueStruct<FConstructHubClaim>(Item).GetValue());
				}
			}
			Reporter->ReportClaims(Index, Result.Outcome, Result.Reason, Result.bRetryable, Found, BagInt32(Reply, TEXT("Total")));
		});
		return;
	}
	if (Op.Kind == TEXT("record"))
	{
		FInstancedPropertyBag Params = Claims->MakeParams(TEXT("RecordClaim"));
		Params.SetValueInt64(TEXT("GridId"), Op.GridId);
		Params.SetValueStruct(TEXT("Chunk"), Op.Chunk);
		CallHub(Claims, TEXT("RecordClaim"), Params, [Reporter, Index](const FCrowdyServerCallResult& Result, const FInstancedPropertyBag& Reply)
		{
			const auto Claim = Reply.GetValueStruct<FConstructHubClaim>(TEXT("Claim"));
			Reporter->ReportRecord(Index, Result.Outcome, Result.Reason, Result.bRetryable, BagBool(Reply, TEXT("bRecorded")),
				Claim.HasValue() ? *Claim.GetValue() : FConstructHubClaim());
		});
		return;
	}
	if (Op.Kind == TEXT("release") || Op.Kind == TEXT("forget"))
	{
		const bool bForget = Op.Kind == TEXT("forget");
		const FName Function = bForget ? TEXT("ForgetClaim") : TEXT("ReleaseClaim");
		FInstancedPropertyBag Params = Claims->MakeParams(Function);
		Params.SetValueInt64(TEXT("GridId"), Op.GridId);
		CallHub(Claims, Function, Params, [Reporter, Index, bForget](const FCrowdyServerCallResult& Result, const FInstancedPropertyBag& Reply)
		{
			if (bForget)
			{
				Reporter->ReportForget(Index, Result.Outcome, Result.Reason, Result.bRetryable, BagBool(Reply, TEXT("bForgotten")));
				return;
			}
			Reporter->ReportRelease(Index, Result.Outcome, Result.Reason, Result.bRetryable, BagBool(Reply, TEXT("bReleased")));
		});
		return;
	}
	Fail(FString::Printf(TEXT("the plan names an unknown kind %s"), *Op.Kind));
}

void UConstructLayer4CheckSubsystem::RunBlueprint(const FCheckOp& Op)
{
	AConstructLayer4CheckProbe* Probe = BlueprintProbe;
	if (Op.Kind == TEXT("world"))
	{
		Probe->RunReadWorld(OpIndex);
	}
	else if (Op.Kind == TEXT("progress"))
	{
		Probe->RunReadProgress(OpIndex, Op.InstanceId.IsEmpty() ? SelfId : Op.InstanceId);
	}
	else if (Op.Kind == TEXT("programs"))
	{
		Probe->RunListPrograms(OpIndex);
	}
	else if (Op.Kind == TEXT("listClaims"))
	{
		Probe->RunListClaims(OpIndex, Op.bOnlyChunk, Op.Chunk, Op.bOnlyMine, Op.Limit);
	}
	else if (Op.Kind == TEXT("record"))
	{
		Probe->RunRecordClaim(OpIndex, Op.GridId, Op.Chunk);
	}
	else if (Op.Kind == TEXT("release"))
	{
		Probe->RunReleaseClaim(OpIndex, Op.GridId);
	}
	else if (Op.Kind == TEXT("forget"))
	{
		Probe->RunForgetClaim(OpIndex, Op.GridId);
	}
	else
	{
		Fail(FString::Printf(TEXT("the plan names an unknown kind %s"), *Op.Kind));
	}
}

void UConstructLayer4CheckSubsystem::ReadProgress(const FString& InstanceId)
{
	if (InstanceId == SelfId)
	{
		ReadingProgress = Progress;
		return;
	}
	FString Error;
	UCrowdyServerObjectSubsystem* Objects = GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyServerObjectSubsystem>();
	ReadingProgress = Objects->Acquire(ProgressDefinition, InstanceId, this, Error);
	if (!ReadingProgress)
	{
		NativeReporter->ReportReadRefused(OpIndex, Error);
	}
}

void UConstructLayer4CheckSubsystem::FinishProgressRead()
{
	const ECrowdyServerObjectStatus Status = ReadingProgress->GetStatus();
	if (Status != ECrowdyServerObjectStatus::Ready && Status != ECrowdyServerObjectStatus::Failed)
	{
		return;
	}
	UCrowdyServerObject* Object = ReadingProgress;
	ReadingProgress = nullptr;
	if (Status == ECrowdyServerObjectStatus::Failed)
	{
		NativeReporter->ReportReadRefused(OpIndex, Object->GetFailureReason());
	}
	else
	{
		const FInstancedPropertyBag State = Object->GetStateList();
		NativeReporter->ReportProgress(OpIndex, ECrowdyServerCallOutcome::Success, FString(), false,
			BagInt64(State, TEXT("Xp")), BagInt64(State, TEXT("Level")), BagInt64(State, TEXT("SkillPoints")));
	}
	if (Object != Progress)
	{
		GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyServerObjectSubsystem>()->Release(Object, this);
	}
}

void UConstructLayer4CheckSubsystem::HandleAnswer(int32 AnsweredOp, const FString& AnswerJson)
{
	if (Stage != EStage::WaitAnswer)
	{
		return;
	}
	const FCheckStep& Step = Steps[StepIndex];
	const TArray<FCheckOp>& Ops = bBlueprintTurn ? Step.Blueprint : Step.Native;
	if (AnsweredOp != OpIndex || !Ops.IsValidIndex(OpIndex))
	{
		Fail(FString::Printf(TEXT("an answer for op %d arrived while waiting for op %d"), AnsweredOp, OpIndex));
		return;
	}
	const FString Line = FString::Printf(TEXT("{\"op\":\"%s\",\"path\":\"%s\",\"answer\":%s}\n"), *Ops[OpIndex].Id, bBlueprintTurn ? TEXT("bp") : TEXT("cpp"), *AnswerJson);
	if (!FFileHelper::SaveStringToFile(Line, *ResultsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
	{
		Fail(TEXT("the results file could not be written"));
		return;
	}
	++OpIndex;
	NextOpAt = FPlatformTime::Seconds() + Layer4OpSpacingSeconds;
	Stage = EStage::RunStep;
}

void UConstructLayer4CheckSubsystem::Fail(const FString& Why)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer4Check: FAIL: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
