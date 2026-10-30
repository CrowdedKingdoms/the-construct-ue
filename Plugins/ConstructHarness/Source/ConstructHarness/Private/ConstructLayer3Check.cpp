#include "ConstructLayer3Check.h"

#include "ConstructHarnessLog.h"
#include "Core/Audio/VoiceChat/VoiceChatSubsystem.h"
#include "Core/UDP/Enums/ECrowdyMessageType.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
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
	const TCHAR* const Layer3MapName = TEXT("L_Layer3Check");
	constexpr int32 Layer3LineChars = 240;
	constexpr int32 Layer3LastLine = 21;
	constexpr int32 Layer3TextDistance = 4;
	constexpr int32 Layer3VoiceDistance = 1;
	constexpr int32 Layer3NearChunkX = 6;
	constexpr int32 Layer3FarChunks = 6;
	constexpr double Layer3ListenerOffset = 400.0;
	constexpr double Layer3PollSeconds = 0.05;
	constexpr double Layer3SettleSeconds = 4.0;
	constexpr double Layer3DoneAt = 62.0;

	// Each feed is 10 s of 20 ms frames: 960 samples of 48 kHz mono, a 440 Hz tone at -12 dBFS.
	constexpr int32 Layer3FeedCount = 3;
	constexpr double Layer3FeedStarts[Layer3FeedCount] = {8.0, 30.0, 48.0};
	constexpr int32 Layer3FeedFrames = 500;
	constexpr int32 Layer3FrameSamples = 960;
	constexpr double Layer3FrameSeconds = 0.02;
	constexpr double Layer3SampleRate = 48000.0;
	constexpr double Layer3ToneHz = 440.0;
	constexpr double Layer3ToneAmplitude = 32767.0 * 0.25118864315;

	const TCHAR* const Layer3PhaseNames[Layer3FeedCount] = {TEXT("near"), TEXT("far"), TEXT("back-near")};
	constexpr double Layer3WindowStarts[Layer3FeedCount] = {0.0, 22.0, 44.0};
	constexpr double Layer3WindowEnds[Layer3FeedCount] = {20.0, 42.0, 62.0};
	constexpr int32 Layer3MoveCount = 2;
	constexpr double Layer3MoveTimes[Layer3MoveCount] = {20.0, 42.0};

	double Layer3LineTime(const int32 N)
	{
		if (N <= 10)
		{
			return 2.0 + 0.5 * (N - 1);
		}
		return N <= 20 ? 24.0 + 0.5 * (N - 11) : 46.0;
	}
}

bool AConstructLayer3CheckPawn::ShouldUseLocalCharacterMovement() const
{
	return false;
}

AConstructLayer3CheckGameMode::AConstructLayer3CheckGameMode()
{
	DefaultPawnClass = AConstructLayer3CheckPawn::StaticClass();
}

FString UConstructLayer3CheckSubsystem::CheckLine(const int32 N)
{
	static const TCHAR Pattern[] = TEXT("abcdefghijklmnopqrstuvwxyz0123456789");
	constexpr int32 PatternLength = UE_ARRAY_COUNT(Pattern) - 1;
	FString Line = FString::Printf(TEXT("line %d "), N);
	Line.Reserve(Layer3LineChars);
	for (int32 Index = Line.Len(); Index < Layer3LineChars; ++Index)
	{
		Line.AppendChar(Pattern[(Index + N) % PatternLength]);
	}
	return Line;
}

int32 UConstructLayer3CheckSubsystem::CheckLineNumber(const FString& Text)
{
	static const FString Prefix = TEXT("line ");
	if (!Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
	{
		return 0;
	}
	int32 Digits = 0;
	while (Digits < 3 && Prefix.Len() + Digits < Text.Len() && FChar::IsDigit(Text[Prefix.Len() + Digits]))
	{
		++Digits;
	}
	if (Digits == 0 || Prefix.Len() + Digits >= Text.Len() || Text[Prefix.Len() + Digits] != TEXT(' '))
	{
		return 0;
	}
	return FCString::Atoi(*Text.Mid(Prefix.Len(), Digits));
}

bool UConstructLayer3CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->WorldType == EWorldType::Game;
}

void UConstructLayer3CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UGameInstance* GameInstance = InWorld.GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	FString Role;
	const bool bHasRole = FParse::Value(FCommandLine::Get(), TEXT("Layer3CheckRole="), Role);
	if (InWorld.GetMapName() != Layer3MapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer3Check.
		if (bHasRole)
		{
			Fail(FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), Layer3MapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	double TimeoutMinutes = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("Layer3CheckTimeoutMinutes="), TimeoutMinutes) && TimeoutMinutes > 0.0)
	{
		TimeoutSeconds = TimeoutMinutes * 60.0;
	}
	// Every frame, so the tone can be fed in real time.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);

	if (Role != TEXT("talker") && Role != TEXT("listener"))
	{
		Fail(TEXT("-Layer3CheckRole=talker or listener is missing; start the check with Tools/Layer3Check"));
		return;
	}
	if (!FParse::Value(FCommandLine::Get(), TEXT("Layer3CheckControl="), ControlPath) || ControlPath.IsEmpty())
	{
		Fail(TEXT("-Layer3CheckControl=<path> is missing; start the check with Tools/Layer3Check"));
		return;
	}
	// Tools/Layer3Check writes the file before launch, so a path cut short at a space fails here, not at go.
	if (!FPaths::FileExists(ControlPath))
	{
		Fail(TEXT("the -Layer3CheckControl file does not exist"));
		return;
	}
	bTalker = Role == TEXT("talker");

	Chat = GameInstance->GetSubsystem<UCrowdyProximityChat>();
	Replication = GameInstance->GetSubsystem<UCrowdyCppReplicationSubsystem>();
	Voice = InWorld.GetSubsystem<UVoiceChatSubsystem>();
	if (!Chat.IsValid() || !Replication.IsValid() || !Voice.IsValid())
	{
		Fail(TEXT("the proximity chat, realtime or voice subsystem is missing"));
		return;
	}
	Chat->OnProximityTextReceived.AddUniqueDynamic(this, &ThisClass::HandleText);
	Chat->OnProximityTextRefused.AddUniqueDynamic(this, &ThisClass::HandleTextRefused);
	Replication->OnSendRefused.AddUniqueDynamic(this, &ThisClass::HandleSendRefused);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: started as %s"), *Role);
}

void UConstructLayer3CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0.
	if (TickerHandle.IsValid() && Stage != EStage::Done)
	{
		Fail(TEXT("the check's world ended before it finished"));
	}
	if (UCrowdyProximityChat* ActiveChat = Chat.Get())
	{
		ActiveChat->OnProximityTextReceived.RemoveDynamic(this, &ThisClass::HandleText);
		ActiveChat->OnProximityTextRefused.RemoveDynamic(this, &ThisClass::HandleTextRefused);
	}
	if (UCrowdyCppReplicationSubsystem* ActiveReplication = Replication.Get())
	{
		ActiveReplication->OnSendRefused.RemoveDynamic(this, &ThisClass::HandleSendRefused);
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer3CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out after %.0f minutes in stage %d"), TimeoutSeconds / 60.0,
		                              static_cast<int32>(Stage)));
		return false;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = GameInstance ? GameInstance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = GameInstance ? GameInstance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	const UCrowdyGameSession* Session = GameInstance ? GameInstance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	if (!Auth || !SDK || !Session || !Voice.IsValid() || !Chat.IsValid())
	{
		Fail(TEXT("the Crowdy SDK subsystems are missing"));
		return false;
	}

	const AConstructLayer3CheckPawn* Pawn = FindLocalPawn();
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
		const FVector ChunkCentre = FVector(Layer3NearChunkX + 0.5, 0.5, 0.5) * ChunkSize;
		NearSpot = bTalker ? ChunkCentre : ChunkCentre + FVector(Layer3ListenerOffset, 0.0, 0.0);
		FarSpot = NearSpot + FVector(Layer3FarChunks * ChunkSize, 0.0, 0.0);
		if (!MoveTo(NearSpot, TEXT("near")))
		{
			return false;
		}
		SettleStartedAt = Now;
		Stage = EStage::Settle;
		return true;
	}
	if (Stage == EStage::Settle)
	{
		// The first line goes out 2 s after go, so this keeps every send 4 s clear of the near move.
		if (Now - SettleStartedAt >= Layer3SettleSeconds)
		{
			UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: stimulus landed: signed in, connected, other player seen"));
			Stage = EStage::WaitGo;
		}
		return true;
	}
	if (Stage == EStage::WaitGo)
	{
		if (ReadGo(Now))
		{
			StartRun(Now);
		}
		return Stage != EStage::Failed;
	}

	const double Elapsed = Now - GoAt;
	if (bTalker)
	{
		TickTalker(Elapsed, Now);
	}
	else
	{
		TickListener(Elapsed);
	}
	return Stage == EStage::Run;
}

bool UConstructLayer3CheckSubsystem::ReadGo(const double Now)
{
	if (Now - LastPollAt < Layer3PollSeconds)
	{
		return false;
	}
	LastPollAt = Now;
	FString Control;
	return FFileHelper::LoadFileToString(Control, *ControlPath) && Control.TrimStartAndEnd() == TEXT("go");
}

void UConstructLayer3CheckSubsystem::StartRun(const double Now)
{
	GoAt = Now;
	Stage = EStage::Run;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: go"));
	if (!bTalker)
	{
		return;
	}
	UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	GameInstance->GetSubsystem<UCrowdySDKSubsystem>()->SetVoiceChatDistance(Layer3VoiceDistance);
	// Read back from this world's voice chat, which addresses the packets, not from the setting kept across maps.
	const int32 Distance = Voice->GetDistance();
	if (Distance != Layer3VoiceDistance)
	{
		Fail(FString::Printf(TEXT("the voice distance reads %d after setting %d"), Distance, Layer3VoiceDistance));
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: voice distance %d"), Distance);
}

void UConstructLayer3CheckSubsystem::TickTalker(const double Elapsed, const double Now)
{
	while (NextLine <= Layer3LastLine && Elapsed >= Layer3LineTime(NextLine))
	{
		const ECrowdyProximityChatResult Result = Chat->SendProximityText(CheckLine(NextLine), Layer3TextDistance);
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: sent line %d: %s"), NextLine,
		       *StaticEnum<ECrowdyProximityChatResult>()->GetNameStringByValue(static_cast<int64>(Result)));
		++NextLine;
	}
	TickFeed(Elapsed, Now);
	if (Stage == EStage::Run && Elapsed >= Layer3DoneAt && FeedIndex >= Layer3FeedCount && NextLine > Layer3LastLine)
	{
		LogDone();
	}
}

void UConstructLayer3CheckSubsystem::TickFeed(const double Elapsed, const double Now)
{
	if (FeedIndex >= Layer3FeedCount || Elapsed < Layer3FeedStarts[FeedIndex])
	{
		return;
	}
	UVoiceChatSubsystem& VoiceChat = *Voice;
	if (!bFeeding)
	{
		bFeeding = true;
		FeedStartedAt = Now;
		FeedFrames = 0;
		FeedSentAtStart = VoiceChat.GetStats().PacketsSent;
		FeedSkippedAtStart = VoiceChat.GetStats().SilentFramesSkipped;
	}

	// Frames are pushed as they fall due, so a late frame catches up rather than shifting the feed.
	const int32 Due = FMath::Min(Layer3FeedFrames, FMath::FloorToInt32((Now - FeedStartedAt) / Layer3FrameSeconds) + 1);
	int16 Frame[Layer3FrameSamples];
	while (FeedFrames < Due)
	{
		for (int16& Sample : Frame)
		{
			const double Phase = 2.0 * UE_DOUBLE_PI * Layer3ToneHz * static_cast<double>(FeedSample++) / Layer3SampleRate;
			Sample = static_cast<int16>(FMath::RoundToInt32(FMath::Sin(Phase) * Layer3ToneAmplitude));
		}
		if (!VoiceChat.FeedVoicePcm(MakeArrayView(Frame)))
		{
			Fail(TEXT("the voice chat refused the feed"));
			return;
		}
		++FeedFrames;
	}
	if (FeedFrames < Layer3FeedFrames)
	{
		return;
	}
	VoiceChat.EndVoiceFeed();
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: voice sent %lld skipped %lld (%s feed)"),
	       VoiceChat.GetStats().PacketsSent - FeedSentAtStart,
	       VoiceChat.GetStats().SilentFramesSkipped - FeedSkippedAtStart, Layer3PhaseNames[FeedIndex]);
	bFeeding = false;
	++FeedIndex;
}

void UConstructLayer3CheckSubsystem::TickListener(const double Elapsed)
{
	const int64 Received = Voice->GetStats().PacketsReceived;
	if (bWindowOpen && Elapsed >= Layer3WindowEnds[WindowIndex])
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: voice received %lld (%s window)"),
		       Received - WindowReceivedAtStart, Layer3PhaseNames[WindowIndex]);
		bWindowOpen = false;
		++WindowIndex;
	}
	if (!bWindowOpen && WindowIndex < Layer3FeedCount && Elapsed >= Layer3WindowStarts[WindowIndex])
	{
		bWindowOpen = true;
		WindowReceivedAtStart = Received;
	}
	if (MoveIndex < Layer3MoveCount && Elapsed >= Layer3MoveTimes[MoveIndex])
	{
		const bool bAway = MoveIndex == 0;
		++MoveIndex;
		if (!MoveTo(bAway ? FarSpot : NearSpot, bAway ? TEXT("far") : TEXT("near")))
		{
			return;
		}
	}
	if (WindowIndex >= Layer3FeedCount)
	{
		LogDone();
	}
}

bool UConstructLayer3CheckSubsystem::MoveTo(const FVector& Spot, const TCHAR* Name)
{
	AConstructLayer3CheckPawn* Pawn = FindLocalPawn();
	const UCrowdyGameSession* Session = GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyGameSession>();
	FInt64Vector Chunk;
	if (!Pawn || !Session || !Pawn->SetActorLocation(Spot) || !Session->TryGetLocalPlayerChunk(GetWorld(), Chunk))
	{
		Fail(FString::Printf(TEXT("the pawn could not be placed at the %s spot"), Name));
		return false;
	}
	// The chunk the SDK addresses sends from, so the judge sees where each stream left from.
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: at %s spot, chunk %lld %lld %lld"), Name, Chunk.X,
	       Chunk.Y, Chunk.Z);
	return true;
}

AConstructLayer3CheckPawn* UConstructLayer3CheckSubsystem::FindLocalPawn() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AConstructLayer3CheckPawn* Pawn = Controller ? Cast<AConstructLayer3CheckPawn>(Controller->GetPawn()) : nullptr;
	const UCrowdyEntityComponent* Component = Pawn ? Pawn->FindComponentByClass<UCrowdyEntityComponent>() : nullptr;
	return Component && Component->GetRole() == ECrowdyRole::Owner ? Pawn : nullptr;
}

FGuid UConstructLayer3CheckSubsystem::FindVisiblePeer() const
{
	for (TActorIterator<AConstructLayer3CheckPawn> It(GetWorld()); It; ++It)
	{
		const UCrowdyEntityComponent* Component = It->FindComponentByClass<UCrowdyEntityComponent>();
		if (Component && Component->GetRole() == ECrowdyRole::RemoteProxy && !It->IsHidden())
		{
			return Component->GetNetID();
		}
	}
	return FGuid();
}

void UConstructLayer3CheckSubsystem::HandleText(const FCrowdyProximityText& Message)
{
	if (Stage == EStage::Failed)
	{
		return;
	}
	// Anyone on the shared app may talk nearby; only the other check player's lines are judged.
	if (!PeerId.IsValid() || Message.SenderEntityId != PeerId)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: a line from another sender, ignored"));
		return;
	}
	const int32 Line = CheckLineNumber(Message.Text);
	const bool bMatch = Line > 0 && Message.Text.Equals(CheckLine(Line), ESearchCase::CaseSensitive);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: received line %d text: %s"), Line,
	       bMatch ? TEXT("match") : TEXT("mismatch"));
}

void UConstructLayer3CheckSubsystem::HandleTextRefused(const FCrowdySendRefusal& Refusal)
{
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer3Check: text refused"));
}

void UConstructLayer3CheckSubsystem::HandleSendRefused(const FCrowdySendRefusal& Refusal)
{
	if (Refusal.MessageType == static_cast<int32>(ECrowdyMessageType::CLIENT_AUDIO_PACKET))
	{
		UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer3Check: voice refused"));
	}
}

void UConstructLayer3CheckSubsystem::LogDone()
{
	Stage = EStage::Done;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer3Check: done"));
}

void UConstructLayer3CheckSubsystem::Fail(const FString& Why)
{
	if (Stage == EStage::Done || Stage == EStage::Failed)
	{
		return;
	}
	Stage = EStage::Failed;
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer3Check: FAIL: %s"), *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, 1);
}
