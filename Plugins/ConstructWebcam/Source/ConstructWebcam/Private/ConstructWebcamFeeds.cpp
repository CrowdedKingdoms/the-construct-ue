#include "ConstructWebcamFeeds.h"

#include "ConstructDeveloperSettings.h"
#include "ConstructPresencePawn.h"
#include "ConstructWebcamCore.h"
#include "ConstructWebcamLog.h"
#include "ConstructWebcamSettingsReply.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Replication/Subsystems/CrowdyEntitySubsystem.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "Tasks/Task.h"

bool UConstructWebcamFeeds::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UConstructWebcamFeeds::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCrowdyEntitySubsystem>();
	Collection.InitializeDependency<UCrowdyActorTracker>();
	Collection.InitializeDependency<UCrowdyVideoFrameReceiver>();
	bEnabled = GetDefault<UConstructDeveloperSettings>()->bEnableWebcam;
}

void UConstructWebcamFeeds::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UGameInstance* Instance = InWorld.GetGameInstance();
	if (!bEnabled || &InWorld != GetWorld() || !Instance || ObservedInstance.IsValid())
	{
		return;
	}
	ObservedInstance = Instance;
	ImageWrapper = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
	{
		SDK->OnLogin.AddUniqueDynamic(this, &ThisClass::HandleLogin);
		SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::HandleLogout);
	}
}

void UConstructWebcamFeeds::Deinitialize()
{
	UnbindFrames();
	if (UGameInstance* Instance = ObservedInstance.Get())
	{
		if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
		{
			SDK->OnLogin.RemoveDynamic(this, &ThisClass::HandleLogin);
			SDK->OnLogout.RemoveDynamic(this, &ThisClass::HandleLogout);
		}
	}
	ObservedInstance.Reset();
	bShowVideo = false;
	bReceiveAllowed = false;
	SettingsReply = nullptr;
	ClearFeeds(false);
	StopRefreshTicker();
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	Super::Deinitialize();
}

void UConstructWebcamFeeds::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UConstructWebcamFeeds* This = CastChecked<UConstructWebcamFeeds>(InThis);
	for (FFeed& Feed : This->Feeds)
	{
		Collector.AddReferencedObject(Feed.Texture, This);
	}
	Super::AddReferencedObjects(InThis, Collector);
}

void UConstructWebcamFeeds::SetShowVideo(bool bShow)
{
	if (!bEnabled || bShowVideo == bShow)
	{
		return;
	}
	bShowVideo = bShow;
	bReceiveAllowed = false;
	if (!bShow)
	{
		StopRefreshTicker();
		UnbindFrames();
		UE_CLOG(IsConstructWebcamTraceOn(), LogConstructWebcam, Log,
		        TEXT("ConstructWebcam: feeds off, %lld undecodable and %lld dropped frames so far"), UndecodableFrames,
		        DroppedFrames);
		ClearFeeds(true);
		return;
	}
	if (!ImageWrapper)
	{
		ImageWrapper = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	}
	BindFrames();
	ClearFeeds(true);
	RequestReceiveSetting();
	if (!RefreshHandle.IsValid())
	{
		RefreshHandle = FTSTicker::GetCoreTicker().AddTicker(
		    FTickerDelegate::CreateUObject(this, &ThisClass::TickRefresh), ConstructWebcam::SettingsRefreshSeconds);
	}
}

void UConstructWebcamFeeds::RefreshSettings()
{
	if (bShowVideo)
	{
		RequestReceiveSetting();
	}
}

bool UConstructWebcamFeeds::TickRefresh(float DeltaTime)
{
	RefreshSettings();
	return true;
}

void UConstructWebcamFeeds::StopRefreshTicker()
{
	if (!RefreshHandle.IsValid())
	{
		return;
	}
	FTSTicker::GetCoreTicker().RemoveTicker(RefreshHandle);
	RefreshHandle.Reset();
}

FConstructWebcamFeed UConstructWebcamFeeds::GetFeedInfo(APawn* Pawn) const
{
	FConstructWebcamFeed Info;
	if (!Pawn || !IsReceiving())
	{
		return Info;
	}
	for (const FFeed& Feed : Feeds)
	{
		if (Feed.Pawn == Pawn && Feed.bShown && Feed.Texture)
		{
			Info.Texture = Feed.Texture;
			Info.SenderId = Feed.SenderId;
			Info.Generation = Feed.Generation;
			return Info;
		}
	}
	return Info;
}

void UConstructWebcamFeeds::BindFrames()
{
	UWorld* World = GetWorld();
	if (bFramesBound || !World)
	{
		return;
	}
	bFramesBound = true;
	Receiver = World->GetSubsystem<UCrowdyVideoFrameReceiver>();
	Tracker = World->GetSubsystem<UCrowdyActorTracker>();
	Entities = World->GetSubsystem<UCrowdyEntitySubsystem>();
	if (UCrowdyVideoFrameReceiver* Frames = Receiver.Get())
	{
		Frames->OnVideoFrameAssembled.AddUniqueDynamic(this, &ThisClass::HandleFrame);
	}
	if (UCrowdyActorTracker* Actors = Tracker.Get())
	{
		Actors->OnRemoteEntityLeft.AddUniqueDynamic(this, &ThisClass::HandleActorLeft);
		Actors->OnRemoteEntityLeftAnnounced.AddUniqueDynamic(this, &ThisClass::HandleActorLeft);
		Actors->OnRemoteEntityTimedOut.AddUniqueDynamic(this, &ThisClass::HandleActorTimedOut);
	}
	if (UCrowdyEntitySubsystem* Registry = Entities.Get())
	{
		Registry->OnEntityUnregistered.AddUniqueDynamic(this, &ThisClass::HandleEntityUnregistered);
	}
}

void UConstructWebcamFeeds::UnbindFrames()
{
	if (!bFramesBound)
	{
		return;
	}
	bFramesBound = false;
	if (UCrowdyVideoFrameReceiver* Frames = Receiver.Get())
	{
		Frames->OnVideoFrameAssembled.RemoveDynamic(this, &ThisClass::HandleFrame);
	}
	if (UCrowdyActorTracker* Actors = Tracker.Get())
	{
		Actors->OnRemoteEntityLeft.RemoveDynamic(this, &ThisClass::HandleActorLeft);
		Actors->OnRemoteEntityLeftAnnounced.RemoveDynamic(this, &ThisClass::HandleActorLeft);
		Actors->OnRemoteEntityTimedOut.RemoveDynamic(this, &ThisClass::HandleActorTimedOut);
	}
	if (UCrowdyEntitySubsystem* Registry = Entities.Get())
	{
		Registry->OnEntityUnregistered.RemoveDynamic(this, &ThisClass::HandleEntityUnregistered);
	}
}

void UConstructWebcamFeeds::RequestReceiveSetting()
{
	const int32 Token = ReceiveGeneration;
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.ReadSettings)
	{
		Hooks.ReadSettings(Token);
		return;
	}
#endif
	UGameInstance* Instance = ObservedInstance.Get();
	UCrowdySafety* Safety = Instance ? Instance->GetSubsystem<UCrowdySafety>() : nullptr;
	if (!Safety)
	{
		return;
	}
	TWeakObjectPtr<UConstructWebcamFeeds> WeakThis(this);
	SettingsReply = UConstructWebcamSettingsReply::Read(*Safety, this,
	    [WeakThis, Token](const FCrowdyCommunicationSettings* Settings, const FCrowdySafetyError* Error)
	    {
		    if (UConstructWebcamFeeds* Self = WeakThis.Get())
		    {
			    Self->ReceiveSettingAnswered(Token, Settings);
		    }
	    });
}

void UConstructWebcamFeeds::ReceiveSettingAnswered(int32 Token, const FCrowdyCommunicationSettings* Settings)
{
	// A failed read keeps what was last known, which is off until a read has landed.
	if (Token != ReceiveGeneration || !bShowVideo || !Settings)
	{
		return;
	}
	bReceiveAllowed = Settings->VideoReceive != ECrowdyCommunicationScope::Off;
	if (!bReceiveAllowed)
	{
		ClearFeeds(true);
	}
}

void UConstructWebcamFeeds::ClearFeeds(bool bNotify)
{
	++ReceiveGeneration;
	for (const FFeed& Feed : Feeds)
	{
		if (bNotify && Feed.bShown)
		{
			PendingNotify.Add(Feed.Pawn);
		}
	}
	Feeds.Reset();
	if (bNotify)
	{
		FlushNotify();
		return;
	}
	PendingNotify.Reset();
}

void UConstructWebcamFeeds::HandleFrame(const FCrowdyVideoFrame& Frame)
{
	if (!IsReceiving())
	{
		return;
	}
	if (Frame.Codec != ECrowdyVideoCodec::Jpeg)
	{
		++UndecodableFrames;
		return;
	}
	APawn* Pawn = Cast<AConstructPresencePawn>(FindSenderActor(Frame.SenderUUID));
	FCrowdyActorId PawnId;
	FCrowdyActorId FrameSender;
	const bool bKnownSender = Pawn && !Pawn->IsHidden() && !Pawn->IsActorBeingDestroyed() && FindPawnId(*Pawn, PawnId) &&
	                          FCrowdyActorId::TryFromString(Frame.SenderId, FrameSender) && FrameSender == PawnId;
	if (!bKnownSender)
	{
		++DroppedFrames;
		return;
	}
	FFeed& Feed = BindFeed(Frame, *Pawn, PawnId);
	Feed.Pending = Frame.Bytes;
	EnsureTicker();
	StartDecode(Feed);
	FlushNotify();
}

UConstructWebcamFeeds::FFeed& UConstructWebcamFeeds::BindFeed(const FCrowdyVideoFrame& Frame,
                                                              APawn& Pawn,
                                                              const FCrowdyActorId& PawnId)
{
	// One pawn shows one sender, and one sender shows on one pawn; any other pairing starts a new feed.
	for (int32 Index = Feeds.Num() - 1; Index >= 0; --Index)
	{
		const bool bSamePawn = Feeds[Index].Pawn == &Pawn;
		const bool bSameSender = Feeds[Index].PawnId == PawnId;
		if (bSamePawn != bSameSender)
		{
			ReleaseFeedAt(Index);
		}
	}
	if (FFeed* Existing = FindFeed(PawnId))
	{
		return *Existing;
	}
	FFeed& Feed = Feeds.AddDefaulted_GetRef();
	Feed.PawnId = PawnId;
	Feed.Pawn = &Pawn;
	Feed.SenderId = Frame.SenderId;
	Feed.UUID = Frame.SenderUUID;
	Feed.Generation = ++FeedGenerationCounter;
	return Feed;
}

UConstructWebcamFeeds::FFeed* UConstructWebcamFeeds::FindFeed(const FCrowdyActorId& PawnId)
{
	return Feeds.FindByPredicate([&PawnId](const FFeed& Feed) { return Feed.PawnId == PawnId; });
}

void UConstructWebcamFeeds::ReleaseFeedAt(int32 Index)
{
	if (Feeds[Index].bShown)
	{
		PendingNotify.Add(Feeds[Index].Pawn);
	}
	Feeds.RemoveAtSwap(Index);
}

void UConstructWebcamFeeds::ReleaseFeedsFor(const FGuid& UUID)
{
	for (int32 Index = Feeds.Num() - 1; Index >= 0; --Index)
	{
		if (Feeds[Index].UUID == UUID)
		{
			ReleaseFeedAt(Index);
		}
	}
	FlushNotify();
}

bool UConstructWebcamFeeds::IsFeedPawnCurrent(const FFeed& Feed) const
{
	const APawn* Pawn = Feed.Pawn.Get();
	FCrowdyActorId CurrentId;
	return Pawn && !Pawn->IsHidden() && !Pawn->IsActorBeingDestroyed() && FindPawnId(*Pawn, CurrentId) &&
	       CurrentId == Feed.PawnId;
}

void UConstructWebcamFeeds::StartDecode(FFeed& Feed)
{
	const double Time = Now();
	if (Feed.bDecoding || Feed.Pending.IsEmpty() || !ImageWrapper)
	{
		return;
	}
	// At most 10 decodes in any second, so frames that arrive close together are not lost to jitter.
	Feed.RecentDecodes.RemoveAll([Time](double At) { return Time - At >= 1.0; });
	if (Feed.RecentDecodes.Num() >= ConstructWebcam::MaxDecodesPerSecond)
	{
		return;
	}
	Feed.bDecoding = true;
	Feed.RecentDecodes.Add(Time);
	TArray<uint8> Bytes = MoveTemp(Feed.Pending);
	Feed.Pending.Reset();

	const FCrowdyActorId PawnId = Feed.PawnId;
	const TWeakObjectPtr<APawn> Pawn = Feed.Pawn;
	const int32 FeedGeneration = Feed.Generation;
	const int32 Receive = ReceiveGeneration;
	IImageWrapperModule* Wrapper = ImageWrapper;
	TWeakObjectPtr<UConstructWebcamFeeds> WeakThis(this);
	Dispatch([WeakThis, PawnId, Pawn, FeedGeneration, Receive, Wrapper, Bytes = MoveTemp(Bytes)]()
	{
		TArray<FColor> Pixels;
		FIntPoint PictureSize;
		const bool bDecoded = DecodeWebcamJpeg(*Wrapper, Bytes, Pixels, PictureSize);
		RunOnWebcamGameThread([WeakThis, PawnId, Pawn, FeedGeneration, Receive, bDecoded, Pixels = MoveTemp(Pixels)]() mutable
		{
			if (UConstructWebcamFeeds* Self = WeakThis.Get())
			{
				Self->ApplyDecode(PawnId, Pawn, FeedGeneration, Receive, bDecoded, MoveTemp(Pixels));
			}
		});
	});
}

void UConstructWebcamFeeds::ApplyDecode(const FCrowdyActorId& PawnId,
                                        TWeakObjectPtr<APawn> Pawn,
                                        int32 FeedGeneration,
                                        int32 Receive,
                                        bool bDecoded,
                                        TArray<FColor> Pixels)
{
	FFeed* Feed = FindFeed(PawnId);
	if (Receive != ReceiveGeneration || !Feed || Feed->Generation != FeedGeneration || Feed->Pawn != Pawn)
	{
		return;
	}
	Feed->bDecoding = false;
	if (!IsReceiving() || !IsFeedPawnCurrent(*Feed))
	{
		ReleaseFeedAt(static_cast<int32>(Feed - Feeds.GetData()));
		FlushNotify();
		return;
	}
	if (!bDecoded)
	{
		++UndecodableFrames;
		StartDecode(*Feed);
		return;
	}
	const FIntPoint FrameSize(ConstructWebcam::FrameWidth, ConstructWebcam::FrameHeight);
	UTexture2D* Previous = Feed->Texture;
	Feed->Texture = WriteWebcamTexture(Previous, Pixels, FrameSize);
	Feed->LastShownAt = Now();
	if (Feed->Texture != Previous || !Feed->bShown)
	{
		PendingNotify.Add(Pawn);
	}
	Feed->bShown = true;
	StartDecode(*Feed);
	FlushNotify();
	if (DecodedObserver && Pawn.IsValid())
	{
		DecodedObserver(Pawn.Get(), Pixels, FrameSize);
	}
}

void UConstructWebcamFeeds::FlushNotify()
{
	TArray<TWeakObjectPtr<APawn>> Pawns = MoveTemp(PendingNotify);
	PendingNotify.Reset();
	for (const TWeakObjectPtr<APawn>& Pawn : Pawns)
	{
		if (APawn* Live = Pawn.Get())
		{
			OnFeedChanged.Broadcast(Live);
		}
	}
}

bool UConstructWebcamFeeds::TickFeeds(float DeltaTime)
{
	const double Time = Now();
	TArray<FCrowdyActorId, TInlineAllocator<8>> Waiting;
	for (int32 Index = Feeds.Num() - 1; Index >= 0; --Index)
	{
		FFeed& Feed = Feeds[Index];
		if (!IsFeedPawnCurrent(Feed))
		{
			ReleaseFeedAt(Index);
			continue;
		}
		if (Feed.bShown && Time - Feed.LastShownAt >= ConstructWebcam::FeedExpirySeconds)
		{
			Feed.bShown = false;
			PendingNotify.Add(Feed.Pawn);
		}
		if (!Feed.Pending.IsEmpty())
		{
			Waiting.Add(Feed.PawnId);
		}
	}
	for (const FCrowdyActorId& PawnId : Waiting)
	{
		if (FFeed* Feed = FindFeed(PawnId))
		{
			StartDecode(*Feed);
		}
	}
	FlushNotify();
	if (Feeds.IsEmpty() && TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	return TickHandle.IsValid();
}

void UConstructWebcamFeeds::EnsureTicker()
{
	if (TickHandle.IsValid())
	{
		return;
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickFeeds),
	                                                  ConstructWebcam::FrameInterval);
}

AActor* UConstructWebcamFeeds::FindSenderActor(const FGuid& UUID) const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.FindActor)
	{
		return Hooks.FindActor(UUID);
	}
#endif
	const UCrowdyEntitySubsystem* Registry = Entities.Get();
	return Registry ? Registry->FindEntity(UUID) : nullptr;
}

bool UConstructWebcamFeeds::FindPawnId(const AActor& Actor, FCrowdyActorId& OutId) const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.FindPawnId)
	{
		return Hooks.FindPawnId(Actor, OutId);
	}
#endif
	const UCrowdyEntitySubsystem* Registry = Entities.Get();
	if (!Registry)
	{
		return false;
	}
	// A player's presence pawn sends video under its entity id written as 32 hex digits, the session's own id.
	const FGuid NetID = Registry->FindEntityID(&Actor);
	return NetID.IsValid() && FCrowdyActorId::TryFromString(NetID.ToString(EGuidFormats::Digits), OutId);
}

double UConstructWebcamFeeds::Now() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.Now)
	{
		return Hooks.Now();
	}
#endif
	return FPlatformTime::Seconds();
}

void UConstructWebcamFeeds::Dispatch(TUniqueFunction<void()> Job)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Hooks.RunWork)
	{
		Hooks.RunWork(MoveTemp(Job));
		return;
	}
#endif
	UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Job));
}

void UConstructWebcamFeeds::HandleActorLeft(const FCrowdyActorLeft& ActorLeft, int32 ActorCount)
{
	ReleaseFeedsFor(ActorLeft.UUID);
}

void UConstructWebcamFeeds::HandleActorTimedOut(FGuid UUID, int32 ActorCount)
{
	ReleaseFeedsFor(UUID);
}

void UConstructWebcamFeeds::HandleEntityUnregistered(const FGuid& NetID)
{
	ReleaseFeedsFor(NetID);
}

void UConstructWebcamFeeds::HandleLogin(bool bSuccess, FString Message)
{
	if (!bSuccess || !bShowVideo)
	{
		return;
	}
	bReceiveAllowed = false;
	ClearFeeds(true);
	RequestReceiveSetting();
}

void UConstructWebcamFeeds::HandleLogout(bool bSuccess)
{
	bReceiveAllowed = false;
	ClearFeeds(true);
}
