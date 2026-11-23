#include "ConstructLayer10Check.h"

#include "ConstructHarnessLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdyModScripts.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
	// One 10 s relisting of the grid's Mod Scripts, plus margin.
	constexpr double Layer10ControlSettleSeconds = 12.0;
	constexpr double Layer10PromptWaitSeconds = 60.0;
	constexpr double Layer10GridWaitSeconds = 120.0;
	constexpr int32 Layer10RunIdLength = 12;

	// Joined without spaces, so a list never reads as the line's own ", " separators.
	FString Layer10JoinOrDash(const TArray<FString>& Items)
	{
		return Items.IsEmpty() ? FString(TEXT("-")) : FString::Join(Items, TEXT(","));
	}
}

bool AConstructLayer10CheckPawn::ShouldUseLocalCharacterMovement() const
{
	return false;
}

AConstructLayer10CheckGameMode::AConstructLayer10CheckGameMode()
{
	DefaultPawnClass = AConstructLayer10CheckPawn::StaticClass();
}

void FConstructLayer10FrameTracker::Reset()
{
	LastSeconds = -1.0;
	LongestMs = 0.0;
}

void FConstructLayer10FrameTracker::Add(const double NowSeconds)
{
	const double Last = LastSeconds;
	LastSeconds = NowSeconds;
	if (Last < 0.0)
	{
		return;
	}
	LongestMs = FMath::Max(LongestMs, (NowSeconds - Last) * 1000.0);
}

void UConstructLayer10CheckHost::DrainClicks_Implementation(FCrowdyModScriptClicks& Clicks)
{
	Clicks = FCrowdyModScriptClicks();
}

bool UConstructLayer10CheckHost::ReadChunk_Implementation(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FCrowdyModScriptChunk& Chunk)
{
	return false;
}

void UConstructLayer10CheckHost::ListPlayers_Implementation(const FCrowdyModScriptGrid& Box, TArray<FCrowdyModScriptPlayer>& Players)
{
	Players.Reset();
}

bool UConstructLayer10CheckHost::SetVoxel_Implementation(const FCrowdyModScript& Script, const FCrowdyVoxelWrite& Write, FString& Refusal)
{
	Refusal = TEXT("not offered by the check");
	return false;
}

void UConstructLayer10CheckHost::ShowHud_Implementation(const FCrowdyModScript& Script, const FString& Text)
{
	if (UConstructLayer10CheckSubsystem::IsMarkerText(Text, Marker))
	{
		++HudMarker;
		return;
	}
	++HudOther;
}

void UConstructLayer10CheckHost::DrawOverlay_Implementation(const FCrowdyModScript& Script, const TArray<FCrowdyModScriptShape>& Shapes)
{
	++Overlays;
}

void UConstructLayer10CheckHost::ClearScript_Implementation(const FCrowdyModScript& Script)
{
	++Clears;
}

bool UConstructLayer10CheckSubsystem::ParseRun(const FString& Text, EConstructLayer10Run& OutRun)
{
	for (const EConstructLayer10Run Candidate : {EConstructLayer10Run::Control, EConstructLayer10Run::Declined, EConstructLayer10Run::Accepted})
	{
		if (Text.Equals(RunName(Candidate), ESearchCase::CaseSensitive))
		{
			OutRun = Candidate;
			return true;
		}
	}
	return false;
}

const TCHAR* UConstructLayer10CheckSubsystem::RunName(const EConstructLayer10Run Run)
{
	switch (Run)
	{
	case EConstructLayer10Run::Declined:
		return TEXT("declined");
	case EConstructLayer10Run::Accepted:
		return TEXT("accepted");
	default:
		return TEXT("control");
	}
}

bool UConstructLayer10CheckSubsystem::IsValidRunId(const FString& RunId)
{
	if (RunId.Len() != Layer10RunIdLength)
	{
		return false;
	}
	for (const TCHAR Char : RunId)
	{
		const bool bDigit = Char >= TEXT('0') && Char <= TEXT('9');
		const bool bLetter = Char >= TEXT('a') && Char <= TEXT('f');
		if (!bDigit && !bLetter)
		{
			return false;
		}
	}
	return true;
}

FString UConstructLayer10CheckSubsystem::MarkerFor(const FString& RunId)
{
	return FString::Printf(TEXT("layer10 marker %s"), *RunId);
}

bool UConstructLayer10CheckSubsystem::IsMarkerText(const FString& Text, const FString& Marker)
{
	return !Marker.IsEmpty() && Text.Equals(Marker, ESearchCase::CaseSensitive);
}

bool UConstructLayer10CheckSubsystem::GridContainsCheckChunk(const FCrowdyModScriptGrid& Grid)
{
	return Grid.GridId != 0 && CheckChunkX >= Grid.LowChunkX && CheckChunkX <= Grid.HighChunkX && CheckChunkY >= Grid.LowChunkY &&
	       CheckChunkY <= Grid.HighChunkY && CheckChunkZ >= Grid.LowChunkZ && CheckChunkZ <= Grid.HighChunkZ;
}

FVector UConstructLayer10CheckSubsystem::SpotFor(const double ChunkSize)
{
	return FVector(CheckChunkX + 0.5, CheckChunkY + 0.5, CheckChunkZ + 0.5) * ChunkSize;
}

FString UConstructLayer10CheckSubsystem::StopReasonName(const ECrowdyModScriptStop Reason)
{
	return StaticEnum<ECrowdyModScriptStop>()->GetNameStringByValue(static_cast<int64>(Reason));
}

FString UConstructLayer10CheckSubsystem::FormatPromptLine(const FCrowdyModScriptPrompt& Prompt)
{
	TArray<FString> Groups;
	for (const ECrowdyModScriptGroup Group : Prompt.Groups)
	{
		Groups.Add(StaticEnum<ECrowdyModScriptGroup>()->GetNameStringByValue(static_cast<int64>(Group)));
	}
	return FString::Printf(TEXT("prompt: scripts %d, calls %s, groups %s, not offered %s"), Prompt.Scripts.Num(),
	                       *Layer10JoinOrDash(Prompt.Calls), *Layer10JoinOrDash(Groups), *Layer10JoinOrDash(Prompt.NotOffered));
}

FString UConstructLayer10CheckSubsystem::FormatSummaryLine(const double LongestFrameMs, const FConstructLayer10Counts& Counts)
{
	return FString::Printf(TEXT("measured %.0f s: longest frame %.1f ms, started %d, stopped %d, hud marker %d, hud other %d, ")
	                       TEXT("overlays %d, clears %d, errors %d, prompts %d"),
	                       MeasureSeconds, LongestFrameMs, Counts.Started, Counts.Stopped, Counts.HudMarker, Counts.HudOther,
	                       Counts.Overlays, Counts.Clears, Counts.Errors, Counts.Prompts);
}

bool UConstructLayer10CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* World = Cast<UWorld>(Outer);
	if (!Super::ShouldCreateSubsystem(Outer) || !World || World->WorldType != EWorldType::Game)
	{
		return false;
	}
	FString RunText;
	return FParse::Value(FCommandLine::Get(), TEXT("Layer10CheckRun="), RunText);
}

void UConstructLayer10CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UGameInstance* GameInstance = InWorld.GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	FString RunText;
	if (!FParse::Value(FCommandLine::Get(), TEXT("Layer10CheckRun="), RunText))
	{
		return;
	}
	// Started for the check but in another world, as when the build did not cook L_Layer10Check.
	if (!Cast<AConstructLayer10CheckGameMode>(InWorld.GetAuthGameMode()))
	{
		Fail(FString::Printf(TEXT("the build loaded %s without the layer 10 check GameMode"), *InWorld.GetMapName()));
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	double TimeoutMinutes = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("Layer10CheckTimeoutMinutes="), TimeoutMinutes) && TimeoutMinutes > 0.0)
	{
		TimeoutSeconds = TimeoutMinutes * 60.0;
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);

	if (!ParseRun(RunText, Run))
	{
		Fail(TEXT("-Layer10CheckRun= is not control, declined or accepted; start the check with Tools/Layer10Check"));
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: run %s"), RunName(Run));

	FString RunId;
	if (!FParse::Value(FCommandLine::Get(), TEXT("Layer10CheckRunId="), RunId) || !IsValidRunId(RunId))
	{
		Fail(TEXT("-Layer10CheckRunId= is not 12 lowercase hex digits; start the check with Tools/Layer10Check"));
		return;
	}
	UCrowdyModScripts* Scripts = InWorld.GetSubsystem<UCrowdyModScripts>();
	if (!Scripts)
	{
		Fail(TEXT("the Mod Scripts subsystem is missing"));
		return;
	}
	// Set here rather than in a config file, so only the check's process writes the per-dispatch lines.
	IConsoleVariable* Trace = IConsoleManager::Get().FindConsoleVariable(TEXT("crowdy.modscripts.trace"));
	if (!Trace)
	{
		Fail(TEXT("the console variable crowdy.modscripts.trace is missing"));
		return;
	}
	Trace->Set(1, ECVF_SetByCode);

	ModScripts = Scripts;
	Host = NewObject<UConstructLayer10CheckHost>(this);
	Host->Marker = MarkerFor(RunId);
	Scripts->OnModScriptPrompt.AddUniqueDynamic(this, &ThisClass::HandlePrompt);
	Scripts->OnModScriptPromptWithdrawn.AddUniqueDynamic(this, &ThisClass::HandlePromptWithdrawn);
	Scripts->OnModScriptStarted.AddUniqueDynamic(this, &ThisClass::HandleStarted);
	Scripts->OnModScriptStopped.AddUniqueDynamic(this, &ThisClass::HandleStopped);
	Scripts->OnModScriptError.AddUniqueDynamic(this, &ThisClass::HandleError);
}

void UConstructLayer10CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0.
	if (TickerHandle.IsValid() && Stage != EStage::Done)
	{
		Fail(TEXT("the check's world ended before it finished"));
	}
	if (UCrowdyModScripts* Scripts = ModScripts.Get())
	{
		if (Host)
		{
			Scripts->ClearHost(TScriptInterface<ICrowdyModScriptHost>(Host.Get()));
		}
		Scripts->OnModScriptPrompt.RemoveDynamic(this, &ThisClass::HandlePrompt);
		Scripts->OnModScriptPromptWithdrawn.RemoveDynamic(this, &ThisClass::HandlePromptWithdrawn);
		Scripts->OnModScriptStarted.RemoveDynamic(this, &ThisClass::HandleStarted);
		Scripts->OnModScriptStopped.RemoveDynamic(this, &ThisClass::HandleStopped);
		Scripts->OnModScriptError.RemoveDynamic(this, &ThisClass::HandleError);
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer10CheckSubsystem::Tick(float DeltaTime)
{
	if (!IsLogging())
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out after %.0f minutes in stage %d"), TimeoutSeconds / 60.0, static_cast<int32>(Stage)));
		return false;
	}
	if (!ModScripts.IsValid())
	{
		Fail(TEXT("the Mod Scripts subsystem was lost"));
		return false;
	}
	switch (Stage)
	{
	case EStage::WaitEntry:
		TickEntry(Now);
		break;
	case EStage::WaitGrid:
		TickGrid(Now);
		break;
	case EStage::Settle:
		TickSettle(Now);
		break;
	case EStage::WaitPrompt:
		TickWaitPrompt(Now);
		break;
	case EStage::Measure:
		TickMeasure(Now);
		break;
	default:
		break;
	}
	return IsLogging();
}

void UConstructLayer10CheckSubsystem::TickEntry(const double Now)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = GameInstance ? GameInstance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = GameInstance ? GameInstance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	if (!Auth || !SDK)
	{
		Fail(TEXT("the Crowdy SDK subsystems are missing"));
		return;
	}
	if (!FindLocalPawn() || !Auth->IsSignedIn() || SDK->GetUDPConnectionState() != EUDPConnectionState::Connected)
	{
		return;
	}
	if (!MoveToCheckChunk())
	{
		return;
	}
	UCrowdyModScripts* Scripts = ModScripts.Get();
	if (!Scripts->IsAvailable())
	{
		Fail(TEXT("Mod Scripts are not available in this build"));
		return;
	}
	Scripts->SetHost(TScriptInterface<ICrowdyModScriptHost>(Host.Get()));
	Scripts->EnterGrid(CheckChunkX, CheckChunkY, CheckChunkZ);
	EnterGridAt = Now;
	Stage = EStage::WaitGrid;
}

void UConstructLayer10CheckSubsystem::TickGrid(const double Now)
{
	UCrowdyModScripts* Scripts = ModScripts.Get();
	if (GridContainsCheckChunk(Scripts->GetGrid()))
	{
		GridAt = Now;
		Stage = Run == EConstructLayer10Run::Control ? EStage::Settle : EStage::WaitPrompt;
		return;
	}
	if (Now - EnterGridAt > Layer10GridWaitSeconds)
	{
		Fail(FString::Printf(TEXT("no grid over chunk M after %.0f s"), Layer10GridWaitSeconds));
		return;
	}
	// The SDK asks again after a failed lookup only when the chunk is named again, and holds each retry itself.
	Scripts->EnterGrid(CheckChunkX, CheckChunkY, CheckChunkZ);
}

void UConstructLayer10CheckSubsystem::TickSettle(const double Now)
{
	if (Counts.Prompts > 0)
	{
		Fail(TEXT("a prompt arrived in the control run"));
		return;
	}
	if (Counts.Started > 0)
	{
		Fail(TEXT("a Mod Script started in the control run"));
		return;
	}
	if (Now - GridAt < Layer10ControlSettleSeconds)
	{
		return;
	}
	BeginMeasure(Now, TEXT("no Mod Script listed"));
}

void UConstructLayer10CheckSubsystem::TickWaitPrompt(const double Now)
{
	if (!bHasPendingPrompt)
	{
		if (Now - GridAt > Layer10PromptWaitSeconds)
		{
			Fail(FString::Printf(TEXT("no prompt within %.0f s of entering grid M"), Layer10PromptWaitSeconds));
		}
		return;
	}
	// Every run measures from the same time after entering the grid, so start-up work weighs on each alike.
	if (Now - GridAt < Layer10ControlSettleSeconds)
	{
		return;
	}
	// Answered here, never inside the prompt's own broadcast.
	bHasPendingPrompt = false;
	BeginMeasure(Now, TEXT("prompt shown"));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: %s"), *FormatPromptLine(PendingPrompt));
	const bool bRun = Run == EConstructLayer10Run::Accepted;
	ModScripts->AnswerPrompt(PendingPrompt.PromptId, bRun);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: answered %s"), bRun ? TEXT("Run") : TEXT("Not now"));
}

void UConstructLayer10CheckSubsystem::BeginMeasure(const double Now, const TCHAR* Stimulus)
{
	if (!IsSignedInAndConnected())
	{
		Fail(TEXT("signed out or disconnected before measuring"));
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: stimulus landed: signed in, connected, in grid M, %s"), Stimulus);
	Frames.Reset();
	Frames.Add(Now);
	MeasureStartedAt = Now;
	Stage = EStage::Measure;
}

void UConstructLayer10CheckSubsystem::TickMeasure(const double Now)
{
	Frames.Add(Now);
	if (Now - MeasureStartedAt < MeasureSeconds)
	{
		return;
	}
	if (!IsSignedInAndConnected())
	{
		Fail(TEXT("signed out or disconnected while measuring"));
		return;
	}
	FConstructLayer10Counts Summary = Counts;
	Summary.HudMarker = Host->HudMarker;
	Summary.HudOther = Host->HudOther;
	Summary.Overlays = Host->Overlays;
	Summary.Clears = Host->Clears;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: %s"), *FormatSummaryLine(Frames.GetLongestMs(), Summary));
	Stage = EStage::Done;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: done"));
	FPlatformMisc::RequestExitWithStatus(true, 0);
}

bool UConstructLayer10CheckSubsystem::MoveToCheckChunk()
{
	AConstructLayer10CheckPawn* Pawn = FindLocalPawn();
	const UCrowdyGameSession* Session = GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyGameSession>();
	const double ChunkSize = Session ? Session->GetChunkSize() : 0.0;
	if (!Pawn || ChunkSize <= 0.0)
	{
		Fail(TEXT("the pawn or the session's chunk size is missing"));
		return false;
	}
	FInt64Vector Chunk(0, 0, 0);
	const bool bPlaced = Pawn->SetActorLocation(SpotFor(ChunkSize)) && Session->TryGetLocalPlayerChunk(GetWorld(), Chunk);
	if (!bPlaced || Chunk != FInt64Vector(CheckChunkX, CheckChunkY, CheckChunkZ))
	{
		Fail(TEXT("the pawn could not be placed in chunk M"));
		return false;
	}
	return true;
}

AConstructLayer10CheckPawn* UConstructLayer10CheckSubsystem::FindLocalPawn() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AConstructLayer10CheckPawn* Pawn = Controller ? Cast<AConstructLayer10CheckPawn>(Controller->GetPawn()) : nullptr;
	const UCrowdyEntityComponent* Component = Pawn ? Pawn->FindComponentByClass<UCrowdyEntityComponent>() : nullptr;
	return Component && Component->GetRole() == ECrowdyRole::Owner ? Pawn : nullptr;
}

bool UConstructLayer10CheckSubsystem::IsSignedInAndConnected() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = GameInstance ? GameInstance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = GameInstance ? GameInstance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	return Auth && SDK && Auth->IsSignedIn() && SDK->GetUDPConnectionState() == EUDPConnectionState::Connected;
}

bool UConstructLayer10CheckSubsystem::IsLogging() const
{
	return Stage != EStage::Done && Stage != EStage::Failed;
}

void UConstructLayer10CheckSubsystem::HandlePrompt(const FCrowdyModScriptPrompt& Prompt)
{
	if (!IsLogging())
	{
		return;
	}
	++Counts.Prompts;
	if (Stage == EStage::WaitGrid || Stage == EStage::WaitPrompt)
	{
		PendingPrompt = Prompt;
		bHasPendingPrompt = true;
	}
}

void UConstructLayer10CheckSubsystem::HandlePromptWithdrawn(const int32 PromptId)
{
	if (!IsLogging() || !bHasPendingPrompt || PendingPrompt.PromptId != PromptId)
	{
		return;
	}
	bHasPendingPrompt = false;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: prompt withdrawn before it was answered"));
}

void UConstructLayer10CheckSubsystem::HandleStarted(const FCrowdyModScript& Script)
{
	if (!IsLogging())
	{
		return;
	}
	++Counts.Started;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: started %s"), *Script.Name);
}

void UConstructLayer10CheckSubsystem::HandleStopped(const FCrowdyModScript& Script, const ECrowdyModScriptStop Reason)
{
	if (!IsLogging())
	{
		return;
	}
	++Counts.Stopped;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: stopped %s %s"), *Script.Name, *StopReasonName(Reason));
}

void UConstructLayer10CheckSubsystem::HandleError(const FCrowdyModScriptError& Error)
{
	if (!IsLogging())
	{
		return;
	}
	++Counts.Errors;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer10Check: error %s"), *Error.Code);
}

void UConstructLayer10CheckSubsystem::Fail(const FString& Why)
{
	if (!IsLogging())
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer10Check: FAILED: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
