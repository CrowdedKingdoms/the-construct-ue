#include "StandInC11.h"

#include "ConstructStudioDock.h"
#include "Engine/World.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogConstructStandInC11, Log, All);

namespace StandInC11Steps
{
	const TCHAR* const Events[] = {
		TEXT("OnStateChanged"), TEXT("OnUsageChanged"), TEXT("OnWarning"), TEXT("OnFocusChanged"), TEXT("OnCloseRequested"),
		TEXT("studio.projectOpen"), TEXT("studio.draftTest"), TEXT("studio.deployLive"), TEXT("studio.screenshot"),
		TEXT("fileChanged"), TEXT("RunProjectAnswered") };
	const TCHAR* const Checkpoints[] = { TEXT("GiveProviderConsent"), TEXT("AnswerDeployPrompt"), TEXT("Close"), TEXT("SetGrid") };
	const TCHAR* const ReplayedStates[] = { TEXT("NeedsConsent"), TEXT("Ready"), TEXT("Failed") };

	bool IsOneOf(const FString& Name, TArrayView<const TCHAR* const> Names)
	{
		return Names.ContainsByPredicate([&Name](const TCHAR* Each) { return Name.Equals(Each, ESearchCase::CaseSensitive); });
	}

	bool CheckpointIsValid(const FString& Name, const FJsonObject& Step, FString& Problem)
	{
		const TSharedPtr<FJsonObject>* Branches = nullptr;
		if (!IsOneOf(Name, Checkpoints))
		{
			Problem = FString::Printf(TEXT("waitFor %s is not GiveProviderConsent, AnswerDeployPrompt, Close or SetGrid"), *Name);
			return false;
		}
		if (!Step.TryGetObjectField(TEXT("branches"), Branches))
		{
			Problem = TEXT("a checkpoint needs a branches object");
			return false;
		}
		const int32 Known = int32((*Branches)->HasField(TEXT("Run"))) + int32((*Branches)->HasField(TEXT("NotNow")));
		if (Name == TEXT("AnswerDeployPrompt") && Known != (*Branches)->Values.Num())
		{
			Problem = TEXT("AnswerDeployPrompt branches are named Run or NotNow");
			return false;
		}
		return true;
	}

	bool StepIsValid(const FJsonObject& Step, FString& Problem)
	{
		FString Name;
		if (Step.TryGetStringField(TEXT("waitFor"), Name))
		{
			return CheckpointIsValid(Name, Step, Problem);
		}
		if (!Step.TryGetStringField(TEXT("event"), Name))
		{
			Problem = TEXT("a step needs event or waitFor");
			return false;
		}
		if (!IsOneOf(Name, Events))
		{
			Problem = FString::Printf(TEXT("%s is not a known event"), *Name);
			return false;
		}
		const TSharedPtr<FJsonValue> AtMs = Step.TryGetField(TEXT("atMs"));
		if (!AtMs.IsValid() || AtMs->Type != EJson::Number)
		{
			Problem = FString::Printf(TEXT("%s needs a numeric atMs"), *Name);
			return false;
		}
		const TSharedPtr<FJsonObject>* Args = nullptr;
		FString StateName;
		if (Name == TEXT("OnStateChanged") && !(Step.TryGetObjectField(TEXT("args"), Args) && (*Args)->TryGetStringField(TEXT("State"), StateName) && IsOneOf(StateName, ReplayedStates)))
		{
			Problem = TEXT("OnStateChanged needs args.State NeedsConsent, Ready or Failed");
			return false;
		}
		if (Name == TEXT("studio.projectOpen") && !(Step.TryGetObjectField(TEXT("args"), Args) && (*Args)->HasTypedField<EJson::String>(TEXT("ProjectId"))))
		{
			Problem = TEXT("studio.projectOpen needs args.ProjectId as a JSON string");
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
				UE_LOG(LogConstructStandInC11, Error, TEXT("StandInC11: step %s: %s"), *Here, *Problem);
				bValid = false;
				continue;
			}
			const TSharedPtr<FJsonObject>* Branches = nullptr;
			if (!Step->TryGetObjectField(TEXT("branches"), Branches))
			{
				continue;
			}
			for (const auto& Pair : (*Branches)->Values)
			{
				const FString BranchPath = FString::Printf(TEXT("%s.%s"), *Here, *Pair.Key);
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Array)
				{
					UE_LOG(LogConstructStandInC11, Error, TEXT("StandInC11: step %s: a branch is a JSON array"), *BranchPath);
					bValid = false;
					continue;
				}
				bValid &= StepsAreValid(Pair.Value->AsArray(), BranchPath);
			}
		}
		return bValid;
	}
}

bool UStandInC11::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UStandInC11::Deinitialize()
{
	StopListening();
	Super::Deinitialize();
}

void UStandInC11::BeginDestroy()
{
	StopListening();
	Super::BeginDestroy();
}

void UStandInC11::Tick(float DeltaTime)
{
	if (!bReplaying || !WaitingFor.IsEmpty())
	{
		return;
	}
	SegmentMs += DeltaTime * 1000.0;
	ReplayDue();
}

TStatId UStandInC11::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStandInC11, STATGROUP_Tickables);
}

bool UStandInC11::SelectTimelineForStandIn(FName Name)
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("StandIn/C11") / (Name.ToString() + TEXT(".json"));
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		Timeline.Reset();
		UE_LOG(LogConstructStandInC11, Warning, TEXT("StandInC11: no timeline at %s"), *Path);
		return false;
	}
	return SelectTimelineTextForStandIn(Json);
}

bool UStandInC11::SelectTimelineTextForStandIn(const FString& Json)
{
	Timeline.Reset();
	TArray<TSharedPtr<FJsonValue>> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Parsed))
	{
		UE_LOG(LogConstructStandInC11, Error, TEXT("StandInC11: the timeline is not a JSON array"));
		return false;
	}
	if (!StandInC11Steps::StepsAreValid(Parsed, FString()))
	{
		return false;
	}
	Timeline = MoveTemp(Parsed);
	Listen();
	return true;
}

void UStandInC11::Listen()
{
	if (!Listening.IsValid())
	{
		Listening = UConstructStudioDock::Outbound.AddUObject(this, &UStandInC11::Heard);
	}
}

void UStandInC11::StopListening()
{
	UConstructStudioDock::Outbound.Remove(Listening);
	Listening.Reset();
}

void UStandInC11::Heard(UConstructStudioDock& Dock, FName Message, const FString& Detail)
{
	if (Message == TEXT("Open"))
	{
		if (Timeline.IsEmpty() || Dock.GetWorld() != GetWorld())
		{
			return;
		}
		Dock.ClaimBoot();
		Target = &Dock;
		Generation = Dock.Generation;
		Steps = Timeline;
		NextStep = 0;
		SegmentMs = 0.0;
		WaitingFor.Reset();
		Branches.Reset();
		bReplaying = true;
		return;
	}
	if (Target.Get() != &Dock)
	{
		return;
	}
	if (Message == TEXT("studio.screenshot") || Message == TEXT("page.capture"))
	{
		UE_LOG(LogConstructStandInC11, Display, TEXT("StandInC11: %s answered: %s"), *Message.ToString(), *Detail);
	}
	if (WaitingFor == Message.ToString())
	{
		TakeBranch(Branches.IsValid() && Branches->HasField(Detail) ? Detail : FString(TEXT("Any")));
		return;
	}
	if (Message == TEXT("Close"))
	{
		bReplaying = false;
	}
}

void UStandInC11::TakeBranch(const FString& Input)
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

bool UStandInC11::EnterCheckpoint(const FJsonObject& Step)
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

void UStandInC11::ReplayDue()
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

void UStandInC11::RunStep(const FJsonObject& Step)
{
	UConstructStudioDock* Dock = Target.Get();
	if (!Dock)
	{
		bReplaying = false;
		return;
	}
	static const FJsonObject NoArgs;
	const TSharedPtr<FJsonObject>* ArgsField = nullptr;
	const FJsonObject& Args = Step.TryGetObjectField(TEXT("args"), ArgsField) ? **ArgsField : NoArgs;
	FString Event;
	Step.TryGetStringField(TEXT("event"), Event);
	if (!Replay(*Dock, Event, Args))
	{
		UE_LOG(LogConstructStandInC11, Error, TEXT("StandInC11: event %s lacks an argument"), *Event);
	}
}

bool UStandInC11::Replay(UConstructStudioDock& Dock, const FString& Event, const FJsonObject& Args) const
{
	FString Text;
	bool bFlag = false;
	if (Event == TEXT("OnStateChanged"))
	{
		FString StateName;
		FString ReasonName;
		Args.TryGetStringField(TEXT("State"), StateName);
		Args.TryGetStringField(TEXT("Reason"), ReasonName);
		Args.TryGetStringField(TEXT("Text"), Text);
		const int64 NewState = StaticEnum<EConstructStudioDockState>()->GetValueByNameString(StateName);
		const int64 NewReason = ReasonName.IsEmpty() ? 0 : StaticEnum<EConstructStudioDockReason>()->GetValueByNameString(ReasonName);
		if (NewState == INDEX_NONE || NewReason == INDEX_NONE)
		{
			return false;
		}
		Dock.ReceiveBootOutcome(Generation, static_cast<EConstructStudioDockState>(NewState), static_cast<EConstructStudioDockReason>(NewReason), FText::FromString(Text));
		return true;
	}
	if (Event == TEXT("studio.projectOpen"))
	{
		FString ProjectId;
		FString Name;
		FString Revision;
		if (!Args.TryGetStringField(TEXT("ProjectId"), ProjectId) ||!Args.TryGetStringField(TEXT("ProjectName"), Name) || !Args.TryGetStringField(TEXT("Revision"), Revision))
		{
			return false;
		}
		Args.TryGetBoolField(TEXT("bReachesVisitors"), bFlag);
		Dock.ReceiveProject(Generation, ProjectId, Name, Revision, bFlag);
		return true;
	}
	if (Event == TEXT("fileChanged"))
	{
		FString Path;
		if (!Args.TryGetStringField(TEXT("Path"), Path) || !Args.TryGetStringField(TEXT("Revision"), Text))
		{
			return false;
		}
		Dock.ReceiveFileChanged(Generation, Path, Text);
		return true;
	}
	if (Event == TEXT("studio.draftTest") || Event == TEXT("studio.deployLive"))
	{
		Dock.ReceiveDeployRequest(Generation, Event == TEXT("studio.deployLive") ? EConstructStudioDeployKind::Live : EConstructStudioDeployKind::DraftTest);
		return true;
	}
	if (Event == TEXT("RunProjectAnswered"))
	{
		if (!Args.TryGetBoolField(TEXT("bOk"), bFlag))
		{
			return false;
		}
		Dock.ReceiveRunAnswer(Generation, bFlag);
		return true;
	}
	if (Event == TEXT("studio.screenshot"))
	{
		if (!Args.TryGetStringField(TEXT("Label"), Text))
		{
			return false;
		}
		Dock.ReceiveScreenshotRequest(Generation, Text);
		return true;
	}
	if (Event == TEXT("OnUsageChanged"))
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FConstructStudioUsage Usage;
		if (!Args.TryGetObjectField(TEXT("Usage"), Object) || !FJsonObjectConverter::JsonObjectToUStruct(Object->ToSharedRef(), &Usage))
		{
			return false;
		}
		Dock.ReceiveUsage(Generation, Usage);
		return true;
	}
	if (Event == TEXT("OnWarning"))
	{
		if (!Args.TryGetStringField(TEXT("Text"), Text))
		{
			return false;
		}
		Dock.ReceiveWarning(Generation, FText::FromString(Text));
		return true;
	}
	if (Event == TEXT("OnFocusChanged"))
	{
		if (!Args.TryGetBoolField(TEXT("bHasFocus"), bFlag))
		{
			return false;
		}
		Dock.ReceiveFocus(Generation, bFlag);
		return true;
	}
	Dock.ReceiveCloseRequest(Generation);
	return true;
}
