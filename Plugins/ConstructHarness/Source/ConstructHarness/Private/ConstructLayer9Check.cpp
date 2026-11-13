#include "ConstructLayer9Check.h"

#include "ConstructDeveloperSettings.h"
#include "ConstructHarnessLog.h"
#include "ConstructWebcamFeeds.h"
#include "ConstructWebcamSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "IMediaCaptureSupport.h"
#include "MediaCaptureSupport.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
	const TCHAR* const Layer9MapName = TEXT("L_Layer9Check");
	constexpr int32 Layer9NearChunkX = 6;
	constexpr int32 Layer9FarChunkX = 9;
	constexpr double Layer9ViewerOffset = 400.0;
	constexpr double Layer9PollSeconds = 0.05;
	constexpr double Layer9SettleSeconds = 4.0;
	constexpr double Layer9ReadySeconds = 60.0;
	constexpr double Layer9FrameSeconds = 0.1;

	// Times are seconds after go. The pattern is frames 1 to 200; the stream that follows carries 201 onwards.
	constexpr int32 Layer9PatternFrames = 200;
	constexpr double Layer9PatternStart = 2.0;
	constexpr double Layer9PatternEnd = 29.0;
	constexpr double Layer9StreamStart = 31.0;
	constexpr double Layer9CameraStart = 46.0;
	constexpr double Layer9CameraOpenSeconds = 6.0;

	constexpr int32 Layer9LegCount = 4;
	const TCHAR* const Layer9LegNames[Layer9LegCount] = {TEXT("pattern"), TEXT("far"), TEXT("back-near"), TEXT("camera")};
	constexpr double Layer9WindowStarts[Layer9LegCount] = {0.0, 32.0, 40.0, 48.0};
	constexpr double Layer9WindowEnds[Layer9LegCount] = {30.0, 37.0, 45.0, 60.0};
	constexpr int32 Layer9MoveCount = 2;
	constexpr double Layer9MoveTimes[Layer9MoveCount] = {30.0, 38.0};

	// 16 by 16 pixel blocks, 8 across and 6 down, each aligned to the JPEG 8x8 grid so it stays flat after encoding.
	// Rows 0 and 5 are a fixed marker, rows 1 and 2 the sequence's 16 bits, rows 3 and 4 their complement.
	constexpr int32 Layer9Block = 16;
	constexpr int32 Layer9BlocksX = 8;
	constexpr int32 Layer9BlocksY = 6;
	constexpr int32 Layer9MaxSequence = 65535;
	constexpr double Layer9BlackBelow = 64.0;
	constexpr double Layer9WhiteAbove = 192.0;

	bool IsBlockWhite(const int32 Sequence, const int32 BlockX, const int32 BlockY)
	{
		if (BlockY == 0 || BlockY == Layer9BlocksY - 1)
		{
			return (BlockX % 2 == 0) == (BlockY == 0);
		}
		const int32 Bit = ((BlockY - 1) % 2) * Layer9BlocksX + BlockX;
		const bool bSet = ((Sequence >> Bit) & 1) != 0;
		return BlockY <= 2 ? bSet : !bSet;
	}

	double Luma(const FColor& Color)
	{
		return 0.299 * Color.R + 0.587 * Color.G + 0.114 * Color.B;
	}

	FString CameraStateText(const EConstructCameraState State, const EConstructCameraBlockedReason Reason)
	{
		return FString::Printf(TEXT("%s, %s"), *StaticEnum<EConstructCameraState>()->GetNameStringByValue(static_cast<int64>(State)),
		                       *StaticEnum<EConstructCameraBlockedReason>()->GetNameStringByValue(static_cast<int64>(Reason)));
	}
}

bool AConstructLayer9CheckPawn::ShouldUseLocalCharacterMovement() const
{
	return false;
}

AConstructLayer9CheckGameMode::AConstructLayer9CheckGameMode()
{
	DefaultPawnClass = AConstructLayer9CheckPawn::StaticClass();
}

void UConstructLayer9CheckSubsystem::DrawSequence(const int32 Sequence, TArray<FColor>& OutPixels)
{
	OutPixels.SetNumUninitialized(FrameWidth * FrameHeight);
	for (int32 Y = 0; Y < FrameHeight; ++Y)
	{
		for (int32 X = 0; X < FrameWidth; ++X)
		{
			const bool bWhite = IsBlockWhite(Sequence, X / Layer9Block, Y / Layer9Block);
			OutPixels[Y * FrameWidth + X] = bWhite ? FColor::White : FColor::Black;
		}
	}
}

int32 UConstructLayer9CheckSubsystem::ReadSequence(TConstArrayView<FColor> Pixels, const FIntPoint Size)
{
	if (Size != FIntPoint(FrameWidth, FrameHeight) || Pixels.Num() != FrameWidth * FrameHeight)
	{
		return 0;
	}
	// Each block reads from its inner 8 by 8 pixels, so any ringing at its edges is never sampled.
	bool bWhite[Layer9BlocksY][Layer9BlocksX];
	for (int32 BlockY = 0; BlockY < Layer9BlocksY; ++BlockY)
	{
		for (int32 BlockX = 0; BlockX < Layer9BlocksX; ++BlockX)
		{
			double Sum = 0.0;
			for (int32 Y = 4; Y < 12; ++Y)
			{
				for (int32 X = 4; X < 12; ++X)
				{
					Sum += Luma(Pixels[(BlockY * Layer9Block + Y) * FrameWidth + BlockX * Layer9Block + X]);
				}
			}
			const double Mean = Sum / 64.0;
			if (Mean > Layer9BlackBelow && Mean < Layer9WhiteAbove)
			{
				return 0;
			}
			bWhite[BlockY][BlockX] = Mean >= Layer9WhiteAbove;
		}
	}
	int32 Sequence = 0;
	for (int32 Bit = 0; Bit < 16; ++Bit)
	{
		Sequence |= bWhite[1 + Bit / Layer9BlocksX][Bit % Layer9BlocksX] ? 1 << Bit : 0;
	}
	for (int32 BlockY = 0; BlockY < Layer9BlocksY; ++BlockY)
	{
		for (int32 BlockX = 0; BlockX < Layer9BlocksX; ++BlockX)
		{
			if (bWhite[BlockY][BlockX] != IsBlockWhite(Sequence, BlockX, BlockY))
			{
				return 0;
			}
		}
	}
	return Sequence;
}

double UConstructLayer9CheckSubsystem::LumaDeviation(TConstArrayView<FColor> Pixels)
{
	if (Pixels.IsEmpty())
	{
		return 0.0;
	}
	double Sum = 0.0;
	double SumSquares = 0.0;
	for (const FColor& Pixel : Pixels)
	{
		const double Value = Luma(Pixel);
		Sum += Value;
		SumSquares += Value * Value;
	}
	const double Mean = Sum / Pixels.Num();
	return FMath::Sqrt(FMath::Max(0.0, SumSquares / Pixels.Num() - Mean * Mean));
}

FVector UConstructLayer9CheckSubsystem::SpotFor(const bool bInSender, const bool bFar, const double ChunkSize)
{
	const FVector SenderSpot = FVector(Layer9NearChunkX + 0.5, 0.5, 0.5) * ChunkSize;
	if (bInSender)
	{
		return SenderSpot;
	}
	return bFar ? FVector(Layer9FarChunkX + 0.5, 0.5, 0.5) * ChunkSize : SenderSpot + FVector(Layer9ViewerOffset, 0.0, 0.0);
}

bool UConstructLayer9CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->WorldType == EWorldType::Game;
}

void UConstructLayer9CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UGameInstance* GameInstance = InWorld.GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	FString Role;
	const bool bHasRole = FParse::Value(FCommandLine::Get(), TEXT("Layer9CheckRole="), Role);
	if (InWorld.GetMapName() != Layer9MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer9Check.
		if (bHasRole)
		{
			Fail(FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer9MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	double TimeoutMinutes = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("Layer9CheckTimeoutMinutes="), TimeoutMinutes) && TimeoutMinutes > 0.0)
	{
		TimeoutSeconds = TimeoutMinutes * 60.0;
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);

	if (Role != TEXT("A") && Role != TEXT("B"))
	{
		Fail(TEXT("-Layer9CheckRole=A or B is missing; start the check with Tools/Layer9Check"));
		return;
	}
	if (!FParse::Value(FCommandLine::Get(), TEXT("Layer9CheckControl="), ControlPath) || ControlPath.IsEmpty())
	{
		Fail(TEXT("-Layer9CheckControl=<path> is missing; start the check with Tools/Layer9Check"));
		return;
	}
	// Tools/Layer9Check writes the file before launch, so a path cut short at a space fails here, not at go.
	if (!FPaths::FileExists(ControlPath))
	{
		Fail(TEXT("the -Layer9CheckControl file does not exist"));
		return;
	}
	bSender = Role == TEXT("A");

	Webcam = GameInstance->GetSubsystem<UConstructWebcamSubsystem>();
	Feeds = InWorld.GetSubsystem<UConstructWebcamFeeds>();
	Sender = GameInstance->GetSubsystem<UCrowdyVideoSender>();
	Receiver = InWorld.GetSubsystem<UCrowdyVideoFrameReceiver>();
	if (!Webcam.IsValid() || !Feeds.IsValid() || !Sender.IsValid() || !Receiver.IsValid())
	{
		Fail(TEXT("the webcam, webcam feeds, video sender or video receiver subsystem is missing"));
		return;
	}
	Sender->OnVideoSendRefused.AddUniqueDynamic(this, &ThisClass::HandleVideoRefused);
	// Arrivals are counted before the feeds' own filters, so a far window's zero is the platform's distance, not a feed.
	if (!bSender)
	{
		Receiver->OnVideoFrameAssembled.AddUniqueDynamic(this, &ThisClass::HandleFrameArrived);
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: started as %s"), *Role);
}

void UConstructLayer9CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0.
	if (TickerHandle.IsValid() && Stage != EStage::Done)
	{
		Fail(TEXT("the check's world ended before it finished"));
	}
	RemoveHooks();
	if (UCrowdyVideoSender* ActiveSender = Sender.Get())
	{
		ActiveSender->OnVideoSendRefused.RemoveDynamic(this, &ThisClass::HandleVideoRefused);
	}
	if (UCrowdyVideoFrameReceiver* ActiveReceiver = Receiver.Get())
	{
		ActiveReceiver->OnVideoFrameAssembled.RemoveDynamic(this, &ThisClass::HandleFrameArrived);
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer9CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out after %.0f minutes in stage %d"), TimeoutSeconds / 60.0, static_cast<int32>(Stage)));
		return false;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = GameInstance ? GameInstance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = GameInstance ? GameInstance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const UCrowdyGameSession* Session = GameInstance ? GameInstance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	if (!Auth || !SDK || !Session || !Webcam.IsValid() || !Feeds.IsValid())
	{
		Fail(TEXT("the Crowdy SDK or webcam subsystems are missing"));
		return false;
	}

	const AConstructLayer9CheckPawn* Pawn = FindLocalPawn();
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
		Fail(TEXT("the local check pawn was lost"));
		return false;
	}
	if (Stage == EStage::WaitPeer)
	{
		PeerId = FindVisiblePeer();
		if (!PeerId.IsValid())
		{
			return true;
		}
		const double ChunkSize = Session->GetChunkSize();
		if (ChunkSize <= 0.0)
		{
			Fail(TEXT("the session has no chunk size"));
			return false;
		}
		NearSpot = SpotFor(bSender, false, ChunkSize);
		FarSpot = SpotFor(bSender, true, ChunkSize);
		if (!MoveTo(NearSpot, TEXT("near")) || !InstallHooks())
		{
			return false;
		}
		SettleStartedAt = Now;
		Stage = EStage::Settle;
		return true;
	}
	if (Stage == EStage::Settle)
	{
		return TickSettle(Now);
	}
	if (Stage == EStage::WaitGo)
	{
		FString Control;
		if (ReadControl(Now, Control) && Control == TEXT("go"))
		{
			GoAt = Now;
			Stage = EStage::Run;
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: go"));
		}
		return true;
	}
	TickRun(Now);
	return Stage == EStage::Run;
}

bool UConstructLayer9CheckSubsystem::ReadControl(const double Now, FString& OutControl)
{
	if (Now - LastPollAt < Layer9PollSeconds)
	{
		return false;
	}
	LastPollAt = Now;
	if (!FFileHelper::LoadFileToString(OutControl, *ControlPath))
	{
		return false;
	}
	OutControl.TrimStartAndEndInline();
	return true;
}

bool UConstructLayer9CheckSubsystem::InstallHooks()
{
#if UE_BUILD_SHIPPING
	Fail(TEXT("the layer 9 check needs a Development build: Shipping has no test frame source"));
	return false;
#else
	if (!GetDefault<UConstructDeveloperSettings>()->bEnableWebcam)
	{
		Fail(TEXT("webcams are not enabled for this process; start the check with Tools/Layer9Check"));
		return false;
	}
	TWeakObjectPtr<UConstructLayer9CheckSubsystem> WeakThis(this);
	if (bSender)
	{
		// Installed before the camera is turned on, so the camera starts on the test source, not a device.
		Webcam->SetFrameSourceForCheck(
		    [WeakThis](TArray<FColor>& OutPixels)
		    {
			    UConstructLayer9CheckSubsystem* Self = WeakThis.Get();
			    return Self && Self->DrawNextFrame(OutPixels);
		    },
		    true,
		    [WeakThis](const FCrowdyVideoFrameSend& Send)
		    {
			    if (UConstructLayer9CheckSubsystem* Self = WeakThis.Get())
			    {
				    Self->HandleSent(Send);
			    }
		    });
		bSourceInstalled = true;
		Webcam->SetCameraOn(true);
		return true;
	}
	Feeds->SetDecodedFrameObserverForCheck(
	    [WeakThis](APawn* Pawn, const TArray<FColor>& Pixels, FIntPoint Size)
	    {
		    if (UConstructLayer9CheckSubsystem* Self = WeakThis.Get())
		    {
			    Self->HandleDecoded(Pawn, Pixels, Size);
		    }
	    });
	bObserverInstalled = true;
	Feeds->SetShowVideo(true);
	return true;
#endif
}

void UConstructLayer9CheckSubsystem::RemoveHooks()
{
#if !UE_BUILD_SHIPPING
	UConstructWebcamSubsystem* ActiveWebcam = Webcam.Get();
	if (bSender && ActiveWebcam)
	{
		// The choice goes first, so clearing the source cannot start the real camera on the way out.
		ActiveWebcam->SetCameraOn(false);
		if (bSourceInstalled)
		{
			ActiveWebcam->ClearFrameSourceForCheck();
		}
	}
	UConstructWebcamFeeds* ActiveFeeds = Feeds.Get();
	if (bObserverInstalled && ActiveFeeds)
	{
		ActiveFeeds->SetDecodedFrameObserverForCheck(nullptr);
	}
#endif
	bSourceInstalled = false;
	bObserverInstalled = false;
}

bool UConstructLayer9CheckSubsystem::IsSenderCameraOn(const double Now)
{
	const EConstructCameraState State = Webcam->GetCameraState();
	const EConstructCameraBlockedReason Reason = Webcam->GetBlockedReason();
	if (State == EConstructCameraState::On)
	{
		return true;
	}
	if (State == EConstructCameraState::Unavailable || State == EConstructCameraState::Off)
	{
		Fail(FString::Printf(TEXT("the camera did not turn on: %s"), *CameraStateText(State, Reason)));
		return false;
	}
	if (Now - SettleStartedAt > Layer9ReadySeconds)
	{
		Fail(FString::Printf(TEXT("the camera stayed %s"), *CameraStateText(State, Reason)));
	}
	return false;
}

bool UConstructLayer9CheckSubsystem::IsViewerReceiving(const double Now)
{
	if (Feeds->IsReceiving())
	{
		return true;
	}
	if (Now - SettleStartedAt > Layer9ReadySeconds)
	{
		Fail(TEXT("the feeds never started receiving: Show Video is off or Video Receive does not allow it"));
	}
	return false;
}

bool UConstructLayer9CheckSubsystem::TickSettle(const double Now)
{
	const bool bReady = bSender ? IsSenderCameraOn(Now) : IsViewerReceiving(Now);
	if (!bReady)
	{
		return Stage != EStage::Failed;
	}
	// The first frame goes out 2 s after go, so every send stays at least 4 s clear of the near move.
	if (Now - SettleStartedAt < Layer9SettleSeconds)
	{
		return true;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: stimulus landed: signed in, connected, other player seen, %s"),
	       bSender ? TEXT("camera state On") : TEXT("receiving video"));
	Stage = EStage::WaitGo;
	return true;
}

void UConstructLayer9CheckSubsystem::TickRun(const double Now)
{
	const double Elapsed = Now - GoAt;
	if (bWindowOpen && Elapsed >= Layer9WindowEnds[WindowIndex])
	{
		if (bSender && WindowIndex == 0)
		{
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: pattern frames drawn %d"), PatternDrawn);
		}
		if (!bSender)
		{
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: arrived %d (%s window)"), WindowArrivals,
			       Layer9LegNames[WindowIndex]);
		}
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: window close %s"), Layer9LegNames[WindowIndex]);
		bWindowOpen = false;
		++WindowIndex;
	}
	if (!bSender && MoveIndex < Layer9MoveCount && Elapsed >= Layer9MoveTimes[MoveIndex])
	{
		const bool bAway = MoveIndex == 0;
		++MoveIndex;
		if (!MoveTo(bAway ? FarSpot : NearSpot, bAway ? TEXT("far") : TEXT("near")))
		{
			return;
		}
	}
	if (!bWindowOpen && WindowIndex < Layer9LegCount && Elapsed >= Layer9WindowStarts[WindowIndex])
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: window open %s"), Layer9LegNames[WindowIndex]);
		bWindowOpen = true;
		WindowArrivals = 0;
	}
	if (bSender && CameraLeg == ECameraLeg::TestSource && Elapsed >= Layer9CameraStart)
	{
		BeginCameraLeg(Now);
	}
	if (bSender)
	{
		TickCameraLeg(Now);
	}
	if (WindowIndex >= Layer9LegCount)
	{
		if (bSender)
		{
			Webcam->SetCameraOn(false);
		}
		LogDone();
	}
}

void UConstructLayer9CheckSubsystem::BeginCameraLeg(const double Now)
{
	CameraLegStartedAt = Now;
	// The webcam subsystem falls back to a software device when no plain one exists; that would prove no capture.
	TArray<FMediaCaptureDeviceInfo> Devices;
	MediaCaptureSupport::EnumerateVideoCaptureDevices(Devices);
	const bool bHardware = Devices.ContainsByPredicate(
	    [](const FMediaCaptureDeviceInfo& Device) { return Device.Type == EMediaCaptureDeviceType::Video; });
	const bool bSoftware = Devices.ContainsByPredicate(
	    [](const FMediaCaptureDeviceInfo& Device) { return Device.Type == EMediaCaptureDeviceType::VideoSoftware; });
	if (!bHardware && bSoftware)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: source: camera, not run: only a software capture device"));
		CameraLeg = ECameraLeg::NotRun;
		return;
	}
#if !UE_BUILD_SHIPPING
	Webcam->ClearFrameSourceForCheck();
#endif
	bSourceInstalled = false;
	bCameraLeftOn = Webcam->GetCameraState() != EConstructCameraState::On;
	CameraLeg = ECameraLeg::Opening;
}

void UConstructLayer9CheckSubsystem::TickCameraLeg(const double Now)
{
	if (CameraLeg != ECameraLeg::Opening)
	{
		return;
	}
	const EConstructCameraState State = Webcam->GetCameraState();
	const EConstructCameraBlockedReason Reason = Webcam->GetBlockedReason();
	// On counts only once the state has left the test source's On, so it is the device that opened.
	bCameraLeftOn |= State != EConstructCameraState::On;
	if (State == EConstructCameraState::On && bCameraLeftOn)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: source: camera, device opened"));
		CameraLeg = ECameraLeg::Opened;
		return;
	}
	if (State == EConstructCameraState::Unavailable && Reason == EConstructCameraBlockedReason::NoCamera)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: source: camera, not run: no camera found"));
		CameraLeg = ECameraLeg::NotRun;
		return;
	}
	if (State == EConstructCameraState::Unavailable || State == EConstructCameraState::Off)
	{
		UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer9Check: source: camera, failed: %s"), *CameraStateText(State, Reason));
		CameraLeg = ECameraLeg::Failed;
		return;
	}
	if (Now - CameraLegStartedAt > Layer9CameraOpenSeconds)
	{
		UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer9Check: source: camera, failed: still %s after %.0f s"),
		       *CameraStateText(State, Reason), Layer9CameraOpenSeconds);
		CameraLeg = ECameraLeg::Failed;
	}
}

bool UConstructLayer9CheckSubsystem::DrawNextFrame(TArray<FColor>& OutPixels)
{
	if (Stage != EStage::Run || CameraLeg != ECameraLeg::TestSource)
	{
		return false;
	}
	// One frame per pull, never ahead of 10 a second; a late pull sends the next frame rather than skipping one.
	const double Elapsed = FPlatformTime::Seconds() - GoAt;
	int32 Sequence = 0;
	if (Elapsed >= Layer9PatternStart && Elapsed < Layer9PatternEnd && PatternDrawn < Layer9PatternFrames)
	{
		if (PatternDrawn > FMath::FloorToInt32((Elapsed - Layer9PatternStart) / Layer9FrameSeconds))
		{
			return false;
		}
		Sequence = ++PatternDrawn;
	}
	else if (Elapsed >= Layer9StreamStart && Elapsed < Layer9CameraStart)
	{
		if (StreamDrawn > FMath::FloorToInt32((Elapsed - Layer9StreamStart) / Layer9FrameSeconds) ||
		    Layer9PatternFrames + StreamDrawn >= Layer9MaxSequence)
		{
			return false;
		}
		Sequence = Layer9PatternFrames + ++StreamDrawn;
	}
	else
	{
		return false;
	}
	LastDrawn = Sequence;
	DrawSequence(Sequence, OutPixels);
	return true;
}

void UConstructLayer9CheckSubsystem::HandleSent(const FCrowdyVideoFrameSend& Send)
{
	// The webcam subsystem pulls the next frame only after this one's send, so the last frame drawn is the one sent.
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: sent %d: %s"), LastDrawn,
	       *StaticEnum<ECrowdyVideoSendResult>()->GetNameStringByValue(static_cast<int64>(Send.Result)));
}

void UConstructLayer9CheckSubsystem::HandleDecoded(APawn* Pawn, const TArray<FColor>& Pixels, const FIntPoint Size)
{
	if (Stage != EStage::Run)
	{
		return;
	}
	const UCrowdyEntityComponent* Component = Pawn ? Pawn->FindComponentByClass<UCrowdyEntityComponent>() : nullptr;
	if (!Component || !PeerId.IsValid() || Component->GetNetID() != PeerId)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: a frame from another sender, ignored"));
		return;
	}
	const int32 Sequence = ReadSequence(Pixels, Size);
	if (Sequence > 0)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: got %d"), Sequence);
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: got bad %s"),
	       LumaDeviation(Pixels) > LumaVariedThreshold ? TEXT("varied") : TEXT("flat"));
}

bool UConstructLayer9CheckSubsystem::MoveTo(const FVector& Spot, const TCHAR* Name)
{
	AConstructLayer9CheckPawn* Pawn = FindLocalPawn();
	const UCrowdyGameSession* Session = GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyGameSession>();
	FInt64Vector Chunk;
	if (!Pawn || !Session || !Pawn->SetActorLocation(Spot) || !Session->TryGetLocalPlayerChunk(GetWorld(), Chunk))
	{
		Fail(FString::Printf(TEXT("the pawn could not be placed at the %s spot"), Name));
		return false;
	}
	// The chunk the SDK addresses sends from, so the judge sees where each frame left from and arrived at.
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: at %s spot, chunk %lld %lld %lld"), Name, Chunk.X, Chunk.Y,
	       Chunk.Z);
	return true;
}

AConstructLayer9CheckPawn* UConstructLayer9CheckSubsystem::FindLocalPawn() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AConstructLayer9CheckPawn* Pawn = Controller ? Cast<AConstructLayer9CheckPawn>(Controller->GetPawn()) : nullptr;
	const UCrowdyEntityComponent* Component = Pawn ? Pawn->FindComponentByClass<UCrowdyEntityComponent>() : nullptr;
	return Component && Component->GetRole() == ECrowdyRole::Owner ? Pawn : nullptr;
}

FGuid UConstructLayer9CheckSubsystem::FindVisiblePeer() const
{
	for (TActorIterator<AConstructLayer9CheckPawn> It(GetWorld()); It; ++It)
	{
		const UCrowdyEntityComponent* Component = It->FindComponentByClass<UCrowdyEntityComponent>();
		if (Component && Component->GetRole() == ECrowdyRole::RemoteProxy && !It->IsHidden())
		{
			return Component->GetNetID();
		}
	}
	return FGuid();
}

void UConstructLayer9CheckSubsystem::HandleFrameArrived(const FCrowdyVideoFrame& Frame)
{
	if (Stage == EStage::Run && bWindowOpen && PeerId.IsValid() && Frame.SenderUUID == PeerId)
	{
		++WindowArrivals;
	}
}

void UConstructLayer9CheckSubsystem::HandleVideoRefused(const FCrowdySendRefusal& Refusal)
{
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer9Check: refused video send"));
}

void UConstructLayer9CheckSubsystem::LogDone()
{
	Stage = EStage::Done;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer9Check: done"));
}

void UConstructLayer9CheckSubsystem::Fail(const FString& Why)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer9Check: FAIL: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
