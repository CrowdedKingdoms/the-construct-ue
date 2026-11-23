#include "ConstructStudioDockReplies.h"

namespace ConstructStudioDockReplyParts
{
	void SetIfAny(FJsonObject& Object, const TCHAR* Name, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Object.SetStringField(Name, Value);
		}
	}

	const TCHAR* SeverityName(ECrowdyModDiagnosticSeverity Severity)
	{
		switch (Severity)
		{
		case ECrowdyModDiagnosticSeverity::Error: return TEXT("error");
		case ECrowdyModDiagnosticSeverity::Warning: return TEXT("warning");
		default: return TEXT("info");
		}
	}

	const TCHAR* LevelName(int32 Level)
	{
		switch (Level)
		{
		case 0: return TEXT("error");
		case 1: return TEXT("warning");
		case 2: return TEXT("info");
		default: return TEXT("debug");
		}
	}

	TSharedRef<FJsonObject> DiagnosticOf(const FCrowdyModDiagnostic& Diagnostic)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("target"), ConstructStudioDockReplies::TargetName(Diagnostic.Target));
		Out->SetStringField(TEXT("path"), Diagnostic.Path);
		Out->SetNumberField(TEXT("line"), Diagnostic.Line);
		Out->SetNumberField(TEXT("column"), Diagnostic.Column);
		Out->SetStringField(TEXT("severity"), SeverityName(Diagnostic.Severity));
		Out->SetStringField(TEXT("message"), Diagnostic.Code.IsEmpty() ? Diagnostic.Message : FString::Printf(TEXT("%s [%s]"), *Diagnostic.Message, *Diagnostic.Code));
		return Out;
	}

	TSharedRef<FJsonObject> RuntimeEntry(const FCrowdyModRunTarget& Target)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("target"), ConstructStudioDockReplies::TargetName(Target.Target));
		Out->SetStringField(TEXT("phase"), ConstructStudioDockReplies::PhaseName(Target.Phase));
		SetIfAny(*Out, TEXT("moduleName"), Target.Module);
		SetIfAny(*Out, TEXT("runningRevision"), Target.RunningRevision);
		SetIfAny(*Out, TEXT("savedRevision"), Target.SavedRevision);
		SetIfAny(*Out, TEXT("message"), Target.Message.Left(1000));
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> Runtime(const TArray<FCrowdyModRunTarget>& Targets)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FCrowdyModRunTarget& Target : Targets)
		{
			Out.Add(MakeShared<FJsonValueObject>(RuntimeEntry(Target)));
		}
		return Out;
	}

	TSharedRef<FJsonObject> Lines(const TArray<FString>& All, int32 Limit, bool bMoreHeld)
	{
		const int32 First = FMath::Max(0, All.Num() - Limit);
		TArray<TSharedPtr<FJsonValue>> Kept;
		for (int32 Index = First; Index < All.Num(); ++Index)
		{
			Kept.Add(MakeShared<FJsonValueString>(All[Index]));
		}
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetArrayField(TEXT("lines"), Kept);
		Out->SetBoolField(TEXT("truncated"), bMoreHeld || First > 0);
		return Out;
	}

	FString LogLine(int64 AtMs, const FString& Module, int32 Level, const FString& Text)
	{
		return FString::Printf(TEXT("%s %s %s: %s"), *ConstructStudioDockReplies::Iso8601(AtMs), *Module, LevelName(Level), *Text.Left(2000));
	}

	TArray<TSharedPtr<FJsonObject>> EntriesOf(const FJsonObject& Result)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Result.TryGetArrayField(TEXT("runtime"), Entries))
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			if (Entry.IsValid() && Entry->Type == EJson::Object)
			{
				Out.Add(Entry->AsObject());
			}
		}
		return Out;
	}

	TSharedPtr<FJsonObject> EntryFor(const FJsonObject& Result, ECrowdyStudioTarget Target)
	{
		const FString Name = ConstructStudioDockReplies::TargetName(Target);
		for (const TSharedPtr<FJsonObject>& Entry : EntriesOf(Result))
		{
			FString Named;
			if (Entry->TryGetStringField(TEXT("target"), Named) && Named.Equals(Name, ESearchCase::CaseSensitive))
			{
				return Entry;
			}
		}
		return nullptr;
	}

	bool PhaseIs(const FJsonObject& Entry, ECrowdyModRunPhase Phase)
	{
		FString Named;
		return Entry.TryGetStringField(TEXT("phase"), Named) && Named.Equals(ConstructStudioDockReplies::PhaseName(Phase), ESearchCase::CaseSensitive);
	}

	/** Controls, and the format and bidirectional marks that hide text or reorder how it shows. */
	bool IsHiddenChar(uint32 Code)
	{
		return Code < 0x20 || (Code >= 0x7F && Code <= 0x9F) || Code == 0xAD || (Code >= 0x600 && Code <= 0x605) || Code == 0x61C
			|| Code == 0x6DD || Code == 0x70F || Code == 0x180E || (Code >= 0x200B && Code <= 0x200F) || (Code >= 0x2028 && Code <= 0x202E)
			|| (Code >= 0x2060 && Code <= 0x206F) || Code == 0xFEFF || (Code >= 0xFFF9 && Code <= 0xFFFB) || (Code >= 0xE0000 && Code <= 0xE007F);
	}
}

bool ConstructStudioDockReplies::RunIsOk(const FCrowdyModRun& Run)
{
	if (Run.Result != ECrowdyModResult::Success || Run.Targets.IsEmpty())
	{
		return false;
	}
	for (const FCrowdyModRunTarget& Target : Run.Targets)
	{
		if (Target.Phase != ECrowdyModRunPhase::Running && Target.Phase != ECrowdyModRunPhase::Deployed)
		{
			return false;
		}
	}
	return true;
}

const FCrowdyModRunTarget* ConstructStudioDockReplies::DeployedClient(const FCrowdyModRun& Run)
{
	return Run.Targets.FindByPredicate([](const FCrowdyModRunTarget& Target)
	{
		return Target.Target == ECrowdyStudioTarget::Client && (Target.Phase == ECrowdyModRunPhase::Running || Target.Phase == ECrowdyModRunPhase::Deployed);
	});
}

const FCrowdyModRunTarget* ConstructStudioDockReplies::DeployedServer(const FCrowdyModRun& Run)
{
	return Run.Targets.FindByPredicate([](const FCrowdyModRunTarget& Target)
	{
		return Target.Target == ECrowdyStudioTarget::Server && Target.Phase == ECrowdyModRunPhase::Deployed;
	});
}

const TCHAR* ConstructStudioDockReplies::TargetName(ECrowdyStudioTarget Target)
{
	return Target == ECrowdyStudioTarget::Client ? TEXT("CLIENT") : TEXT("SERVER");
}

const TCHAR* ConstructStudioDockReplies::PhaseName(ECrowdyModRunPhase Phase)
{
	switch (Phase)
	{
	case ECrowdyModRunPhase::Building: return TEXT("COMPILING");
	case ECrowdyModRunPhase::BuildFailed: return TEXT("COMPILE_FAILED");
	case ECrowdyModRunPhase::Deploying: return TEXT("ENABLING");
	case ECrowdyModRunPhase::DeployFailed: return TEXT("ERROR");
	case ECrowdyModRunPhase::Deployed: return TEXT("STOPPED");
	case ECrowdyModRunPhase::Running: return TEXT("RUNNING");
	default: return TEXT("IDLE");
	}
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::RuntimeReply(const TArray<FCrowdyModRunTarget>& Targets)
{
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetArrayField(TEXT("runtime"), ConstructStudioDockReplyParts::Runtime(Targets));
	return Out;
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::BuildResult(const FCrowdyModRun& Run)
{
	const bool bLive = Run.Mode == ECrowdyStudioRunMode::Live;
	TArray<TSharedPtr<FJsonValue>> Diagnostics;
	const int32 Shown = FMath::Min(Run.Diagnostics.Num(), 200);
	for (int32 Index = 0; Index < Shown; ++Index)
	{
		Diagnostics.Add(MakeShared<FJsonValueObject>(ConstructStudioDockReplyParts::DiagnosticOf(Run.Diagnostics[Index])));
	}
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), RunIsOk(Run));
	Out->SetStringField(TEXT("mode"), bLive ? TEXT("live") : TEXT("draft"));
	Out->SetArrayField(TEXT("diagnostics"), Diagnostics);
	Out->SetStringField(TEXT("buildLog"), Run.BuildLog.Right(MaxBuildLogChars));
	Out->SetArrayField(TEXT("runtime"), ConstructStudioDockReplyParts::Runtime(Run.Targets));
	if (Run.Result == ECrowdyModResult::Success)
	{
		Settle(*Out);
		return Out;
	}
	const TCHAR* NotDone = bLive ? TEXT("The live deploy did not finish; see the diagnostics and runtime.")
		: TEXT("The draft test did not finish; see the diagnostics and runtime.");
	Out->SetStringField(TEXT("summary"), Run.Message.IsEmpty() ? FString(NotDone) : Run.Message.Left(2000));
	return Out;
}

bool ConstructStudioDockReplies::IsOk(const FJsonObject& Result)
{
	bool bOk = false;
	return Result.TryGetBoolField(TEXT("ok"), bOk) && bOk;
}

bool ConstructStudioDockReplies::IsHalfRunning(const FJsonObject& Result, ECrowdyStudioTarget Target)
{
	const TSharedPtr<FJsonObject> Entry = ConstructStudioDockReplyParts::EntryFor(Result, Target);
	return Entry.IsValid() && ConstructStudioDockReplyParts::PhaseIs(*Entry, ECrowdyModRunPhase::Running);
}

void ConstructStudioDockReplies::ApplyServerMod(FJsonObject& Result, const FCrowdyMod& Mod)
{
	const TSharedPtr<FJsonObject> Server = ConstructStudioDockReplyParts::EntryFor(Result, ECrowdyStudioTarget::Server);
	if (!Server.IsValid())
	{
		return;
	}
	// A switched-on mod that is not blocked starts on its first call, so the Studio calls it running.
	if (Mod.bEnabled && !Mod.bBlocked)
	{
		Server->SetStringField(TEXT("phase"), PhaseName(ECrowdyModRunPhase::Running));
		return;
	}
	Result.SetBoolField(TEXT("ok"), false);
	Server->SetStringField(TEXT("message"), Mod.bBlocked ? FString::Printf(TEXT("Deployed, but blocked: %s"), *Mod.BlockedReason.Left(500))
		: FString(TEXT("Deployed, but switched off.")));
}

void ConstructStudioDockReplies::MarkClientRunning(FJsonObject& Result)
{
	const TSharedPtr<FJsonObject> Client = ConstructStudioDockReplyParts::EntryFor(Result, ECrowdyStudioTarget::Client);
	if (!Client.IsValid())
	{
		return;
	}
	Client->SetStringField(TEXT("phase"), PhaseName(ECrowdyModRunPhase::Running));
	Client->RemoveField(TEXT("message"));
}

void ConstructStudioDockReplies::MarkClientNotStarted(FJsonObject& Result, const FString& Why, bool bFailed)
{
	const TSharedPtr<FJsonObject> Client = ConstructStudioDockReplyParts::EntryFor(Result, ECrowdyStudioTarget::Client);
	if (!Client.IsValid())
	{
		return;
	}
	// A half that has not started yet may still be waiting for the player's trust prompt or the next listing.
	if (bFailed)
	{
		Result.SetBoolField(TEXT("ok"), false);
	}
	Client->SetStringField(TEXT("phase"), PhaseName(ECrowdyModRunPhase::NotStarted));
	Client->SetStringField(TEXT("message"), Why.Left(1000));
}

void ConstructStudioDockReplies::Settle(FJsonObject& Result)
{
	FString Mode;
	Result.TryGetStringField(TEXT("mode"), Mode);
	const TCHAR* What = Mode.Equals(TEXT("live"), ESearchCase::CaseSensitive) ? TEXT("The live deploy") : TEXT("The draft test");
	bool bAllRunning = true;
	FString Why;
	for (const TSharedPtr<FJsonObject>& Entry : ConstructStudioDockReplyParts::EntriesOf(Result))
	{
		FString Message;
		if (ConstructStudioDockReplyParts::PhaseIs(*Entry, ECrowdyModRunPhase::Running))
		{
			continue;
		}
		bAllRunning = false;
		if (Why.IsEmpty() && Entry->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
		{
			Why = FString::Printf(TEXT(" %s: %s"), *Entry->GetStringField(TEXT("target")), *Message);
		}
	}
	FString Summary;
	if (!IsOk(Result))
	{
		Summary = FString::Printf(TEXT("%s did not start every half; see the diagnostics and runtime.%s"), What, *Why);
	}
	else if (bAllRunning)
	{
		Summary = FString::Printf(TEXT("%s is running."), What);
	}
	else
	{
		Summary = FString::Printf(TEXT("%s deployed every half, but this game could not confirm that each one started; see the runtime.%s"), What, *Why);
	}
	Result.SetStringField(TEXT("summary"), Summary);
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::Screenshot(const FConstructStudioCapture& Capture, int32 Number)
{
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("name"), FString::Printf(TEXT("capture-%04d.png"), Number));
	Out->SetStringField(TEXT("mediaType"), TEXT("image/png"));
	Out->SetStringField(TEXT("bytesBase64"), Capture.PngBase64);
	Out->SetNumberField(TEXT("width"), Capture.Width);
	Out->SetNumberField(TEXT("height"), Capture.Height);
	ConstructStudioDockReplyParts::SetIfAny(*Out, TEXT("caption"), Capture.Caption.Left(200));
	return Out;
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::ProjectSummary(const FCrowdyStudioProject& Project)
{
	const TCHAR* Kind = Project.Kind == ECrowdyStudioProjectKind::Server ? TEXT("SERVER")
		: Project.Kind == ECrowdyStudioProjectKind::Client ? TEXT("CLIENT")
		: TEXT("FULL_STACK");
	const TSharedRef<FJsonObject> Out = ProjectEvent(Project);
	Out->SetStringField(TEXT("name"), Project.Name);
	Out->SetStringField(TEXT("kind"), Kind);
	Out->SetStringField(TEXT("updatedAt"), Iso8601(Project.UpdatedAtMs));
	ConstructStudioDockReplyParts::SetIfAny(*Out, TEXT("github"), Project.GitHubRef);
	return Out;
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::ProjectEvent(const FCrowdyStudioProject& Project)
{
	const bool bGitHub = Project.Source == ECrowdyStudioProjectSource::GitHub;
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("projectId"), Project.ProjectId);
	Out->SetStringField(TEXT("source"), bGitHub ? TEXT("GITHUB") : TEXT("STUDIO"));
	if (bGitHub && !Project.GitHubSha.IsEmpty())
	{
		Out->SetStringField(TEXT("githubSha"), Project.GitHubSha);
	}
	else
	{
		Out->SetField(TEXT("githubSha"), MakeShared<FJsonValueNull>());
	}
	return Out;
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::ProjectReply(const FCrowdyStudioProject& Project)
{
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetObjectField(TEXT("project"), ProjectSummary(Project));
	return Out;
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::ProjectList(const TArray<FCrowdyStudioProject>& Projects, const FString& CurrentProjectId, bool bMore)
{
	TArray<TSharedPtr<FJsonValue>> Listed;
	for (const FCrowdyStudioProject& Project : Projects)
	{
		Listed.Add(MakeShared<FJsonValueObject>(ProjectSummary(Project)));
	}
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetArrayField(TEXT("projects"), Listed);
	Out->SetBoolField(TEXT("more"), bMore);
	if (CurrentProjectId.IsEmpty())
	{
		Out->SetField(TEXT("currentProjectId"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Out->SetStringField(TEXT("currentProjectId"), CurrentProjectId);
	}
	return Out;
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::ModLog(const TArray<FCrowdyModLogLine>& Lines, const FString& Module, int32 Limit)
{
	TArray<FString> All;
	All.Reserve(Lines.Num());
	for (const FCrowdyModLogLine& Line : Lines)
	{
		All.Add(ConstructStudioDockReplyParts::LogLine(Line.AtMs, Module, Line.Level, Line.Text));
	}
	return ConstructStudioDockReplyParts::Lines(All, Limit, false);
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::ScriptLog(const FCrowdyModScriptLog& Log, int32 Limit)
{
	TArray<FString> All;
	All.Reserve(Log.Lines.Num());
	for (const FCrowdyModScriptLogLine& Line : Log.Lines)
	{
		All.Add(ConstructStudioDockReplyParts::LogLine(Line.AtMs, Log.Name, Line.Level, Line.Text));
	}
	return ConstructStudioDockReplyParts::Lines(All, Limit, Log.DroppedLines > 0);
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::NoLines()
{
	return ConstructStudioDockReplyParts::Lines(TArray<FString>(), 1, false);
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::GridReply(int64 AppId, const FCrowdyChunkClaimInfo& Claim, const TArray<FCrowdyChannel>& Channels)
{
	const TSharedRef<FJsonObject> Chunk = MakeShared<FJsonObject>();
	Chunk->SetStringField(TEXT("x"), LexToString(Claim.ChunkX));
	Chunk->SetStringField(TEXT("y"), LexToString(Claim.ChunkY));
	Chunk->SetStringField(TEXT("z"), LexToString(Claim.ChunkZ));
	TArray<TSharedPtr<FJsonValue>> Listed;
	const int32 Shown = FMath::Min(Channels.Num(), 100);
	for (int32 Index = 0; Index < Shown; ++Index)
	{
		const TSharedRef<FJsonObject> Channel = MakeShared<FJsonObject>();
		Channel->SetStringField(TEXT("groupId"), LexToString(Channels[Index].ChannelId));
		Channel->SetStringField(TEXT("name"), Channels[Index].Name.Left(128));
		Listed.Add(MakeShared<FJsonValueObject>(Channel));
	}
	// A claim is one chunk, so its box's low and high corners are that chunk.
	const TSharedRef<FJsonObject> Grid = MakeShared<FJsonObject>();
	Grid->SetStringField(TEXT("appId"), LexToString(AppId));
	Grid->SetStringField(TEXT("gridId"), LexToString(Claim.GridId));
	Grid->SetObjectField(TEXT("low"), Chunk);
	Grid->SetObjectField(TEXT("high"), Chunk);
	Grid->SetBoolField(TEXT("owned"), Claim.bOwnedByMe);
	Grid->SetArrayField(TEXT("channels"), Listed);
	Grid->SetArrayField(TEXT("sessions"), TArray<TSharedPtr<FJsonValue>>());
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetObjectField(TEXT("grid"), Grid);
	return Out;
}

TSharedRef<FJsonObject> ConstructStudioDockReplies::NoGridReply()
{
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetField(TEXT("grid"), MakeShared<FJsonValueNull>());
	return Out;
}

ECrowdyStudioProjectKind ConstructStudioDockReplies::KindForTemplate(const FString& Template)
{
	if (Template.Equals(TEXT("server"), ESearchCase::CaseSensitive))
	{
		return ECrowdyStudioProjectKind::Server;
	}
	if (Template.Equals(TEXT("client"), ESearchCase::CaseSensitive))
	{
		return ECrowdyStudioProjectKind::Client;
	}
	return ECrowdyStudioProjectKind::Paired;
}

bool ConstructStudioDockReplies::ParamString(const TSharedPtr<FJsonObject>& Params, const TCHAR* Name, int32 MaxChars, FString& Out)
{
	const TSharedPtr<FJsonValue> Value = Params.IsValid() ? Params->TryGetField(Name) : nullptr;
	if (!Value.IsValid() || Value->Type != EJson::String)
	{
		return false;
	}
	Out = Value->AsString().Left(MaxChars);
	return true;
}

int32 ConstructStudioDockReplies::ParamLimit(const TSharedPtr<FJsonObject>& Params, int32 Fallback)
{
	const TSharedPtr<FJsonValue> Value = Params.IsValid() ? Params->TryGetField(TEXT("limit")) : nullptr;
	if (!Value.IsValid() || Value->Type != EJson::Number || !FMath::IsFinite(Value->AsNumber()))
	{
		return Fallback;
	}
	return FMath::Clamp(FMath::FloorToInt32(FMath::Clamp(Value->AsNumber(), 1.0, 400.0)), 1, 400);
}

bool ConstructStudioDockReplies::IsReadablePath(const FString& Path)
{
	if (Path.Equals(TEXT("Cargo.toml"), ESearchCase::CaseSensitive) || Path.Equals(TEXT("README.md"), ESearchCase::CaseSensitive))
	{
		return true;
	}
	if (!Path.StartsWith(TEXT("src/"), ESearchCase::CaseSensitive) || !Path.EndsWith(TEXT(".rs"), ESearchCase::CaseSensitive) || Path.Len() <= 7)
	{
		return false;
	}
	const FString Body = Path.Mid(4, Path.Len() - 7);
	if (Body.StartsWith(TEXT("/")) || Body.EndsWith(TEXT("/")) || Body.Contains(TEXT("//")))
	{
		return false;
	}
	for (const TCHAR Char : Body)
	{
		const bool bAllowed = (Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('A') && Char <= TEXT('Z')) || (Char >= TEXT('0') && Char <= TEXT('9'))
			|| Char == TEXT('_') || Char == TEXT('-') || Char == TEXT('/');
		if (!bAllowed)
		{
			return false;
		}
	}
	return true;
}

FString ConstructStudioDockReplies::PlainText(const FString& Text)
{
	FString Out;
	Out.Reserve(Text.Len());
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		const uint32 Code = static_cast<uint32>(Text[Index]);
		// A UTF-16 string carries the invisible tag characters, U+E0000 to U+E007F, as this surrogate pair.
		const bool bTagPair = Code == 0xDB40 && Index + 1 < Text.Len() && static_cast<uint32>(Text[Index + 1]) >= 0xDC00 && static_cast<uint32>(Text[Index + 1]) <= 0xDC7F;
		if (bTagPair)
		{
			++Index;
			continue;
		}
		if (!ConstructStudioDockReplyParts::IsHiddenChar(Code))
		{
			Out.AppendChar(Text[Index]);
		}
	}
	return Out;
}

FString ConstructStudioDockReplies::Iso8601(int64 UnixMs)
{
	return (FDateTime(1970, 1, 1) + FTimespan::FromMilliseconds(static_cast<double>(UnixMs))).ToIso8601();
}
