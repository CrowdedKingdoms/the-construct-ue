#include "ConstructLayer5Check.h"

#include "ConstructChunkView.h"
#include "ConstructHarnessLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "Subsystem/CrowdyVoxels.h"

namespace
{
	const TCHAR* const Layer5MapName = TEXT("L_Layer5Check");
	constexpr double Layer5PollSeconds = 0.05;
	constexpr double Layer5TimeoutSeconds = 20.0 * 60.0;

	// The seed chunk S and the range read around it (Internal/Contracts.md C5 item 10).
	constexpr int64 SeedX = 37;
	constexpr int64 SeedY = -52;
	constexpr int64 SeedZ = 1;
	constexpr int32 Layer5Range = 2;

	// S's 13 filled cells after the seed: the lone cube, the x bar less its removed end, the y bar, the z bar, the edit.
	const FIntVector ExpectedCells[] = {
		{1, 2, 3},
		{10, 12, 12}, {11, 12, 12}, {12, 12, 12}, {13, 12, 12}, {14, 12, 12},
		{12, 4, 8}, {12, 5, 8}, {12, 6, 8}, {12, 7, 8},
		{4, 12, 13}, {4, 12, 14},
		{15, 15, 15}};

	FString GridDigest(const TArray<uint8>& Grid)
	{
		FSHAHash Hash;
		FSHA1::HashBuffer(Grid.GetData(), Grid.Num(), Hash.Hash);
		return Hash.ToString().ToLower();
	}
}

bool UConstructLayer5CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* OuterWorld = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && OuterWorld && OuterWorld->WorldType == EWorldType::Game;
}

void UConstructLayer5CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetGameInstance())
	{
		return;
	}
	const bool bAsked = FParse::Value(FCommandLine::Get(), TEXT("Layer5CheckControl="), ControlPath);
	if (InWorld.GetMapName() != Layer5MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer5Check.
		if (bAsked)
		{
			Fail(FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer5MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);
	// Tools/Layer5Check writes both files before launch, so a path cut short at a space fails here, not at go.
	if (!bAsked || ControlPath.IsEmpty() || !FPaths::FileExists(ControlPath)
		|| !FParse::Value(FCommandLine::Get(), TEXT("Layer5CheckWriter="), WriterPath) || !FPaths::FileExists(WriterPath))
	{
		Fail(TEXT("-Layer5CheckControl and -Layer5CheckWriter are needed; start the check with Tools/Layer5Check"));
		return;
	}
	for (TActorIterator<AConstructChunkView> It(&InWorld); It; ++It)
	{
		View = *It;
		break;
	}
	if (!View.IsValid())
	{
		Fail(TEXT("no AConstructChunkView is placed in the map"));
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: started"));
}

void UConstructLayer5CheckSubsystem::Deinitialize()
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

bool UConstructLayer5CheckSubsystem::ReadControl(const FString& Expected) const
{
	// Allows a write, or Node's write of this file could land inside a read and fail on the sharing violation.
	FString Text;
	return FFileHelper::LoadFileToString(Text, *ControlPath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite) && Text.TrimStartAndEnd() == Expected;
}

bool UConstructLayer5CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > Layer5TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out in stage %d"), static_cast<int32>(Stage)));
		return false;
	}
	if (Stage == EStage::WaitEntry)
	{
		return TickEntry();
	}
	if (Stage != EStage::WaitGo || Now - LastPollAt < Layer5PollSeconds)
	{
		return true;
	}
	LastPollAt = Now;
	if (ReadControl(TEXT("go")))
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: go"));
		Fetch();
	}
	return true;
}

bool UConstructLayer5CheckSubsystem::TickEntry()
{
	UWorld* CurrentWorld = GetWorld();
	UGameInstance* Game = CurrentWorld ? CurrentWorld->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = Game ? Game->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	if (!Auth || !SDK || !Session)
	{
		Fail(TEXT("the Crowdy SDK subsystems are missing"));
		return false;
	}
	if (!Auth->IsSignedIn() || SDK->GetUDPConnectionState() != EUDPConnectionState::Connected || Session->GetUserID() == 0)
	{
		return true;
	}
	// The client must not be the account that wrote S, or it could draw its own write. The id is compared, never logged.
	FString WriterId;
	if (!FFileHelper::LoadFileToString(WriterId, *WriterPath) || WriterId.TrimStartAndEnd() == LexToString(Session->GetUserID()))
	{
		Fail(TEXT("the client is signed in as the account that wrote S, or the writer file could not be read; sign player A in with --setup"));
		return false;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: stimulus landed: signed in, connected"));
	Stage = EStage::WaitGo;
	return true;
}

void UConstructLayer5CheckSubsystem::Fetch()
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
	TWeakObjectPtr<UConstructLayer5CheckSubsystem> WeakThis(this);
	Voxels->FetchChunksAround(SeedX, SeedY, SeedZ, Layer5Range, [WeakThis](const FCrowdyChunkFetch& Fetched)
	{
		if (UConstructLayer5CheckSubsystem* Self = WeakThis.Get())
		{
			Self->HandleFetch(Fetched);
		}
	});
}

void UConstructLayer5CheckSubsystem::HandleFetch(const FCrowdyChunkFetch& Fetch)
{
	AConstructChunkView* DrawView = View.Get();
	if (!DrawView)
	{
		Fail(TEXT("the chunk view went away during the fetch"));
		return;
	}
	const FString ResultName = StaticEnum<ECrowdyVoxelResult>()->GetNameStringByValue(static_cast<int64>(Fetch.Result));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: read %s chunks %d"), *ResultName, Fetch.Chunks.Num());
	Stage = EStage::Done;
	if (Fetch.Result != ECrowdyVoxelResult::Ok)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: done"));
		return;
	}
	DrawView->ShowChunks(Fetch.Chunks, SeedX, SeedY, SeedZ);
	for (const FCrowdyVoxelChunk& Chunk : Fetch.Chunks)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: chunk %lld %lld %lld sha1 %s truncated %d"),
			Chunk.X, Chunk.Y, Chunk.Z, *GridDigest(Chunk.Grid), Chunk.bEditsTruncated ? 1 : 0);
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: cubes %d"), DrawView->GetCubeCount());
	for (const FIntVector& Cell : ExpectedCells)
	{
		FVector Centre;
		int32 Type = 0;
		if (!DrawView->FindCube(SeedX, SeedY, SeedZ, Cell, Centre, Type))
		{
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: cube %d %d %d missing"), Cell.X, Cell.Y, Cell.Z);
			continue;
		}
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: cube %d %d %d type %d at %.2f %.2f %.2f"), Cell.X, Cell.Y, Cell.Z, Type, Centre.X, Centre.Y, Centre.Z);
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer5Check: done"));
}

void UConstructLayer5CheckSubsystem::Fail(const FString& Why)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer5Check: FAIL: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
