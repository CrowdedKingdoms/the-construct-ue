#include "StandInC9.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogStandInC9, Log, All);

namespace
{
constexpr int32 FixtureFrameCount = 8;
constexpr float LoopInterval = 0.1f;

bool IsUnavailableReason(EConstructCameraBlockedReason Reason)
{
	return Reason == EConstructCameraBlockedReason::WebcamsDisabled || Reason == EConstructCameraBlockedReason::NoCamera;
}

UTexture2D* DecodeFixture(IImageWrapperModule& Wrapper, const FString& Path)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return nullptr;
	}
	TSharedPtr<IImageWrapper> Image = Wrapper.CreateImageWrapper(EImageFormat::JPEG);
	if (!Image.IsValid() || !Image->SetCompressed(Bytes.GetData(), Bytes.Num()))
	{
		return nullptr;
	}
	TArray64<uint8> Raw;
	if (!Image->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return nullptr;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(Image->GetWidth(), Image->GetHeight(), PF_B8G8R8A8);
	if (!Texture || !Texture->GetPlatformData())
	{
		return nullptr;
	}
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Raw.GetData(), FMath::Min<int64>(Raw.Num(), Mip.BulkData.GetBulkDataSize()));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}
}

void UStandInC9::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadFixtures();
}

void UStandInC9::Deinitialize()
{
	bDeinitialized = true;
	FTSTicker::GetCoreTicker().RemoveTicker(LoopHandle);
	LoopHandle.Reset();
	Super::Deinitialize();
}

void UStandInC9::LoadFixtures()
{
	Frames.Reset();
	IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	bool bMissing = false;
	for (int32 Index = 1; Index <= FixtureFrameCount; ++Index)
	{
		const FString Path = FPaths::ProjectContentDir() / FString::Printf(TEXT("StandIn/C9/Frame%d.jpg"), Index);
		UTexture2D* Texture = DecodeFixture(Wrapper, Path);
		bMissing |= Texture == nullptr;
		Frames.Add(Texture);
	}
	if (bMissing)
	{
		UE_LOG(LogStandInC9, Warning, TEXT("Stand-in webcam frames are missing under Content/StandIn/C9; feeds show no picture."));
	}
}

UTexture2D* UStandInC9::GetLocalPreview() const
{
	if (CameraState != EConstructCameraState::On || !Frames.IsValidIndex(FrameIndex))
	{
		return nullptr;
	}
	return Frames[FrameIndex];
}

void UStandInC9::SetCameraState(EConstructCameraState State, EConstructCameraBlockedReason Reason)
{
	CameraState = State;
	BlockedReason = Reason;
	UpdateLoop();
	OnCameraStateChanged.Broadcast(State, Reason);
}

void UStandInC9::SetCameraOn(bool bOn)
{
	if (!bOn)
	{
		const bool bIdle = CameraState == EConstructCameraState::Unavailable
			|| (CameraState == EConstructCameraState::Off && BlockedReason == EConstructCameraBlockedReason::None);
		if (!bIdle)
		{
			SetCameraState(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
		}
		return;
	}
	if (FixtureBlock != EConstructCameraBlockedReason::None)
	{
		SetCameraState(IsUnavailableReason(FixtureBlock) ? EConstructCameraState::Unavailable : EConstructCameraState::Off,
		               FixtureBlock);
		return;
	}
	if (CameraState == EConstructCameraState::Starting || CameraState == EConstructCameraState::On)
	{
		return;
	}
	SetCameraState(EConstructCameraState::Starting, EConstructCameraBlockedReason::None);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		if (!bDeinitialized && CameraState == EConstructCameraState::Starting)
		{
			SetCameraState(EConstructCameraState::On, EConstructCameraBlockedReason::None);
		}
		return false;
	}));
}

void UStandInC9::SetShowVideo(bool bShow)
{
	if (bShowVideo == bShow)
	{
		return;
	}
	bShowVideo = bShow;
	PruneFeeds();
	TArray<TWeakObjectPtr<APawn>> Changed;
	for (FFixtureFeed& Feed : Feeds)
	{
		Feed.bExpired = false;
		Changed.Add(Feed.Pawn);
	}
	UpdateLoop();
	for (const TWeakObjectPtr<APawn>& Pawn : Changed)
	{
		if (APawn* Live = Pawn.Get())
		{
			OnFeedChanged.Broadcast(Live);
		}
	}
}

FConstructWebcamFeed UStandInC9::GetFeedInfo(APawn* Pawn) const
{
	FConstructWebcamFeed Info;
	const FFixtureFeed* Feed = FindFeed(Pawn);
	if (!Feed || !IsVisible(*Feed))
	{
		return Info;
	}
	Info.Texture = Frames.IsValidIndex(FrameIndex) ? Frames[FrameIndex] : nullptr;
	Info.SenderId = Feed->SenderId;
	Info.Generation = Feed->Generation;
	return Info;
}

void UStandInC9::ResolveActorOwner(FGuid ActorId, FStandInC9OwnerResolved OnResolved)
{
	EStandInC9OwnerResult Result = ForcedOwnerResult;
	if (!bOwnerForced)
	{
		bool bAlreadyAsked = false;
		AskedActors.Add(ActorId, &bAlreadyAsked);
		Result = bAlreadyAsked ? EStandInC9OwnerResult::Confirmed : EStandInC9OwnerResult::Unknown;
	}
	const int64 UserId = Result == EStandInC9OwnerResult::Confirmed ? FixtureUserId : 0;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, OnResolved, Result, UserId](float)
	{
		if (!bDeinitialized)
		{
			OnResolved.ExecuteIfBound(Result, UserId);
		}
		return false;
	}));
}

void UStandInC9::AddFixtureFeed(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	if (FindFeed(Pawn))
	{
		RestoreFixtureFeed(Pawn);
		return;
	}
	FFixtureFeed& Feed = Feeds.AddDefaulted_GetRef();
	Feed.Pawn = Pawn;
	Feed.Generation = NextGeneration++;
	Feed.SenderId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UpdateLoop();
	if (bShowVideo)
	{
		OnFeedChanged.Broadcast(Pawn);
	}
}

void UStandInC9::RemoveFixtureFeed(APawn* Pawn)
{
	const FFixtureFeed* Feed = FindFeed(Pawn);
	if (!Feed)
	{
		return;
	}
	const bool bWasVisible = IsVisible(*Feed);
	Feeds.RemoveAll([Pawn](const FFixtureFeed& Other) { return Other.Pawn == Pawn; });
	UpdateLoop();
	if (bWasVisible)
	{
		OnFeedChanged.Broadcast(Pawn);
	}
}

void UStandInC9::SetFixtureCameraBlock(EConstructCameraBlockedReason Reason)
{
	FixtureBlock = Reason;
	if (Reason == EConstructCameraBlockedReason::None)
	{
		if (BlockedReason != EConstructCameraBlockedReason::None)
		{
			SetCameraState(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
		}
		return;
	}
	if (IsUnavailableReason(Reason))
	{
		if (CameraState != EConstructCameraState::Unavailable || BlockedReason != Reason)
		{
			SetCameraState(EConstructCameraState::Unavailable, Reason);
		}
		return;
	}
	const bool bRunning = CameraState == EConstructCameraState::Starting || CameraState == EConstructCameraState::On;
	if (bRunning)
	{
		SetCameraState(EConstructCameraState::Off, Reason);
		return;
	}
	if (CameraState == EConstructCameraState::Unavailable)
	{
		SetCameraState(EConstructCameraState::Off, EConstructCameraBlockedReason::None);
	}
}

void UStandInC9::SetFixtureRecording(bool bRecorded)
{
	if (bRecording == bRecorded)
	{
		return;
	}
	bRecording = bRecorded;
	OnCameraStateChanged.Broadcast(CameraState, BlockedReason);
}

void UStandInC9::SetFixtureOwner(EStandInC9OwnerResult Result, int64 UserId)
{
	bOwnerForced = true;
	ForcedOwnerResult = Result;
	FixtureUserId = UserId;
}

void UStandInC9::ExpireFixtureFeed(APawn* Pawn)
{
	FFixtureFeed* Feed = FindFeed(Pawn);
	if (!Feed || Feed->bExpired)
	{
		return;
	}
	Feed->bExpired = true;
	if (bShowVideo)
	{
		OnFeedChanged.Broadcast(Pawn);
	}
}

void UStandInC9::RestoreFixtureFeed(APawn* Pawn)
{
	FFixtureFeed* Feed = FindFeed(Pawn);
	if (!Feed || !Feed->bExpired)
	{
		return;
	}
	Feed->bExpired = false;
	if (bShowVideo)
	{
		OnFeedChanged.Broadcast(Pawn);
	}
}

void UStandInC9::ChangeFixtureFeedGeneration(APawn* Pawn)
{
	FFixtureFeed* Feed = FindFeed(Pawn);
	if (!Feed)
	{
		return;
	}
	Feed->Generation = NextGeneration++;
	if (IsVisible(*Feed))
	{
		OnFeedChanged.Broadcast(Pawn);
	}
}

void UStandInC9::AdvanceFixtureFrame()
{
	FrameIndex = (FrameIndex + 1) % FixtureFrameCount;
	PruneFeeds();
	if (!bShowVideo)
	{
		return;
	}
	TArray<TWeakObjectPtr<APawn>> Changed;
	for (const FFixtureFeed& Feed : Feeds)
	{
		if (!Feed.bExpired)
		{
			Changed.Add(Feed.Pawn);
		}
	}
	for (const TWeakObjectPtr<APawn>& Pawn : Changed)
	{
		if (APawn* Live = Pawn.Get())
		{
			OnFeedChanged.Broadcast(Live);
		}
	}
}

void UStandInC9::UpdateLoop()
{
	const bool bNeeded = CameraState == EConstructCameraState::On || (bShowVideo && Feeds.Num() > 0);
	if (bNeeded == LoopHandle.IsValid())
	{
		return;
	}
	if (!bNeeded)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LoopHandle);
		LoopHandle.Reset();
		return;
	}
	LoopHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		AdvanceFixtureFrame();
		return true;
	}), LoopInterval);
}

void UStandInC9::PruneFeeds()
{
	Feeds.RemoveAll([](const FFixtureFeed& Feed) { return !Feed.Pawn.IsValid(); });
	UpdateLoop();
}

UStandInC9::FFixtureFeed* UStandInC9::FindFeed(const APawn* Pawn)
{
	return Feeds.FindByPredicate([Pawn](const FFixtureFeed& Feed) { return Pawn && Feed.Pawn == Pawn; });
}

const UStandInC9::FFixtureFeed* UStandInC9::FindFeed(const APawn* Pawn) const
{
	return Feeds.FindByPredicate([Pawn](const FFixtureFeed& Feed) { return Pawn && Feed.Pawn == Pawn; });
}
