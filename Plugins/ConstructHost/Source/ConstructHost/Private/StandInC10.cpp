#include "StandInC10.h"

#include "Engine/World.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogConstructStandIn, Log, All);

namespace StandInC10Steps
{
	// Voxels along one chunk axis, as the platform stores chunks.
	constexpr double ChunkVoxels = 16.0;

	const TCHAR* const HostCalls[] = { TEXT("DrainClicks"), TEXT("ReadChunk"), TEXT("ListPlayers"), TEXT("SetVoxel"), TEXT("ShowHud"), TEXT("DrawOverlay"), TEXT("ClearScript") };
	const TCHAR* const Events[] = { TEXT("OnModScriptPrompt"), TEXT("OnModScriptPromptWithdrawn"), TEXT("OnModScriptStarted"), TEXT("OnModScriptStopped"), TEXT("OnModScriptError") };
	const TCHAR* const Checkpoints[] = { TEXT("AnswerPrompt"), TEXT("Revoke"), TEXT("ForgetAuthor"), TEXT("LeaveGrid") };

	bool IsOneOf(const FString& Name, TArrayView<const TCHAR* const> Names)
	{
		return Names.ContainsByPredicate([&Name](const TCHAR* Each) { return Name.Equals(Each, ESearchCase::CaseSensitive); });
	}

	bool StepIsValid(const FJsonObject& Step, FString& Problem)
	{
		FString Name;
		if (Step.TryGetStringField(TEXT("waitFor"), Name))
		{
			const TSharedPtr<FJsonObject>* Branches = nullptr;
			if (!IsOneOf(Name, Checkpoints))
			{
				Problem = FString::Printf(TEXT("waitFor %s is not AnswerPrompt, Revoke, ForgetAuthor or LeaveGrid"), *Name);
				return false;
			}
			if (!Step.TryGetObjectField(TEXT("branches"), Branches))
			{
				Problem = TEXT("a checkpoint needs a branches object");
				return false;
			}
			const int32 Known = int32((*Branches)->HasField(TEXT("Run"))) + int32((*Branches)->HasField(TEXT("NotNow")));
			if (Name == TEXT("AnswerPrompt") && Known != (*Branches)->Values.Num())
			{
				Problem = TEXT("AnswerPrompt branches are named Run or NotNow");
				return false;
			}
			return true;
		}
		const bool bCall = Step.TryGetStringField(TEXT("call"), Name);
		if (!bCall && !Step.TryGetStringField(TEXT("event"), Name))
		{
			Problem = TEXT("a step needs call, event or waitFor");
			return false;
		}
		if (!IsOneOf(Name, bCall ? MakeArrayView(HostCalls) : MakeArrayView(Events)))
		{
			Problem = FString::Printf(TEXT("%s is not a known %s"), *Name, bCall ? TEXT("host call") : TEXT("event"));
			return false;
		}
		const TSharedPtr<FJsonValue> AtMs = Step.TryGetField(TEXT("atMs"));
		if (!AtMs.IsValid() || AtMs->Type != EJson::Number)
		{
			Problem = FString::Printf(TEXT("%s needs a numeric atMs"), *Name);
			return false;
		}
		return true;
	}

	bool StepsAreValid(const TArray<TSharedPtr<FJsonValue>>& List, const FString& Path)
	{
		bool bValid = true;
		for (int32 Index = 0; Index < List.Num(); ++Index)
		{
			const FString Here = Path.IsEmpty() ? FString::FromInt(Index) : FString::Printf(TEXT("%s.%d"), *Path, Index);
			const TSharedPtr<FJsonObject> Step = List[Index].IsValid() && List[Index]->Type == EJson::Object ? List[Index]->AsObject() : nullptr;
			FString Problem = TEXT("a step is a JSON object");
			if (!Step.IsValid() || !StepIsValid(*Step, Problem))
			{
				UE_LOG(LogConstructStandIn, Error, TEXT("StandInC10: step %s: %s"), *Here, *Problem);
				bValid = false;
				continue;
			}
			const TSharedPtr<FJsonObject>* Branches = nullptr;
			if (!Step->TryGetObjectField(TEXT("branches"), Branches))
			{
				continue;
			}
			int32 BranchIndex = 0;
			for (const auto& Pair : (*Branches)->Values)
			{
				const FString BranchPath = FString::Printf(TEXT("%s.branch%d"), *Here, BranchIndex++);
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Array)
				{
					UE_LOG(LogConstructStandIn, Error, TEXT("StandInC10: step %s: a branch is a JSON array"), *BranchPath);
					bValid = false;
					continue;
				}
				bValid &= StepsAreValid(Pair.Value->AsArray(), BranchPath);
			}
		}
		return bValid;
	}

	template <typename StructType>
	bool ReadStruct(const FJsonObject& Args, const TCHAR* Name, StructType& Out)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Args.TryGetObjectField(Name, Object) && FJsonObjectConverter::JsonObjectToUStruct(Object->ToSharedRef(), &Out);
	}
}

bool UStandInC10::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UStandInC10::Tick(float DeltaTime)
{
	if (!bReplaying || !WaitingFor.IsEmpty())
	{
		return;
	}
	SegmentMs += DeltaTime * 1000.0;
	ReplayDue();
}

TStatId UStandInC10::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStandInC10, STATGROUP_Tickables);
}

bool UStandInC10::SelectTimelineForStandIn(FName Name)
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("StandIn/C10") / (Name.ToString() + TEXT(".json"));
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		Timeline.Reset();
		UE_LOG(LogConstructStandIn, Warning, TEXT("StandInC10: no timeline at %s"), *Path);
		return false;
	}
	return SelectTimelineTextForStandIn(Json);
}

bool UStandInC10::SelectTimelineTextForStandIn(const FString& Json)
{
	Timeline.Reset();
	TArray<TSharedPtr<FJsonValue>> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Parsed))
	{
		UE_LOG(LogConstructStandIn, Error, TEXT("StandInC10: the timeline is not a JSON array"));
		return false;
	}
	if (!StandInC10Steps::StepsAreValid(Parsed, FString()))
	{
		return false;
	}
	Timeline = MoveTemp(Parsed);
	return true;
}

bool UStandInC10::IsAvailable() const
{
	return true;
}

void UStandInC10::EnterGrid(int64 ChunkX, int64 ChunkY, int64 ChunkZ)
{
	const bool bInside = Grid.GridId != 0
		&& ChunkX >= Grid.LowChunkX && ChunkX <= Grid.HighChunkX
		&& ChunkY >= Grid.LowChunkY && ChunkY <= Grid.HighChunkY
		&& ChunkZ >= Grid.LowChunkZ && ChunkZ <= Grid.HighChunkZ;
	if (bInside)
	{
		return;
	}
	EndVisit();
	if (Timeline.IsEmpty())
	{
		UE_LOG(LogConstructStandIn, Warning, TEXT("StandInC10: EnterGrid with no timeline selected replays nothing"));
	}
	EntryChunk = FInt64Vector(ChunkX, ChunkY, ChunkZ);
	Grid.GridId = 1;
	Grid.LowChunkX = ChunkX - GridReach;
	Grid.LowChunkY = ChunkY - GridReach;
	Grid.LowChunkZ = ChunkZ - GridReach;
	Grid.HighChunkX = ChunkX + GridReach;
	Grid.HighChunkY = ChunkY + GridReach;
	Grid.HighChunkZ = ChunkZ + GridReach;
	++Generation;
	Steps = Timeline;
	NextStep = 0;
	SegmentMs = 0.0;
	bReplaying = Steps.Num() > 0;
}

void UStandInC10::LeaveGrid()
{
	if (Grid.GridId == 0)
	{
		return;
	}
	if (WaitingFor == TEXT("LeaveGrid"))
	{
		TakeBranch(TEXT("Any"));
	}
	EndVisit();
}

void UStandInC10::EndVisit()
{
	Grid = FCrowdyModScriptGrid();
	bReplaying = false;
	WaitingFor.Reset();
	Branches.Reset();
	const TArray<int32> Withdrawn = MoveTemp(OpenPrompts);
	OpenPrompts.Reset();
	for (const int32 PromptId : Withdrawn)
	{
		OnModScriptPromptWithdrawn.Broadcast(PromptId);
	}
	const TArray<FCrowdyModScript> Stopped = MoveTemp(Running);
	Running.Reset();
	for (const FCrowdyModScript& Script : Stopped)
	{
		if (UObject* Host = HostObject.Get())
		{
			ICrowdyModScriptHost::Execute_ClearScript(Host, Script);
		}
		OnModScriptStopped.Broadcast(Script, ECrowdyModScriptStop::LeftGrid);
	}
}

FCrowdyModScriptGrid UStandInC10::GetGrid() const
{
	return Grid;
}

void UStandInC10::SetHost(TScriptInterface<ICrowdyModScriptHost> Host)
{
	HostObject = Host.GetObject();
}

void UStandInC10::ClearHost(TScriptInterface<ICrowdyModScriptHost> Host)
{
	if (HostObject.Get() == Host.GetObject())
	{
		HostObject.Reset();
	}
}

void UStandInC10::AnswerPrompt(int32 PromptId, bool bRun)
{
	if (OpenPrompts.Remove(PromptId) == 0)
	{
		return;
	}
	if (WaitingFor == TEXT("AnswerPrompt"))
	{
		TakeBranch(bRun ? TEXT("Run") : TEXT("NotNow"));
	}
}

TArray<FCrowdyModScript> UStandInC10::GetRunning() const
{
	return Running;
}

void UStandInC10::Revoke(const FString& ModId)
{
	if (WaitingFor == TEXT("Revoke"))
	{
		TakeBranch(Branches.IsValid() && Branches->HasField(ModId) ? ModId : FString(TEXT("Any")));
	}
}

void UStandInC10::ForgetAuthor(int64 AuthorId)
{
	const FString Author = LexToString(AuthorId);
	if (WaitingFor == TEXT("ForgetAuthor"))
	{
		TakeBranch(Branches.IsValid() && Branches->HasField(Author) ? Author : FString(TEXT("Any")));
	}
}

void UStandInC10::TakeBranch(const FString& Input)
{
	const TArray<TSharedPtr<FJsonValue>>* Branch = nullptr;
	if (!Branches.IsValid() || !Branches->TryGetArrayField(Input, Branch))
	{
		return;
	}
	Steps = *Branch;
	Branches.Reset();
	WaitingFor.Reset();
	NextStep = 0;
	SegmentMs = 0.0;
	ReplayDue();
}

bool UStandInC10::EnterCheckpoint(const FJsonObject& Step)
{
	FString Checkpoint;
	if (!Step.TryGetStringField(TEXT("waitFor"), Checkpoint))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* StepBranches = nullptr;
	Branches.Reset();
	if (Step.TryGetObjectField(TEXT("branches"), StepBranches))
	{
		Branches = *StepBranches;
	}
	WaitingFor = Checkpoint;
	return true;
}

void UStandInC10::ReplayDue()
{
	while (bReplaying && WaitingFor.IsEmpty() && NextStep < Steps.Num())
	{
		const TSharedPtr<FJsonObject> Step = Steps[NextStep]->AsObject();
		if (EnterCheckpoint(*Step))
		{
			++NextStep;
			return;
		}
		double AtMs = 0.0;
		Step->TryGetNumberField(TEXT("atMs"), AtMs);
		if (AtMs > SegmentMs)
		{
			return;
		}
		++NextStep;
		// A checkpoint right after this step opens first, so the player's answer to it may come from inside this step.
		if (NextStep < Steps.Num() && EnterCheckpoint(*Steps[NextStep]->AsObject()))
		{
			++NextStep;
		}
		RunStep(*Step);
	}
}

void UStandInC10::RunStep(const FJsonObject& Step)
{
	static const FJsonObject NoArgs;
	const TSharedPtr<FJsonObject>* ArgsField = nullptr;
	const FJsonObject& Args = Step.TryGetObjectField(TEXT("args"), ArgsField) ? **ArgsField : NoArgs;
	FString Name;
	if (Step.TryGetStringField(TEXT("call"), Name))
	{
		UObject* Host = HostObject.Get();
		if (Host && !CallHost(Host, Name, Args))
		{
			UE_LOG(LogConstructStandIn, Error, TEXT("StandInC10: call %s lacks an argument"), *Name);
		}
		return;
	}
	if (Step.TryGetStringField(TEXT("event"), Name) && !FireEvent(Name, Args))
	{
		UE_LOG(LogConstructStandIn, Error, TEXT("StandInC10: event %s lacks an argument"), *Name);
	}
}

bool UStandInC10::ReadScript(const FJsonObject& Args, FCrowdyModScript& Script) const
{
	if (!StandInC10Steps::ReadStruct(Args, TEXT("Script"), Script))
	{
		return false;
	}
	Script.GridId = Grid.GridId;
	Script.Generation = Generation;
	return true;
}

bool UStandInC10::CallHost(UObject* Host, const FString& Method, const FJsonObject& Args) const
{
	using namespace StandInC10Steps;
	if (Method == TEXT("DrainClicks"))
	{
		FCrowdyModScriptClicks Clicks;
		ICrowdyModScriptHost::Execute_DrainClicks(Host, Clicks);
		return true;
	}
	if (Method == TEXT("ReadChunk"))
	{
		int64 X = 0;
		int64 Y = 0;
		int64 Z = 0;
		if (!Args.TryGetNumberField(TEXT("ChunkX"), X) || !Args.TryGetNumberField(TEXT("ChunkY"), Y) || !Args.TryGetNumberField(TEXT("ChunkZ"), Z))
		{
			return false;
		}
		FCrowdyModScriptChunk Chunk;
		ICrowdyModScriptHost::Execute_ReadChunk(Host, EntryChunk.X + X, EntryChunk.Y + Y, EntryChunk.Z + Z, Chunk);
		return true;
	}
	if (Method == TEXT("ListPlayers"))
	{
		TArray<FCrowdyModScriptPlayer> Players;
		ICrowdyModScriptHost::Execute_ListPlayers(Host, Grid, Players);
		return true;
	}
	FCrowdyModScript Script;
	if (!ReadScript(Args, Script))
	{
		return false;
	}
	if (Method == TEXT("SetVoxel"))
	{
		FCrowdyVoxelWrite Write;
		if (!ReadStruct(Args, TEXT("Write"), Write))
		{
			return false;
		}
		Write.ChunkX += EntryChunk.X;
		Write.ChunkY += EntryChunk.Y;
		Write.ChunkZ += EntryChunk.Z;
		FString Refusal;
		ICrowdyModScriptHost::Execute_SetVoxel(Host, Script, Write, Refusal);
		return true;
	}
	if (Method == TEXT("ShowHud"))
	{
		FString Text;
		if (!Args.TryGetStringField(TEXT("Text"), Text))
		{
			return false;
		}
		ICrowdyModScriptHost::Execute_ShowHud(Host, Script, Text);
		return true;
	}
	if (Method == TEXT("DrawOverlay"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		TArray<FCrowdyModScriptShape> Shapes;
		if (!Args.TryGetArrayField(TEXT("Shapes"), Values) || !FJsonObjectConverter::JsonArrayToUStruct(*Values, &Shapes))
		{
			return false;
		}
		const FVector Corner = FVector(static_cast<double>(EntryChunk.X), static_cast<double>(EntryChunk.Y), static_cast<double>(EntryChunk.Z)) * ChunkVoxels;
		for (FCrowdyModScriptShape& Shape : Shapes)
		{
			Shape.Location += Corner;
		}
		ICrowdyModScriptHost::Execute_DrawOverlay(Host, Script, Shapes);
		return true;
	}
	ICrowdyModScriptHost::Execute_ClearScript(Host, Script);
	return true;
}

bool UStandInC10::FireEvent(const FString& Event, const FJsonObject& Args)
{
	using namespace StandInC10Steps;
	if (Event == TEXT("OnModScriptPrompt"))
	{
		FCrowdyModScriptPrompt Prompt;
		if (!ReadStruct(Args, TEXT("Prompt"), Prompt))
		{
			return false;
		}
		Prompt.GridId = Grid.GridId;
		OpenPrompts.AddUnique(Prompt.PromptId);
		OnModScriptPrompt.Broadcast(Prompt);
		return true;
	}
	if (Event == TEXT("OnModScriptPromptWithdrawn"))
	{
		int32 PromptId = 0;
		if (!Args.TryGetNumberField(TEXT("PromptId"), PromptId))
		{
			return false;
		}
		OpenPrompts.Remove(PromptId);
		OnModScriptPromptWithdrawn.Broadcast(PromptId);
		return true;
	}
	if (Event == TEXT("OnModScriptError"))
	{
		FCrowdyModScriptError Error;
		if (!ReadStruct(Args, TEXT("Error"), Error))
		{
			return false;
		}
		OnModScriptError.Broadcast(Error);
		return true;
	}
	FCrowdyModScript Script;
	if (!ReadScript(Args, Script))
	{
		return false;
	}
	const auto SameMod = [&Script](const FCrowdyModScript& Each) { return Each.ModId == Script.ModId; };
	if (Event == TEXT("OnModScriptStarted"))
	{
		Running.RemoveAll(SameMod);
		Running.Add(Script);
		OnModScriptStarted.Broadcast(Script);
		return true;
	}
	FString ReasonName;
	const int64 Reason = Args.TryGetStringField(TEXT("Reason"), ReasonName) ? StaticEnum<ECrowdyModScriptStop>()->GetValueByNameString(ReasonName) : INDEX_NONE;
	if (Reason == INDEX_NONE)
	{
		return false;
	}
	Running.RemoveAll(SameMod);
	OnModScriptStopped.Broadcast(Script, static_cast<ECrowdyModScriptStop>(Reason));
	return true;
}
