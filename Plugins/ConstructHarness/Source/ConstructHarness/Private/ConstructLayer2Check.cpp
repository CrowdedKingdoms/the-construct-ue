#include "ConstructLayer2Check.h"

#include "ConstructHarnessLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
	const TCHAR* const Layer2MapName = TEXT("L_Layer2Check");
	constexpr double Layer2HalfSide = 400.0;
	constexpr double Layer2Speed = 300.0;
	constexpr double Layer2HoldSeconds = 2.0;
	constexpr double Layer2WalkSeconds = 60.0;
	constexpr float Layer2TickSeconds = 0.1f;

	// The check pawn's class ships in every game, so its notifies log only while a check runs in this process.
	bool bLayer2CheckRunning = false;

	FString Layer2Position(const FVector& Location)
	{
		return FString::Printf(TEXT("%d %d %d"), FMath::RoundToInt(Location.X), FMath::RoundToInt(Location.Y),
		                       FMath::RoundToInt(Location.Z));
	}
}

void AConstructLayer2CheckPawn::SendNextCounter()
{
	++CheckCounter;
}

bool AConstructLayer2CheckPawn::IsRemoteCopy() const
{
	const UCrowdyEntityComponent* Component = FindComponentByClass<UCrowdyEntityComponent>();
	return Component && Component->GetRole() == ECrowdyRole::RemoteProxy;
}

bool AConstructLayer2CheckPawn::ShouldUseLocalCharacterMovement() const
{
	return false;
}

void AConstructLayer2CheckPawn::OnRep_PlayerName()
{
	Super::OnRep_PlayerName();
	if (!bLayer2CheckRunning)
	{
		return;
	}
	// The watcher alone carries the expected name; its own pawn's notify is not a received name.
	const FString Expected = FPlatformMisc::GetEnvironmentVariable(TEXT("CONSTRUCT_EXPECT_NAME"));
	if (Expected.IsEmpty() || PlayerName.IsEmpty() || !IsRemoteCopy())
	{
		return;
	}
	FString Gamertag = PlayerName;
	int32 Hash = INDEX_NONE;
	if (PlayerName.FindLastChar(TEXT('#'), Hash))
	{
		Gamertag = PlayerName.Left(Hash);
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer2Check: name: %s"),
	       Gamertag.Equals(Expected, ESearchCase::CaseSensitive) ? TEXT("match") : TEXT("mismatch"));
}

void AConstructLayer2CheckPawn::OnRep_CheckCounter()
{
	if (!bLayer2CheckRunning || !IsRemoteCopy())
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer2Check: received %d at %s"), CheckCounter,
	       *Layer2Position(GetActorLocation()));
}

AConstructLayer2CheckGameMode::AConstructLayer2CheckGameMode()
{
	DefaultPawnClass = AConstructLayer2CheckPawn::StaticClass();
}

bool UConstructLayer2CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->WorldType == EWorldType::Game;
}

void UConstructLayer2CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetGameInstance())
	{
		return;
	}
	FString Role;
	const bool bHasRole = FParse::Value(FCommandLine::Get(), TEXT("Layer2CheckRole="), Role);
	if (InWorld.GetMapName() != Layer2MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer2Check.
		if (bHasRole)
		{
			Finish(false, FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer2MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	double TimeoutMinutes = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("Layer2CheckTimeoutMinutes="), TimeoutMinutes) && TimeoutMinutes > 0.0)
	{
		TimeoutSeconds = TimeoutMinutes * 60.0;
	}
	TickerHandle =
	    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), Layer2TickSeconds);

	if (Role != TEXT("walker") && Role != TEXT("watcher"))
	{
		Finish(false, TEXT("-Layer2CheckRole=walker or watcher is missing; start the check with Tools/Layer2Check"));
		return;
	}
	bWalker = Role == TEXT("walker");
	bLayer2CheckRunning = true;
	Tracker = InWorld.GetSubsystem<UCrowdyActorTracker>();
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer2Check: started as %s"), *Role);
}

void UConstructLayer2CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0.
	if (TickerHandle.IsValid() && Stage != EStage::Done)
	{
		Finish(false, TEXT("the check's world ended before it finished"));
	}
	if (UCrowdyActorTracker* ActiveTracker = Tracker.Get())
	{
		ActiveTracker->OnRemoteEntityLeft.RemoveDynamic(this, &ThisClass::HandlePeerLeft);
		ActiveTracker->OnRemoteEntityTimedOut.RemoveDynamic(this, &ThisClass::HandlePeerTimedOut);
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	bLayer2CheckRunning = false;
	Super::Deinitialize();
}

bool UConstructLayer2CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds)
	{
		Finish(false, FString::Printf(TEXT("timed out after %.0f minutes in stage %d"), TimeoutSeconds / 60.0,
		                              static_cast<int32>(Stage)));
		return false;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = GameInstance ? GameInstance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = GameInstance ? GameInstance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	if (!Auth || !SDK)
	{
		Finish(false, TEXT("the Crowdy SDK subsystems are missing"));
		return false;
	}

	AConstructLayer2CheckPawn* Pawn = FindLocalPawn();
	if (Stage == EStage::WaitEntry)
	{
		if (Pawn && Auth->IsSignedIn() && SDK->GetUDPConnectionState() == EUDPConnectionState::Connected)
		{
			Stage = EStage::WaitPeer;
		}
		return true;
	}
	if (!Pawn)
	{
		Finish(false, TEXT("the local check pawn was lost"));
		return false;
	}
	if (Stage == EStage::WaitLeave)
	{
		// Finished here, not in the tracker's broadcast, so presence's own departure line is written first.
		if (bPeerLeft)
		{
			Finish(true, FString());
			return false;
		}
		return true;
	}
	if (Stage == EStage::WaitPeer)
	{
		PeerId = FindVisiblePeer();
		if (!PeerId.IsValid())
		{
			return true;
		}
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer2Check: stimulus landed: signed in, connected, other player seen"));
		Centre = Pawn->GetActorLocation();
		StageStartedAt = Now;
		if (bWalker)
		{
			Pawn->SetActorLocation(PathPoint(0.0));
			Stage = EStage::Hold;
			return true;
		}
		if (UCrowdyActorTracker* ActiveTracker = Tracker.Get())
		{
			ActiveTracker->OnRemoteEntityLeft.AddUniqueDynamic(this, &ThisClass::HandlePeerLeft);
			ActiveTracker->OnRemoteEntityTimedOut.AddUniqueDynamic(this, &ThisClass::HandlePeerTimedOut);
		}
		Stage = EStage::WaitLeave;
		return true;
	}
	if (Stage == EStage::Hold && Now - StageStartedAt >= Layer2HoldSeconds)
	{
		StageStartedAt = Now;
		Stage = EStage::Walk;
	}
	if (Stage == EStage::Walk)
	{
		TickWalk(*Pawn, Now);
	}
	return true;
}

void UConstructLayer2CheckSubsystem::TickWalk(AConstructLayer2CheckPawn& Pawn, double Now)
{
	const double Elapsed = Now - StageStartedAt;
	if (Elapsed >= Layer2WalkSeconds)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer2Check: walk done: %d sent"), Sent);
		Stage = EStage::Idle;
		return;
	}
	Pawn.SetActorLocation(PathPoint(Elapsed * Layer2Speed));
	Pawn.SendNextCounter();
	++Sent;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer2Check: sent %d at %s"), Pawn.GetCheckCounter(),
	       *Layer2Position(Pawn.GetActorLocation()));
}

AConstructLayer2CheckPawn* UConstructLayer2CheckSubsystem::FindLocalPawn() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AConstructLayer2CheckPawn* Pawn = Controller ? Cast<AConstructLayer2CheckPawn>(Controller->GetPawn()) : nullptr;
	const UCrowdyEntityComponent* Component = Pawn ? Pawn->FindComponentByClass<UCrowdyEntityComponent>() : nullptr;
	return Component && Component->GetRole() == ECrowdyRole::Owner ? Pawn : nullptr;
}

FGuid UConstructLayer2CheckSubsystem::FindVisiblePeer() const
{
	for (TActorIterator<AConstructLayer2CheckPawn> It(GetWorld()); It; ++It)
	{
		const UCrowdyEntityComponent* Component = It->FindComponentByClass<UCrowdyEntityComponent>();
		if (Component && Component->GetRole() == ECrowdyRole::RemoteProxy && !It->IsHidden())
		{
			return Component->GetNetID();
		}
	}
	return FGuid();
}

FVector UConstructLayer2CheckSubsystem::PathPoint(double Distance) const
{
	const FVector Corners[] = {
	    Centre + FVector(-Layer2HalfSide, -Layer2HalfSide, 0.0), Centre + FVector(Layer2HalfSide, -Layer2HalfSide, 0.0),
	    Centre + FVector(Layer2HalfSide, Layer2HalfSide, 0.0), Centre + FVector(-Layer2HalfSide, Layer2HalfSide, 0.0)};
	const double Side = Layer2HalfSide * 2.0;
	const double Along = FMath::Fmod(Distance, Side * 4.0);
	const int32 Edge = FMath::Clamp(FMath::FloorToInt(Along / Side), 0, 3);
	return FMath::Lerp(Corners[Edge], Corners[(Edge + 1) % 4], (Along - Edge * Side) / Side);
}

void UConstructLayer2CheckSubsystem::HandlePeerLeft(const FCrowdyActorLeft& ActorLeft, int32 ActorCount)
{
	bPeerLeft |= ActorLeft.UUID == PeerId;
}

void UConstructLayer2CheckSubsystem::HandlePeerTimedOut(FGuid UUID, int32 ActorCount)
{
	bPeerLeft |= UUID == PeerId;
}

void UConstructLayer2CheckSubsystem::Finish(bool bOk, const FString& Why)
{
	if (Stage == EStage::Done)
	{
		return;
	}
	Stage = EStage::Done;
	if (bOk)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer2Check: watcher done: the other player left"));
	}
	else
	{
		UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer2Check: FAIL: %s"), *Why);
	}
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, bOk ? 0 : 1);
}
