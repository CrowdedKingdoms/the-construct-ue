#include "ConstructLayer6Check.h"

#include "Algo/AllOf.h"
#include "ConstructChunkView.h"
#include "ConstructHarnessDigest.h"
#include "ConstructHarnessLog.h"
#include "ConstructPaintCanvas.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "Subsystem/CrowdyVoxelWorld.h"

namespace
{
	const TCHAR* const Layer6MapName = TEXT("L_Layer6Check");
	constexpr double Layer6PollSeconds = 0.05;
	constexpr double Layer6TimeoutSeconds = 20.0 * 60.0;
	// B logs the cubes once every watched cell was redrawn, or after this long.
	constexpr double Layer6CubeWaitSeconds = 10.0;
	constexpr int32 Layer6MaxCells = 256;

	// The canvas chunk K (Internal/Contracts.md C6 item 11).
	const FInt64Vector3 CanvasChunk(37, -56, 0);

	bool Layer6IsCanvas(const int64 X, const int64 Y, const int64 Z)
	{
		return X == CanvasChunk.X && Y == CanvasChunk.Y && Z == CanvasChunk.Z;
	}

	// "x,y,z,t;x,y,z,t;..." as the control file writes a list of cells: distinct floor cells, each with a block type, since
	// the canvas ignores anything else and the check would then wait for an answer that never comes.
	bool Layer6ParseCells(const FString& Text, TArray<FIntVector>& OutCells, TArray<int32>& OutTypes)
	{
		TArray<FString> Items;
		Text.ParseIntoArray(Items, TEXT(";"));
		if (Items.Num() == 0 || Items.Num() > Layer6MaxCells)
		{
			return false;
		}
		for (const FString& Item : Items)
		{
			TArray<FString> Parts;
			Item.ParseIntoArray(Parts, TEXT(","));
			int32 Numbers[4] = { 0, 0, 0, 0 };
			if (Parts.Num() != 4)
			{
				return false;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (!LexTryParseString(Numbers[Index], *Parts[Index].TrimStartAndEnd()))
				{
					return false;
				}
			}
			const FIntVector Cell(Numbers[0], Numbers[1], Numbers[2]);
			const bool bFloor = Cell.X >= 0 && Cell.X < 16 && Cell.Y >= 0 && Cell.Y < 16 && Cell.Z == 0;
			if (!bFloor || Numbers[3] < 1 || Numbers[3] > 12 || OutCells.Contains(Cell))
			{
				return false;
			}
			OutCells.Add(Cell);
			OutTypes.Add(Numbers[3]);
		}
		return true;
	}
}

bool UConstructLayer6CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* OuterWorld = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && OuterWorld && OuterWorld->WorldType == EWorldType::Game;
}

void UConstructLayer6CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetGameInstance())
	{
		return;
	}
	const bool bAsked = FParse::Value(FCommandLine::Get(), TEXT("Layer6CheckControl="), ControlPath);
	if (InWorld.GetMapName() != Layer6MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer6Check.
		if (bAsked)
		{
			Fail(FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer6MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);
	// Tools/Layer6Check writes the file before launch, so a path cut short at a space fails here, not at go.
	if (!bAsked || ControlPath.IsEmpty() || !FPaths::FileExists(ControlPath))
	{
		Fail(TEXT("-Layer6CheckControl is needed; start the check with Tools/Layer6Check"));
		return;
	}
	for (TActorIterator<AConstructPaintCanvas> It(&InWorld); It; ++It)
	{
		Canvas = *It;
		break;
	}
	for (TActorIterator<AConstructChunkView> It(&InWorld); It; ++It)
	{
		View = *It;
		break;
	}
	UCrowdyVoxelWorld* VoxelWorld = InWorld.GetSubsystem<UCrowdyVoxelWorld>();
	if (!Canvas.IsValid() || !View.IsValid() || !VoxelWorld)
	{
		Fail(TEXT("the map needs a placed AConstructPaintCanvas and AConstructChunkView, and the voxel world"));
		return;
	}
	if (Canvas->GetCanvasChunk() != CanvasChunk)
	{
		const FInt64Vector3 Placed = Canvas->GetCanvasChunk();
		Fail(FString::Printf(TEXT("the canvas stands in chunk %lld %lld %lld, not K"), Placed.X, Placed.Y, Placed.Z));
		return;
	}
	VoxelWorld->OnChunkChanged.AddUniqueDynamic(this, &ThisClass::HandleChanged);
	View->OnRedrawn.AddUniqueDynamic(this, &ThisClass::HandleRedrawn);
	Canvas->OnPaintDone.AddUniqueDynamic(this, &ThisClass::HandlePaintDone);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: started"));
}

void UConstructLayer6CheckSubsystem::Deinitialize()
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

bool UConstructLayer6CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > Layer6TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out in stage %d"), static_cast<int32>(Stage)));
		return false;
	}
	if (Stage == EStage::WaitEntry)
	{
		return TickEntry();
	}
	if (bWatching && !bCubesLogged && Now - WatchedAt > Layer6CubeWaitSeconds)
	{
		LogCubes();
	}
	if (Stage == EStage::Running && Now - LastPollAt >= Layer6PollSeconds)
	{
		LastPollAt = Now;
		PollControl();
	}
	return true;
}

bool UConstructLayer6CheckSubsystem::TickEntry()
{
	UWorld* CurrentWorld = GetWorld();
	UGameInstance* Game = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = Game ? Game->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	const UCrowdyVoxelWorld* VoxelWorld = CurrentWorld ? CurrentWorld->GetSubsystem<UCrowdyVoxelWorld>() : nullptr;
	AConstructChunkView* DrawView = View.Get();
	if (!Auth || !SDK || !Session || !VoxelWorld || !DrawView || !Canvas.IsValid())
	{
		Fail(TEXT("the Crowdy SDK subsystems, the canvas or the view are missing"));
		return false;
	}
	if (!Auth->IsSignedIn() || SDK->GetUDPConnectionState() != EUDPConnectionState::Connected || Session->GetUserID() == 0
		|| !HasVisiblePeer() || !VoxelWorld->IsChunkLive(CanvasChunk.X, CanvasChunk.Y, CanvasChunk.Z))
	{
		return true;
	}
	DrawView->ShowLoadedChunks(CanvasChunk.X, CanvasChunk.Y, CanvasChunk.Z);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: stimulus landed: signed in, connected, canvas live"));
	Stage = EStage::Running;
	return true;
}

bool UConstructLayer6CheckSubsystem::HasVisiblePeer() const
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

void UConstructLayer6CheckSubsystem::PollControl()
{
	// Allows a write, or Node's write of this file could land inside a read and fail on the sharing violation.
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ControlPath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite))
	{
		return;
	}
	// A line counts only once its newline is written, so the last piece is never acted on. Node replaces "wait" with the
	// first instruction, so "wait" takes no line: the first instruction is line 0.
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"), false);
	for (int32 Index = LinesDone; Index < Lines.Num() - 1 && Stage == EStage::Running; ++Index)
	{
		const FString Line = Lines[Index].TrimStartAndEnd();
		if (Line == TEXT("wait"))
		{
			return;
		}
		LinesDone = Index + 1;
		Act(Line);
	}
}

void UConstructLayer6CheckSubsystem::Act(const FString& Line)
{
	FString Kind;
	FString Rest;
	TArray<FIntVector> Cells;
	TArray<int32> Types;
	if (!Line.Split(TEXT(" "), &Kind, &Rest) || !Layer6ParseCells(Rest, Cells, Types))
	{
		Fail(FString::Printf(TEXT("the control line \"%s\" is not one this check reads"), *Line.Left(80)));
		return;
	}
	if (Kind == TEXT("watch") && !bWatching)
	{
		bWatching = true;
		WatchedAt = FPlatformTime::Seconds();
		Watched = MoveTemp(Cells);
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: watch"));
		return;
	}
	if (Kind == TEXT("go") && Painting.Num() == 0)
	{
		TArray<FPaintStep> Steps;
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			Steps.Add({ Cells[Index], Types[Index], 0 });
		}
		StartPaint(Steps);
		return;
	}
	Fail(FString::Printf(TEXT("the control line \"%s\" came twice or out of order"), *Line.Left(80)));
}

void UConstructLayer6CheckSubsystem::StartPaint(const TArray<FPaintStep>& Steps)
{
	AConstructPaintCanvas* PaintCanvas = Canvas.Get();
	if (!PaintCanvas)
	{
		Fail(TEXT("the canvas went away before go"));
		return;
	}
	Painting = Steps;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: go"));
	for (const FPaintStep& Step : Steps)
	{
		PaintCanvas->PaintCell(Step.Cell, Step.Type);
	}
}

void UConstructLayer6CheckSubsystem::HandleChanged(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions)
{
	const UCrowdyVoxelWorld* VoxelWorld = GetWorld() ? GetWorld()->GetSubsystem<UCrowdyVoxelWorld>() : nullptr;
	if (Stage != EStage::Running || !VoxelWorld || !Layer6IsCanvas(ChunkX, ChunkY, ChunkZ))
	{
		return;
	}
	// Every named cell, not only watched ones: the other player's edits can land before the watch line is read.
	for (const FIntVector& Cell : Positions)
	{
		const int32 Type = VoxelWorld->GetVoxel(ChunkX, ChunkY, ChunkZ, Cell).Type;
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: seen %d %d %d type %d"), Cell.X, Cell.Y, Cell.Z, Type);
	}
}

void UConstructLayer6CheckSubsystem::HandleRedrawn(int64 ChunkX, int64 ChunkY, int64 ChunkZ, const TArray<FIntVector>& Positions)
{
	if (Stage != EStage::Running || !Layer6IsCanvas(ChunkX, ChunkY, ChunkZ))
	{
		return;
	}
	Redrawn.Append(Positions);
	if (bWatching && !bCubesLogged && Algo::AllOf(Watched, [this](const FIntVector& Cell) { return Redrawn.Contains(Cell); }))
	{
		LogCubes();
	}
	if (!bWatching)
	{
		return;
	}
	for (FPaintStep& Step : Painting)
	{
		if (!Positions.Contains(Step.Cell) || (Step.Logged != 0 && Step.Logged != 2))
		{
			continue;
		}
		LogCell(Step.Logged == 0 ? TEXT("pending") : TEXT("after"), Step.Cell);
		++Step.Logged;
	}
	if (Painting.Num() > 0 && Algo::AllOf(Painting, [](const FPaintStep& Step) { return Step.Logged == 3; }))
	{
		FetchFresh();
	}
}

void UConstructLayer6CheckSubsystem::HandlePaintDone(FIntVector Cell, int32 Type, ECrowdyVoxelResult Result, const FString& Message)
{
	if (Stage != EStage::Running)
	{
		return;
	}
	FPaintStep* Step = Painting.FindByPredicate([&Cell, Type](const FPaintStep& Each) { return Each.Cell == Cell && Each.Type == Type && Each.Logged < 2; });
	if (!Step)
	{
		return;
	}
	Step->Logged = 2;
	const FString ResultName = StaticEnum<ECrowdyVoxelResult>()->GetNameStringByValue(static_cast<int64>(Result));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: edit %d %d %d type %d %s"), Cell.X, Cell.Y, Cell.Z, Type, *ResultName);
	if (!bWatching && ++Answered == Painting.Num())
	{
		Stage = EStage::Done;
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: done"));
	}
}

void UConstructLayer6CheckSubsystem::LogCubes()
{
	bCubesLogged = true;
	const AConstructChunkView* DrawView = View.Get();
	for (const FIntVector& Cell : Watched)
	{
		FVector Centre;
		int32 Type = 0;
		if (!DrawView || !DrawView->FindCube(CanvasChunk.X, CanvasChunk.Y, CanvasChunk.Z, Cell, Centre, Type))
		{
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: cube %d %d %d missing"), Cell.X, Cell.Y, Cell.Z);
			continue;
		}
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: cube %d %d %d type %d at %.2f %.2f %.2f"), Cell.X, Cell.Y, Cell.Z, Type, Centre.X, Centre.Y, Centre.Z);
	}
}

void UConstructLayer6CheckSubsystem::LogCell(const TCHAR* Kind, const FIntVector& Cell)
{
	FVector Centre;
	int32 Type = 0;
	const AConstructChunkView* DrawView = View.Get();
	if (!DrawView || !DrawView->FindCube(CanvasChunk.X, CanvasChunk.Y, CanvasChunk.Z, Cell, Centre, Type))
	{
		Type = 0;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: %s %d %d %d type %d"), Kind, Cell.X, Cell.Y, Cell.Z, Type);
}

void UConstructLayer6CheckSubsystem::FetchFresh()
{
	UWorld* CurrentWorld = GetWorld();
	UGameInstance* Game = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	UCrowdyVoxels* Voxels = Game ? Game->GetSubsystem<UCrowdyVoxels>() : nullptr;
	if (!Voxels)
	{
		Fail(TEXT("the Crowdy voxels subsystem is missing"));
		return;
	}
	Stage = EStage::Fetching;
	TWeakObjectPtr<UConstructLayer6CheckSubsystem> WeakThis(this);
	Voxels->FetchChunk(CanvasChunk.X, CanvasChunk.Y, CanvasChunk.Z, [WeakThis](const FCrowdyChunkFetch& Fetched)
	{
		if (UConstructLayer6CheckSubsystem* Self = WeakThis.Get())
		{
			Self->HandleFresh(Fetched);
		}
	});
}

void UConstructLayer6CheckSubsystem::HandleFresh(const FCrowdyChunkFetch& Fetch)
{
	if (Fetch.Result != ECrowdyVoxelResult::Ok || Fetch.Chunks.Num() != 1)
	{
		const FString ResultName = StaticEnum<ECrowdyVoxelResult>()->GetNameStringByValue(static_cast<int64>(Fetch.Result));
		Fail(FString::Printf(TEXT("the fresh fetch of K ended %s with %d chunks"), *ResultName, Fetch.Chunks.Num()));
		return;
	}
	Stage = EStage::Done;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: fresh sha1 %s"), *ConstructHarness::GridDigest(Fetch.Chunks[0].Grid));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer6Check: done"));
}

void UConstructLayer6CheckSubsystem::Fail(const FString& Why)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer6Check: FAIL: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
