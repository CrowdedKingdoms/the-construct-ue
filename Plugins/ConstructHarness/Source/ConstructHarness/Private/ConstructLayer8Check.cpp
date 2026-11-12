#include "ConstructLayer8Check.h"

#include "ConstructChunkView.h"
#include "ConstructClaimPad.h"
#include "ConstructHarnessDigest.h"
#include "ConstructHarnessLog.h"
#include "ConstructModPanel.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "Subsystem/CrowdyVoxelWorld.h"

namespace
{
	const TCHAR* const Layer8MapName = TEXT("L_Layer8Check");
	const TCHAR* const Layer8Starter = TEXT("construct-spinning-child");
	constexpr double Layer8PollSeconds = 0.05;
	constexpr double Layer8TimeoutSeconds = 40.0 * 60.0;
	// A claim covers the platform call, the hub call with its retries, and the pad's refresh after.
	constexpr double Layer8ClaimSeconds = 60.0;
	constexpr double Layer8ConfirmSeconds = 30.0;
	// How long a panel instruction waits for the panel to offer it, past a status read that briefly shows an error.
	constexpr double Layer8ReadySeconds = 15.0;
	// Build Mod gives up after 11 minutes and answers then.
	constexpr double Layer8BuildSeconds = 12.0 * 60.0;
	constexpr double Layer8AnswerSeconds = 60.0;
	constexpr double Layer8SwitchSeconds = 30.0;
	constexpr double Layer8ReloadSeconds = 30.0;

	// The chunk reserved for this check; Tools/Layer8Check reads and cleans the same one.
	const FInt64Vector Layer8Chunk(38, -64, 0);

	FString Layer8Line(const TSharedRef<FJsonObject>& Object)
	{
		FString Line;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		FJsonSerializer::Serialize(Object, Writer);
		return Line;
	}

	template <typename TEnum>
	FString Layer8Name(const TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	TArray<TSharedPtr<FJsonValue>> Layer8Cells(const TArray<FIntVector>& Cells)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FIntVector& Cell : Cells)
		{
			Values.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%d %d %d"), Cell.X, Cell.Y, Cell.Z)));
		}
		return Values;
	}

	bool Layer8IsPanelLine(const FString& Line)
	{
		return Line == TEXT("build") || Line == TEXT("deploy") || Line == TEXT("on") || Line == TEXT("off") || Line == TEXT("log");
	}
}

TArray<FString> UConstructLayer8CheckSubsystem::CompleteLines(const FString& Text, const int32 FirstNew)
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

bool UConstructLayer8CheckSubsystem::IsReadyFor(const FString& Instruction, const EConstructModPanelState State)
{
	using EState = EConstructModPanelState;
	if (Instruction == TEXT("build"))
	{
		return State == EState::NoMod;
	}
	if (Instruction == TEXT("deploy"))
	{
		return State == EState::Built;
	}
	if (Instruction == TEXT("on"))
	{
		return State == EState::Off;
	}
	if (Instruction == TEXT("off"))
	{
		return State == EState::On || State == EState::Blocked;
	}
	if (Instruction == TEXT("log"))
	{
		return State == EState::Built || State == EState::Off || State == EState::On;
	}
	if (Instruction == TEXT("delete"))
	{
		return State == EState::Off || State == EState::Blocked;
	}
	return true;
}

TArray<FIntVector> UConstructLayer8CheckSubsystem::SpinnerCells()
{
	return { FIntVector(8, 0, 8), FIntVector(8, 1, 8), FIntVector(9, 1, 8), FIntVector(8, 1, 9), FIntVector(7, 1, 8), FIntVector(8, 1, 7) };
}

FString UConstructLayer8CheckSubsystem::EnabledLine(const ECrowdyModResult Result, const bool bEnabled, const bool bRunning)
{
	return FString::Printf(TEXT("enabled %s %d running %d"), *Layer8Name(Result), bEnabled ? 1 : 0, bRunning ? 1 : 0);
}

FString UConstructLayer8CheckSubsystem::DeployLine(const ECrowdyModResult Result, const int64 Version, const bool bEnabled)
{
	return FString::Printf(TEXT("deploy %s version %lld enabled %d"), *Layer8Name(Result), Version, bEnabled ? 1 : 0);
}

FString UConstructLayer8CheckSubsystem::CubeLine(const FIntVector& Cell, const bool bDrawn, const int32 Type, const FVector& Centre)
{
	if (!bDrawn)
	{
		return FString::Printf(TEXT("cube %d %d %d missing"), Cell.X, Cell.Y, Cell.Z);
	}
	return FString::Printf(TEXT("cube %d %d %d type %d at %.2f %.2f %.2f"), Cell.X, Cell.Y, Cell.Z, Type, Centre.X, Centre.Y, Centre.Z);
}

FString UConstructLayer8CheckSubsystem::ClaimJson(const ECrowdyClaimResult Result, const int64 GridId)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("event"), TEXT("claim"));
	Object->SetStringField(TEXT("result"), Layer8Name(Result));
	Object->SetStringField(TEXT("grid"), LexToString(GridId));
	return Layer8Line(Object);
}

FString UConstructLayer8CheckSubsystem::BuildJson(const ECrowdyModResult Result, const ECrowdyModBuildStatus Status, const FString& DoneBuildId, const FString& BuildId, const TArray<FConstructModFileHash>& Files)
{
	TArray<TSharedPtr<FJsonValue>> FileValues;
	for (const FConstructModFileHash& File : Files)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), File.Path);
		Entry->SetStringField(TEXT("sha1"), File.Sha1);
		FileValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("event"), TEXT("build"));
	Object->SetStringField(TEXT("result"), Layer8Name(Result));
	Object->SetStringField(TEXT("status"), Layer8Name(Status));
	Object->SetStringField(TEXT("doneBuildId"), DoneBuildId);
	Object->SetStringField(TEXT("buildId"), BuildId);
	Object->SetArrayField(TEXT("files"), FileValues);
	return Layer8Line(Object);
}

FString UConstructLayer8CheckSubsystem::ReloadJson(const ECrowdyVoxelResult Result, const FString& BeforeSha1, const FString& Sha1, const TArray<FIntVector>& ChangedCells, const TArray<FIntVector>& RedrawnCells)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("event"), TEXT("reload"));
	Object->SetStringField(TEXT("result"), Layer8Name(Result));
	Object->SetStringField(TEXT("before"), BeforeSha1);
	Object->SetStringField(TEXT("sha1"), Sha1);
	Object->SetArrayField(TEXT("changed"), Layer8Cells(ChangedCells));
	Object->SetArrayField(TEXT("redrawn"), Layer8Cells(RedrawnCells));
	return Layer8Line(Object);
}

bool UConstructLayer8CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* OuterWorld = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && OuterWorld && OuterWorld->WorldType == EWorldType::Game;
}

void UConstructLayer8CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetGameInstance())
	{
		return;
	}
	const bool bAsked = FParse::Value(FCommandLine::Get(), TEXT("Layer8CheckControl="), ControlPath);
	if (InWorld.GetMapName() != Layer8MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer8Check.
		if (bAsked)
		{
			Fail(FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer8MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);
	// Tools/Layer8Check writes both files before launch, so a path cut short at a space fails here, not at the first line.
	if (!bAsked || ControlPath.IsEmpty() || !FParse::Value(FCommandLine::Get(), TEXT("Layer8CheckResults="), ResultsPath)
		|| !FPaths::FileExists(ControlPath) || !FPaths::FileExists(ResultsPath))
	{
		Fail(TEXT("-Layer8CheckControl and -Layer8CheckResults are needed; start the check with Tools/Layer8Check"));
		return;
	}
	// Taken before any panel opens, since an open panel spawns a chunk view of its own.
	for (TActorIterator<AConstructChunkView> It(&InWorld); It; ++It)
	{
		View = *It;
		break;
	}
	for (TActorIterator<AConstructClaimPad> It(&InWorld); It; ++It)
	{
		Pad = *It;
		break;
	}
	VoxelWorld = InWorld.GetSubsystem<UCrowdyVoxelWorld>();
	AConstructClaimPad* ClaimPad = Pad.Get();
	AConstructChunkView* DrawView = View.Get();
	UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
	if (!ClaimPad || !DrawView || !Voxels)
	{
		Fail(TEXT("the map needs a placed AConstructClaimPad and AConstructChunkView, and the voxel world"));
		return;
	}
	const FInt64Vector PadChunk = ClaimPad->GetClaimChunk();
	if (PadChunk != Layer8Chunk)
	{
		Fail(FString::Printf(TEXT("the pad stands in chunk %lld %lld %lld, not the check's chunk"), PadChunk.X, PadChunk.Y, PadChunk.Z));
		return;
	}
	ClaimPad->OnClaimDone.AddUniqueDynamic(this, &ThisClass::HandleClaimDone);
	ClaimPad->OnReleaseDone.AddUniqueDynamic(this, &ThisClass::HandleReleaseDone);
	Voxels->OnChunkChanged.AddUniqueDynamic(this, &ThisClass::HandleChunkChanged);
	DrawView->OnRedrawn.AddUniqueDynamic(this, &ThisClass::HandleRedrawn);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: started"));
}

void UConstructLayer8CheckSubsystem::Deinitialize()
{
	// A world ending mid-check is a failure, never the graceful exit's 0; Node stops the process, which never gets here.
	if (TickerHandle.IsValid())
	{
		Fail(TEXT("the check's world ended before Node stopped the client"));
	}
	UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
	APawn* Followed = FollowedPawn.Get();
	if (Voxels && Followed)
	{
		Voxels->StopFollowing(Followed);
	}
	FollowedPawn.Reset();
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer8CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Failed)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > Layer8TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out in stage %d, step %d"), static_cast<int32>(Stage), static_cast<int32>(Step)));
		return false;
	}
	UpdateFollow();
	if (Stage == EStage::WaitEntry)
	{
		return TickEntry();
	}
	if (Step != EStep::Idle)
	{
		TickStep();
		return Stage != EStage::Failed;
	}
	if (Now - LastPollAt >= Layer8PollSeconds)
	{
		LastPollAt = Now;
		PollControl();
	}
	return Stage != EStage::Failed;
}

bool UConstructLayer8CheckSubsystem::TickEntry()
{
	UWorld* CurrentWorld = GetWorld();
	UGameInstance* Game = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = Game ? Game->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	const UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
	const AConstructClaimPad* ClaimPad = Pad.Get();
	AConstructChunkView* DrawView = View.Get();
	if (!Auth || !SDK || !Session || !Voxels || !ClaimPad || !DrawView)
	{
		Fail(TEXT("the Crowdy SDK subsystems, the claim pad or the view are missing"));
		return false;
	}
	int64 GridId = 0;
	int64 OwnerUserId = 0;
	bool bOwnedByMe = false;
	FString OwnerName;
	if (!Auth->IsSignedIn() || SDK->GetUDPConnectionState() != EUDPConnectionState::Connected || Session->GetUserID() == 0
		|| !FollowedPawn.IsValid() || !ClaimPad->GetShownOwner(GridId, OwnerUserId, bOwnedByMe, OwnerName)
		|| !Voxels->IsChunkLive(Layer8Chunk.X, Layer8Chunk.Y, Layer8Chunk.Z))
	{
		return true;
	}
	DrawView->ShowLoadedChunks(Layer8Chunk.X, Layer8Chunk.Y, Layer8Chunk.Z);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: stimulus landed: signed in, connected, pad shown, chunk live"));
	Stage = EStage::Running;
	return true;
}

void UConstructLayer8CheckSubsystem::UpdateFollow()
{
	UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
	UWorld* CurrentWorld = GetWorld();
	const APlayerController* Controller = CurrentWorld ? CurrentWorld->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Voxels || Pawn == FollowedPawn.Get())
	{
		return;
	}
	// A destroyed pawn leaves the voxel world's followers by itself; a replaced one still standing is stopped here.
	if (APawn* Previous = FollowedPawn.Get())
	{
		Voxels->StopFollowing(Previous);
	}
	FollowedPawn = Pawn;
	if (Pawn)
	{
		Voxels->Follow(Pawn, 1);
	}
}

void UConstructLayer8CheckSubsystem::TickStep()
{
	if (Step == EStep::AwaitOwned)
	{
		const AConstructClaimPad* ClaimPad = Pad.Get();
		int64 GridId = 0;
		int64 OwnerUserId = 0;
		bool bOwnedByMe = false;
		FString OwnerName;
		if (ClaimPad && ClaimPad->GetShownOwner(GridId, OwnerUserId, bOwnedByMe, OwnerName) && GridId == ClaimedGrid && bOwnedByMe)
		{
			OpenPanel();
			return;
		}
	}
	if (Step == EStep::AwaitNoMod)
	{
		const UConstructModPanel* ModPanel = Panel.Get();
		if (!ModPanel)
		{
			Fail(TEXT("claim: the mod panel closed before it showed No mod"));
			return;
		}
		const EConstructModPanelState State = ModPanel->GetState();
		if (State != EConstructModPanelState::Loading)
		{
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: panel %s"), *Layer8Name(State));
		}
		if (State == EConstructModPanelState::NoMod)
		{
			Finish();
			return;
		}
		if (State != EConstructModPanelState::Loading)
		{
			Fail(FString::Printf(TEXT("claim: the panel shows %s, not No mod"), *Layer8Name(State)));
			return;
		}
	}
	if (Step == EStep::AwaitReady)
	{
		const UConstructModPanel* ModPanel = Panel.Get();
		if (ModPanel && IsReadyFor(PendingLine, ModPanel->GetState()))
		{
			StartPanelStep();
			return;
		}
	}
	if (FPlatformTime::Seconds() > StepDeadline)
	{
		Fail(FString::Printf(TEXT("%s (panel %s)"), *StepWhat, *PanelStateName()));
	}
}

void UConstructLayer8CheckSubsystem::PollControl()
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

void UConstructLayer8CheckSubsystem::Act(const FString& Line)
{
	if (Line == TEXT("claim"))
	{
		StartClaim();
		return;
	}
	if (Line == TEXT("reload"))
	{
		StartReload();
		return;
	}
	if (Line == TEXT("delete"))
	{
		StartDelete();
		return;
	}
	if (Line == TEXT("release"))
	{
		StartRelease();
		return;
	}
	if (!Layer8IsPanelLine(Line))
	{
		Fail(FString::Printf(TEXT("the control line \"%s\" is not one this check reads"), *Line.Left(80)));
		return;
	}
	if (!Panel.IsValid())
	{
		Fail(FString::Printf(TEXT("%s: no mod panel is open"), *Line));
		return;
	}
	PendingLine = Line;
	Begin(EStep::AwaitReady, Layer8ReadySeconds, FString::Printf(TEXT("%s: the panel did not offer it within %.0f s"), *Line, Layer8ReadySeconds));
}

void UConstructLayer8CheckSubsystem::StartClaim()
{
	AConstructClaimPad* ClaimPad = Pad.Get();
	int64 GridId = 0;
	int64 OwnerUserId = 0;
	bool bOwnedByMe = false;
	FString OwnerName;
	if (!ClaimPad || !ClaimPad->GetShownOwner(GridId, OwnerUserId, bOwnedByMe, OwnerName) || GridId != 0)
	{
		Fail(TEXT("claim: the pad does not show the check's chunk free"));
		return;
	}
	Begin(EStep::Claiming, Layer8ClaimSeconds, TEXT("claim: no claim answer within 60 s"));
	ClaimPad->Claim();
}

void UConstructLayer8CheckSubsystem::OpenPanel()
{
	AConstructClaimPad* ClaimPad = Pad.Get();
	UConstructModPanel* Opened = ClaimPad ? ClaimPad->OpenMods() : nullptr;
	if (!Opened)
	{
		Fail(TEXT("claim: the pad's OpenMods returned no panel"));
		return;
	}
	Panel = Opened;
	Opened->OnBuildProgress.AddUniqueDynamic(this, &ThisClass::HandleBuildProgress);
	Opened->OnBuildDone.AddUniqueDynamic(this, &ThisClass::HandleBuildDone);
	Opened->OnDeployed.AddUniqueDynamic(this, &ThisClass::HandleDeployed);
	Opened->OnEnabledChanged.AddUniqueDynamic(this, &ThisClass::HandleEnabledChanged);
	Opened->OnWakeDone.AddUniqueDynamic(this, &ThisClass::HandleWakeDone);
	Opened->OnLogShown.AddUniqueDynamic(this, &ThisClass::HandleLogShown);
	Opened->OnDeleted.AddUniqueDynamic(this, &ThisClass::HandleDeleted);
	Begin(EStep::AwaitNoMod, Layer8ConfirmSeconds, TEXT("claim: the panel did not show No mod within 30 s"));
}

void UConstructLayer8CheckSubsystem::StartPanelStep()
{
	UConstructModPanel* ModPanel = Panel.Get();
	if (!ModPanel)
	{
		Fail(FString::Printf(TEXT("%s: the mod panel closed"), *PendingLine));
		return;
	}
	if (PendingLine == TEXT("build"))
	{
		Begin(EStep::Building, Layer8BuildSeconds, TEXT("build: no build answer within 12 minutes"));
		ModPanel->Build(Layer8Starter);
		// A failed source read answers inside Build; anything else that leaves the panel idle started nothing.
		if (Step == EStep::Building && ModPanel->GetState() != EConstructModPanelState::Building)
		{
			Fail(TEXT("build: the panel did not start the build"));
		}
		return;
	}
	if (PendingLine == TEXT("deploy"))
	{
		Begin(EStep::Deploying, Layer8AnswerSeconds, TEXT("deploy: no deploy answer within 60 s"));
		ModPanel->Deploy();
		return;
	}
	if (PendingLine == TEXT("on"))
	{
		Begin(EStep::Enabling, Layer8SwitchSeconds, TEXT("on: no enable answer within 30 s"));
		ModPanel->SetEnabled(true);
		return;
	}
	if (PendingLine == TEXT("off"))
	{
		Begin(EStep::Disabling, Layer8SwitchSeconds, TEXT("off: no enable answer within 30 s"));
		ModPanel->SetEnabled(false);
		return;
	}
	Begin(EStep::Logging, Layer8AnswerSeconds, TEXT("log: no log answer within 60 s"));
	ModPanel->ShowLog();
}

void UConstructLayer8CheckSubsystem::StartReload()
{
	UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
	if (!Voxels)
	{
		Fail(TEXT("reload: the voxel world is gone"));
		return;
	}
	Changed.Reset();
	Redrawn.Reset();
	// The copy held before the reload, so a reload that never fetched cannot pass on a cached copy that already matched.
	ReloadBefore.Reset();
	FCrowdyVoxelChunk Cached;
	if (Voxels->FindChunk(Layer8Chunk.X, Layer8Chunk.Y, Layer8Chunk.Z, Cached))
	{
		ReloadBefore = ConstructHarness::GridDigest(Cached.Grid);
	}
	Begin(EStep::Reloading, Layer8ReloadSeconds, TEXT("reload: no reload answer within 30 s"));
	const uint64 Serial = StepSerial;
	TWeakObjectPtr<UConstructLayer8CheckSubsystem> WeakThis(this);
	Voxels->ReloadChunk(Layer8Chunk.X, Layer8Chunk.Y, Layer8Chunk.Z, [WeakThis, Serial](const ECrowdyVoxelResult Result, const FString&)
	{
		UConstructLayer8CheckSubsystem* Self = WeakThis.Get();
		if (Self && Self->StepSerial == Serial && Self->Step == EStep::Reloading)
		{
			Self->HandleReloaded(Result);
		}
	});
}

void UConstructLayer8CheckSubsystem::HandleReloaded(const ECrowdyVoxelResult Result)
{
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: reload %s"), *Layer8Name(Result));
	FString Sha1;
	const UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
	FCrowdyVoxelChunk Loaded;
	if (Result == ECrowdyVoxelResult::Ok && Voxels && Voxels->FindChunk(Layer8Chunk.X, Layer8Chunk.Y, Layer8Chunk.Z, Loaded))
	{
		Sha1 = ConstructHarness::GridDigest(Loaded.Grid);
	}
	const AConstructChunkView* DrawView = View.Get();
	const TArray<FIntVector> Cells = Result == ECrowdyVoxelResult::Ok ? SpinnerCells() : TArray<FIntVector>();
	for (const FIntVector& Cell : Cells)
	{
		FVector Centre = FVector::ZeroVector;
		int32 Type = 0;
		const bool bDrawn = DrawView && DrawView->FindCube(Layer8Chunk.X, Layer8Chunk.Y, Layer8Chunk.Z, Cell, Centre, Type);
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: %s"), *CubeLine(Cell, bDrawn, Type, Centre));
	}
	WriteResult(ReloadJson(Result, ReloadBefore, Sha1, Changed, Redrawn));
	Finish();
}

void UConstructLayer8CheckSubsystem::StartDelete()
{
	UConstructModPanel* ModPanel = Panel.Get();
	if (!ModPanel || !IsReadyFor(TEXT("delete"), ModPanel->GetState()))
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: delete skipped"));
		Finish();
		return;
	}
	Begin(EStep::Deleting, Layer8AnswerSeconds, TEXT("delete: no delete answer within 60 s"));
	ModPanel->Delete();
}

void UConstructLayer8CheckSubsystem::StartRelease()
{
	AConstructClaimPad* ClaimPad = Pad.Get();
	int64 GridId = 0;
	int64 OwnerUserId = 0;
	bool bOwnedByMe = false;
	FString OwnerName;
	// Releasing a grid that still holds the mod would leave it running under this player where nothing cleans it up.
	if (!bModDeleted || !ClaimPad || !ClaimPad->GetShownOwner(GridId, OwnerUserId, bOwnedByMe, OwnerName) || GridId == 0 || !bOwnedByMe)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: release skipped"));
		Finish();
		return;
	}
	Begin(EStep::Releasing, Layer8AnswerSeconds, TEXT("release: no release answer within 60 s"));
	ClaimPad->Release();
}

FString UConstructLayer8CheckSubsystem::PanelStateName() const
{
	const UConstructModPanel* ModPanel = Panel.Get();
	return ModPanel ? Layer8Name(ModPanel->GetState()) : TEXT("none");
}

void UConstructLayer8CheckSubsystem::HandleClaimDone(const ECrowdyClaimResult Result, const int64 GridId, const bool bListed, const FString& Message)
{
	if (Stage != EStage::Running || Step != EStep::Claiming)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: claim %s listed %d"), *Layer8Name(Result), bListed ? 1 : 0);
	WriteResult(ClaimJson(Result, GridId));
	if (Result != ECrowdyClaimResult::Success || GridId <= 0)
	{
		Finish();
		return;
	}
	ClaimedGrid = GridId;
	// The pad's own refresh after the claim is the ownership confirmation; its claim answer alone is not.
	Begin(EStep::AwaitOwned, Layer8ConfirmSeconds, TEXT("claim: the pad did not show this player's new claim within 30 s"));
}

void UConstructLayer8CheckSubsystem::HandleReleaseDone(const ECrowdyClaimResult Result, const int64 GridId, const bool bUnlisted, const FString& Message)
{
	if (Stage != EStage::Running || Step != EStep::Releasing)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: release %s unlisted %d"), *Layer8Name(Result), bUnlisted ? 1 : 0);
	Finish();
}

void UConstructLayer8CheckSubsystem::HandleBuildProgress(const ECrowdyModBuildStatus Status)
{
	if (Stage != EStage::Running || Step != EStep::Building)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: build-progress %s"), *Layer8Name(Status));
}

void UConstructLayer8CheckSubsystem::HandleBuildDone(const ECrowdyModResult Result, const ECrowdyModBuildStatus Status, const FString& BuildId)
{
	if (Stage != EStage::Running || Step != EStep::Building)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: build %s %s"), *Layer8Name(Result), *Layer8Name(Status));
	FString SubmittedId;
	TArray<FConstructModFileHash> Files;
	if (const UConstructModPanel* ModPanel = Panel.Get())
	{
		ModPanel->GetLastBuildFiles(SubmittedId, Files);
	}
	WriteResult(BuildJson(Result, Status, BuildId, SubmittedId, Files));
	Finish();
}

void UConstructLayer8CheckSubsystem::HandleDeployed(const ECrowdyModResult Result, const int64 Version, const bool bEnabled)
{
	if (Stage != EStage::Running || Step != EStep::Deploying)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: %s"), *DeployLine(Result, Version, bEnabled));
	Finish();
}

void UConstructLayer8CheckSubsystem::HandleEnabledChanged(const ECrowdyModResult Result, const bool bEnabled, const bool bRunning)
{
	if (Stage != EStage::Running || (Step != EStep::Enabling && Step != EStep::Disabling))
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: %s"), *EnabledLine(Result, bEnabled, bRunning));
	// The panel sends its one wake after a successful switch on; a failed one sends none.
	if (Step == EStep::Disabling || Result != ECrowdyModResult::Success)
	{
		Finish();
		return;
	}
	Begin(EStep::Waking, Layer8SwitchSeconds, TEXT("on: no wake answer within 30 s"));
}

void UConstructLayer8CheckSubsystem::HandleWakeDone(const ECrowdyModResult Result, const FString& Message)
{
	if (Stage != EStage::Running || Step != EStep::Waking)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: wake %s"), *Layer8Name(Result));
	Finish();
}

void UConstructLayer8CheckSubsystem::HandleLogShown(const ECrowdyModResult Result, const int32 LineCount)
{
	if (Stage != EStage::Running || Step != EStep::Logging)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: log %s lines %d"), *Layer8Name(Result), LineCount);
	Finish();
}

void UConstructLayer8CheckSubsystem::HandleDeleted(const ECrowdyModResult Result)
{
	if (Stage != EStage::Running || Step != EStep::Deleting)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: delete %s"), *Layer8Name(Result));
	bModDeleted = Result == ECrowdyModResult::Success;
	Finish();
}

void UConstructLayer8CheckSubsystem::HandleChunkChanged(const int64 ChunkX, const int64 ChunkY, const int64 ChunkZ, const TArray<FIntVector>& Positions)
{
	if (Step != EStep::Reloading || FInt64Vector(ChunkX, ChunkY, ChunkZ) != Layer8Chunk)
	{
		return;
	}
	for (const FIntVector& Cell : Positions)
	{
		Changed.AddUnique(Cell);
	}
}

void UConstructLayer8CheckSubsystem::HandleRedrawn(const int64 ChunkX, const int64 ChunkY, const int64 ChunkZ, const TArray<FIntVector>& Positions)
{
	if (Step != EStep::Reloading || FInt64Vector(ChunkX, ChunkY, ChunkZ) != Layer8Chunk)
	{
		return;
	}
	for (const FIntVector& Cell : Positions)
	{
		Redrawn.AddUnique(Cell);
	}
}

void UConstructLayer8CheckSubsystem::Begin(const EStep Next, const double Seconds, const FString& What)
{
	Step = Next;
	++StepSerial;
	StepDeadline = FPlatformTime::Seconds() + Seconds;
	StepWhat = What;
}

void UConstructLayer8CheckSubsystem::Finish()
{
	Step = EStep::Idle;
	++StepSerial;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer8Check: done"));
}

void UConstructLayer8CheckSubsystem::WriteResult(const FString& Line)
{
	if (!FFileHelper::SaveStringToFile(Line + TEXT("\n"), *ResultsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
	{
		Fail(TEXT("the results file could not be written"));
	}
}

void UConstructLayer8CheckSubsystem::Fail(const FString& Why)
{
	if (Stage == EStage::Failed)
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer8Check: FAIL: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
