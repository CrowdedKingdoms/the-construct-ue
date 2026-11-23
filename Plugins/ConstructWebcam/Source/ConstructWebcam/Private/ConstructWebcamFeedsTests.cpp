#include "ConstructWebcamFeeds.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructDeveloperSettings.h"
#include "ConstructPresencePawn.h"
#include "ConstructWebcamCore.h"
#include "ConstructWebcamTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

struct FConstructWebcamFeedsTestAccess
{
	static auto& Hooks(UConstructWebcamFeeds& Feeds)
	{
		return Feeds.Hooks;
	}

	static void Enable(UConstructWebcamFeeds& Feeds)
	{
		Feeds.bEnabled = true;
	}

	static void Deliver(UConstructWebcamFeeds& Feeds, const FCrowdyVideoFrame& Frame)
	{
		Feeds.HandleFrame(Frame);
	}

	static void Tick(UConstructWebcamFeeds& Feeds)
	{
		Feeds.TickFeeds(ConstructWebcam::FrameInterval);
	}

	static void AnswerReceive(UConstructWebcamFeeds& Feeds, int32 Token, const FCrowdyCommunicationSettings& Settings)
	{
		Feeds.ReceiveSettingAnswered(Token, &Settings);
	}

	static void FailReceive(UConstructWebcamFeeds& Feeds, int32 Token)
	{
		Feeds.ReceiveSettingAnswered(Token, nullptr);
	}

	static void RefreshTick(UConstructWebcamFeeds& Feeds)
	{
		Feeds.TickRefresh(ConstructWebcam::SettingsRefreshSeconds);
	}

	static void Left(UConstructWebcamFeeds& Feeds, const FGuid& UUID)
	{
		FCrowdyActorLeft ActorLeft;
		ActorLeft.UUID = UUID;
		Feeds.HandleActorLeft(ActorLeft, 0);
	}

	static int32 FeedCount(const UConstructWebcamFeeds& Feeds)
	{
		return Feeds.Feeds.Num();
	}
};

namespace ConstructWebcamFeedsTests
{
constexpr EAutomationTestFlags FeedsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

const FCrowdyActorId FirstId = FCrowdyActorId::FromAnsiLiteral("0123456789abcdef0123456789abcdef");
const FCrowdyActorId SecondId = FCrowdyActorId::FromAnsiLiteral("fedcba9876543210fedcba9876543210");

struct FFeedsFixture
{
	UWorld* World = nullptr;
	TStrongObjectPtr<UConstructWebcamFeeds> Feeds;
	TStrongObjectPtr<UConstructWebcamTestListener> Listener;
	TMap<FGuid, TWeakObjectPtr<AActor>> Actors;
	TMap<TWeakObjectPtr<const AActor>, FCrowdyActorId> Ids;
	TArray<int32> Reads;
	TArray<FColor> LastDecoded;
	IImageWrapperModule* Wrapper = nullptr;
	double Time = 100.0;
	int32 Decodes = 0;

	~FFeedsFixture()
	{
		if (Feeds.IsValid())
		{
			Feeds->Deinitialize();
		}
		Feeds.Reset();
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	bool Initialize(FAutomationTestBase& Test, bool bWithFeeds = true)
	{
		Wrapper = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (!Test.TestNotNull(TEXT("Feeds fixture has an Engine"), GEngine) ||
		    !Test.TestNotNull(TEXT("Feeds fixture has the image wrapper"), Wrapper))
		{
			return false;
		}
		UWorld::InitializationValues Init;
		Init.InitializeScenes(true)
		    .AllowAudioPlayback(false)
		    .CreatePhysicsScene(false)
		    .CreateNavigation(false)
		    .CreateAISystem(false)
		    .ShouldSimulatePhysics(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Feeds fixture owns an isolated game world"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		if (!bWithFeeds)
		{
			return true;
		}

		Feeds.Reset(NewObject<UConstructWebcamFeeds>(World));
		Listener.Reset(NewObject<UConstructWebcamTestListener>(GetTransientPackage()));
		Feeds->OnFeedChanged.AddDynamic(Listener.Get(), &UConstructWebcamTestListener::OnFeed);
		FConstructWebcamFeedsTestAccess::Enable(*Feeds);
		auto& Hooks = FConstructWebcamFeedsTestAccess::Hooks(*Feeds);
		Hooks.FindActor = [this](const FGuid& UUID) { return Actors.FindRef(UUID).Get(); };
		Hooks.FindPawnId = [this](const AActor& Actor, FCrowdyActorId& OutId)
		{
			const FCrowdyActorId* Id = Ids.Find(&Actor);
			if (Id)
			{
				OutId = *Id;
			}
			return Id != nullptr;
		};
		Hooks.ReadSettings = [this](int32 Token) { Reads.Add(Token); };
		Hooks.Now = [this]() { return Time; };
		Hooks.RunWork = [](TUniqueFunction<void()> Job) { Job(); };
		Feeds->SetDecodedFrameObserverForCheck([this](APawn*, const TArray<FColor>& Pixels, FIntPoint)
		{
			++Decodes;
			LastDecoded = Pixels;
		});
		return true;
	}

	AConstructPresencePawn* AddPawn(FAutomationTestBase& Test, const FCrowdyActorId& Id, FGuid& OutUUID)
	{
		AConstructPresencePawn* Pawn = World->SpawnActor<AConstructPresencePawn>();
		Test.TestNotNull(TEXT("Fixture spawned a presence pawn"), Pawn);
		OutUUID = FGuid::NewGuid();
		Actors.Add(OutUUID, Pawn);
		Ids.Add(Pawn, Id);
		return Pawn;
	}

	void ShowAndAllow(ECrowdyCommunicationScope Receive = ECrowdyCommunicationScope::Everyone)
	{
		Feeds->SetShowVideo(true);
		AnswerLast(Receive);
	}

	void AnswerLast(ECrowdyCommunicationScope Receive)
	{
		FCrowdyCommunicationSettings Settings;
		Settings.VideoReceive = Receive;
		FConstructWebcamFeedsTestAccess::AnswerReceive(*Feeds, Reads.Num() > 0 ? Reads.Last() : 0, Settings);
	}

	TArray<uint8> Jpeg(int32 Width, int32 Height, const FColor& Color) const
	{
		TArray<FColor> Pixels;
		Pixels.Init(Color, Width * Height);
		TArray64<uint8> Encoded;
		Wrapper->CompressImage(Encoded, EImageFormat::JPEG, FImageView(Pixels.GetData(), Width, Height), 90);
		return TArray<uint8>(Encoded.GetData(), static_cast<int32>(Encoded.Num()));
	}

	void Send(const FGuid& UUID, const FString& SenderId, TArray<uint8> Bytes, ECrowdyVideoCodec Codec = ECrowdyVideoCodec::Jpeg)
	{
		FCrowdyVideoFrame Frame;
		Frame.SenderUUID = UUID;
		Frame.SenderId = SenderId;
		Frame.Codec = Codec;
		Frame.Bytes = MoveTemp(Bytes);
		FConstructWebcamFeedsTestAccess::Deliver(*Feeds, Frame);
	}

	void Send(const FGuid& UUID, const FCrowdyActorId& Id, const FColor& Color)
	{
		Send(UUID, Id.ToString(), Jpeg(128, 96, Color));
	}

	void Tick()
	{
		FConstructWebcamFeedsTestAccess::Tick(*Feeds);
	}

	bool IsShown(APawn* Pawn) const
	{
		return Feeds->GetFeedInfo(Pawn).Texture != nullptr;
	}
};

struct FWebcamSettingScope
{
	bool bPrevious = GetDefault<UConstructDeveloperSettings>()->bEnableWebcam;

	explicit FWebcamSettingScope(bool bEnabled)
	{
		GetMutableDefault<UConstructDeveloperSettings>()->bEnableWebcam = bEnabled;
	}

	~FWebcamSettingScope()
	{
		GetMutableDefault<UConstructDeveloperSettings>()->bEnableWebcam = bPrevious;
	}
};
}

using namespace ConstructWebcamFeedsTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamFeedsDisabledTest, "Construct.Webcam.Feeds.DisabledBindsNothing", FeedsTestFlags)
bool FConstructWebcamFeedsDisabledTest::RunTest(const FString& Parameters)
{
	const FWebcamSettingScope Setting(false);
	FFeedsFixture F;
	if (!F.Initialize(*this, false))
	{
		return false;
	}
	UConstructWebcamFeeds* WorldFeeds = F.World->GetSubsystem<UConstructWebcamFeeds>();
	UCrowdyVideoFrameReceiver* Receiver = F.World->GetSubsystem<UCrowdyVideoFrameReceiver>();
	if (!TestNotNull(TEXT("The world made its feeds"), WorldFeeds) || !TestNotNull(TEXT("And its receiver"), Receiver))
	{
		return false;
	}
	WorldFeeds->SetShowVideo(true);
	TestFalse(TEXT("Disabled webcams bind nothing to the receiver"), Receiver->OnVideoFrameAssembled.IsBound());

	UConstructWebcamFeeds* Enabled = NewObject<UConstructWebcamFeeds>(F.World);
	FConstructWebcamFeedsTestAccess::Enable(*Enabled);
	FConstructWebcamFeedsTestAccess::Hooks(*Enabled).ReadSettings = [](int32) {};
	Enabled->SetShowVideo(true);
	TestTrue(TEXT("Enabled webcams with Show Video bind the receiver"), Receiver->OnVideoFrameAssembled.IsBound());
	Enabled->SetShowVideo(false);
	TestFalse(TEXT("Show Video off unbinds it"), Receiver->OnVideoFrameAssembled.IsBound());
	Enabled->Deinitialize();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamFeedsExpiryTest, "Construct.Webcam.Feeds.FirstFrameAndExpiry", FeedsTestFlags)
bool FConstructWebcamFeedsExpiryTest::RunTest(const FString& Parameters)
{
	FFeedsFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	FGuid UUID;
	AConstructPresencePawn* Pawn = F.AddPawn(*this, FirstId, UUID);
	F.Send(UUID, FirstId, FColor::Blue);
	TestFalse(TEXT("Nothing shows before Show Video"), F.IsShown(Pawn));
	TestEqual(TEXT("Nothing is decoded before Show Video"), F.Decodes, 0);

	F.ShowAndAllow();
	F.Send(UUID, FirstId, FColor::Blue);
	const FConstructWebcamFeed First = F.Feeds->GetFeedInfo(Pawn);
	TestNotNull(TEXT("The first frame shows"), First.Texture.Get());
	TestEqual(TEXT("Under its exact sender id"), First.SenderId, FirstId.ToString());
	TestTrue(TEXT("With a generation"), First.Generation > 0);
	TestEqual(TEXT("The first frame fires the event"), F.Listener->FeedEventsFor(Pawn), 1);
	TestTrue(TEXT("Decoded with its colour"), F.LastDecoded.Num() > 0 && F.LastDecoded[0].B > 200);

	F.Time += 0.5;
	F.Tick();
	TestTrue(TEXT("Half a second later it still shows"), F.IsShown(Pawn));
	TestEqual(TEXT("With no new event"), F.Listener->FeedEventsFor(Pawn), 1);
	F.Time += 0.6;
	F.Tick();
	TestFalse(TEXT("After 1 s without frames it is empty"), F.IsShown(Pawn));
	TestEqual(TEXT("And the expiry fires without a frame"), F.Listener->FeedEventsFor(Pawn), 2);

	F.Send(UUID, FirstId, FColor::Blue);
	TestTrue(TEXT("A new frame shows again"), F.IsShown(Pawn));
	TestEqual(TEXT("On the same feed"), F.Feeds->GetFeedInfo(Pawn).Generation, First.Generation);
	TestEqual(TEXT("With an event"), F.Listener->FeedEventsFor(Pawn), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamFeedsRejectTest, "Construct.Webcam.Feeds.RejectsBadFrames", FeedsTestFlags)
bool FConstructWebcamFeedsRejectTest::RunTest(const FString& Parameters)
{
	FFeedsFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	FGuid UUID;
	AConstructPresencePawn* Pawn = F.AddPawn(*this, FirstId, UUID);
	F.ShowAndAllow();

	F.Send(UUID, FirstId.ToString(), F.Jpeg(160, 120, FColor::Red));
	TestEqual(TEXT("A header over 128x96 counts as undecodable"), F.Feeds->GetUndecodableFrameCount(), int64(1));
	TestEqual(TEXT("And decodes no pixels"), F.Decodes, 0);
	TestFalse(TEXT("And shows nothing"), F.IsShown(Pawn));

	F.Time += 0.2;
	F.Send(UUID, FirstId.ToString(), F.Jpeg(128, 96, FColor::Red), ECrowdyVideoCodec::WebP);
	TestEqual(TEXT("WebP counts as undecodable"), F.Feeds->GetUndecodableFrameCount(), int64(2));
	TestFalse(TEXT("And shows nothing"), F.IsShown(Pawn));

	F.Time += 0.2;
	F.Send(UUID, FirstId.ToString().ToUpper(), F.Jpeg(128, 96, FColor::Red));
	TestEqual(TEXT("A sender id differing only in case is dropped"), F.Feeds->GetDroppedFrameCount(), int64(1));
	F.Send(UUID, SecondId, FColor::Red);
	TestEqual(TEXT("Another sender id on this pawn is dropped"), F.Feeds->GetDroppedFrameCount(), int64(2));
	F.Send(FGuid::NewGuid(), FirstId, FColor::Red);
	TestEqual(TEXT("A sender with no pawn is dropped"), F.Feeds->GetDroppedFrameCount(), int64(3));

	AActor* Prop = F.World->SpawnActor<AActor>();
	const FGuid PropUUID = FGuid::NewGuid();
	F.Actors.Add(PropUUID, Prop);
	F.Ids.Add(Prop, SecondId);
	F.Send(PropUUID, SecondId, FColor::Red);
	TestEqual(TEXT("A sender that is not a presence pawn is dropped"), F.Feeds->GetDroppedFrameCount(), int64(4));

	Pawn->SetActorHiddenInGame(true);
	F.Send(UUID, FirstId, FColor::Red);
	TestEqual(TEXT("A pawn that is not drawn is dropped"), F.Feeds->GetDroppedFrameCount(), int64(5));
	TestEqual(TEXT("None of these decoded"), F.Decodes, 0);
	Pawn->SetActorHiddenInGame(false);

	F.Feeds->SetShowVideo(false);
	F.ShowAndAllow(ECrowdyCommunicationScope::Off);
	F.Send(UUID, FirstId, FColor::Red);
	TestFalse(TEXT("Video Receive off shows nothing"), F.IsShown(Pawn));
	TestEqual(TEXT("And decodes nothing"), F.Decodes, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamFeedsRateTest, "Construct.Webcam.Feeds.DecodeRateKeepsNewest", FeedsTestFlags)
bool FConstructWebcamFeedsRateTest::RunTest(const FString& Parameters)
{
	FFeedsFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	FGuid UUID;
	AConstructPresencePawn* Pawn = F.AddPawn(*this, FirstId, UUID);
	F.ShowAndAllow();
	for (int32 Index = 0; Index < 10; ++Index)
	{
		F.Send(UUID, FirstId, FColor::Red);
	}
	TestEqual(TEXT("Ten frames arriving together all decode"), F.Decodes, 10);
	TestTrue(TEXT("The first frame shows"), F.IsShown(Pawn));
	F.Send(UUID, FirstId, FColor::Green);
	F.Send(UUID, FirstId, FColor::Blue);
	TestEqual(TEXT("No eleventh decode within the same second"), F.Decodes, 10);

	F.Time += 0.5;
	F.Tick();
	TestEqual(TEXT("Still none half a second later"), F.Decodes, 10);
	F.Time += 0.55;
	F.Tick();
	TestEqual(TEXT("The waiting frame decodes once the second has passed"), F.Decodes, 11);
	TestTrue(TEXT("And it is the newest one"),
	         F.LastDecoded.Num() > 0 && F.LastDecoded[0].B > 200 && F.LastDecoded[0].G < 100);
	F.Time += 0.15;
	F.Tick();
	TestEqual(TEXT("The older frame was never kept"), F.Decodes, 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamFeedsLifetimeTest, "Construct.Webcam.Feeds.ClearRebindAndRelease", FeedsTestFlags)
bool FConstructWebcamFeedsLifetimeTest::RunTest(const FString& Parameters)
{
	FFeedsFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	FGuid FirstUUID;
	FGuid SecondUUID;
	AConstructPresencePawn* First = F.AddPawn(*this, FirstId, FirstUUID);
	AConstructPresencePawn* Second = F.AddPawn(*this, SecondId, SecondUUID);
	F.ShowAndAllow();
	F.Send(FirstUUID, FirstId, FColor::Red);
	F.Send(SecondUUID, SecondId, FColor::Blue);
	TestTrue(TEXT("Both feeds show"), F.IsShown(First) && F.IsShown(Second));

	F.Feeds->SetShowVideo(false);
	TestFalse(TEXT("Show Video off clears every feed"), F.IsShown(First) || F.IsShown(Second));
	TestEqual(TEXT("And fires for the first"), F.Listener->FeedEventsFor(First), 2);
	TestEqual(TEXT("And for the second"), F.Listener->FeedEventsFor(Second), 2);

	F.ShowAndAllow();
	F.Send(FirstUUID, FirstId, FColor::Red);
	F.Send(SecondUUID, SecondId, FColor::Blue);
	const int32 FirstGeneration = F.Feeds->GetFeedInfo(First).Generation;
	TestTrue(TEXT("Both show again"), F.IsShown(First) && F.IsShown(Second));

	FGuid ThirdUUID;
	AConstructPresencePawn* Third = F.AddPawn(*this, FirstId, ThirdUUID);
	F.Actors.Add(FirstUUID, Third);
	F.Time += 0.15;
	F.Send(FirstUUID, FirstId, FColor::Red);
	TestFalse(TEXT("A sender moving to another pawn leaves the old one"), F.IsShown(First));
	TestEqual(TEXT("With an event for the old pawn"), F.Listener->FeedEventsFor(First), 4);
	TestTrue(TEXT("And shows on the new one"), F.IsShown(Third));
	TestNotEqual(TEXT("Under a new generation"), F.Feeds->GetFeedInfo(Third).Generation, FirstGeneration);

	const int32 SecondEvents = F.Listener->FeedEventsFor(Second);
	FConstructWebcamFeedsTestAccess::Left(*F.Feeds, SecondUUID);
	TestFalse(TEXT("A pawn that leaves is released"), F.IsShown(Second));
	TestEqual(TEXT("With an event"), F.Listener->FeedEventsFor(Second), SecondEvents + 1);

	F.Ids.Add(Third, SecondId);
	F.Tick();
	TestFalse(TEXT("A pawn taking another entity id is released"), F.IsShown(Third));
	TestEqual(TEXT("Nothing is left"), FConstructWebcamFeedsTestAccess::FeedCount(*F.Feeds), 0);

	F.Ids.Add(Third, FirstId);
	F.Time += 0.15;
	F.Send(FirstUUID, FirstId, FColor::Red);
	TestTrue(TEXT("Its own id shows again"), F.IsShown(Third));
	Third->Destroy();
	F.Tick();
	TestEqual(TEXT("A destroyed pawn is released"), FConstructWebcamFeedsTestAccess::FeedCount(*F.Feeds), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamFeedsRefreshTest, "Construct.Webcam.Feeds.RefreshAppliesVideoReceive", FeedsTestFlags)
bool FConstructWebcamFeedsRefreshTest::RunTest(const FString& Parameters)
{
	FFeedsFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	FGuid UUID;
	AConstructPresencePawn* Pawn = F.AddPawn(*this, FirstId, UUID);
	F.ShowAndAllow();
	F.Send(UUID, FirstId, FColor::Blue);
	TestTrue(TEXT("The feed shows"), F.IsShown(Pawn));

	const int32 Reads = F.Reads.Num();
	F.Feeds->RefreshSettings();
	TestEqual(TEXT("A refresh reads the settings again"), F.Reads.Num(), Reads + 1);
	FConstructWebcamFeedsTestAccess::FailReceive(*F.Feeds, F.Reads.Last());
	TestTrue(TEXT("A failed read keeps what was known"), F.IsShown(Pawn));
	F.Feeds->RefreshSettings();
	F.AnswerLast(ECrowdyCommunicationScope::Off);
	TestFalse(TEXT("Video Receive now off clears the feed"), F.IsShown(Pawn));
	TestEqual(TEXT("And fires the event"), F.Listener->FeedEventsFor(Pawn), 2);
	TestEqual(TEXT("And releases it"), FConstructWebcamFeedsTestAccess::FeedCount(*F.Feeds), 0);
	F.Time += 0.15;
	F.Send(UUID, FirstId, FColor::Blue);
	TestFalse(TEXT("Frames show nothing while it is off"), F.IsShown(Pawn));

	FConstructWebcamFeedsTestAccess::RefreshTick(*F.Feeds);
	F.AnswerLast(ECrowdyCommunicationScope::Everyone);
	F.Time += 0.15;
	F.Send(UUID, FirstId, FColor::Blue);
	TestTrue(TEXT("The periodic read turning it back on shows frames again"), F.IsShown(Pawn));

	F.Feeds->SetShowVideo(false);
	const int32 Before = F.Reads.Num();
	F.Feeds->RefreshSettings();
	TestEqual(TEXT("With Show Video off nothing is read"), F.Reads.Num(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamFeedsSizeTest, "Construct.Webcam.Feeds.PictureSizesReuseTexture", FeedsTestFlags)
bool FConstructWebcamFeedsSizeTest::RunTest(const FString& Parameters)
{
	FFeedsFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}
	FGuid UUID;
	AConstructPresencePawn* Pawn = F.AddPawn(*this, FirstId, UUID);
	F.ShowAndAllow();
	F.Send(UUID, FirstId, FColor::Red);
	UTexture2D* First = F.Feeds->GetFeedInfo(Pawn).Texture;
	TestNotNull(TEXT("The first picture shows"), First);

	const FIntPoint Sizes[] = {FIntPoint(64, 48), FIntPoint(127, 96), FIntPoint(128, 95)};
	for (const FIntPoint& Size : Sizes)
	{
		F.Time += 0.15;
		F.Send(UUID, FirstId.ToString(), F.Jpeg(Size.X, Size.Y, FColor::Blue));
		TestTrue(TEXT("Each picture size reuses the feed's texture"), F.Feeds->GetFeedInfo(Pawn).Texture == First);
	}
	TestEqual(TEXT("Every picture decoded"), F.Decodes, 4);
	TestEqual(TEXT("Into the 128x96 frame"), F.LastDecoded.Num(), ConstructWebcam::FramePixels);
	TestEqual(TEXT("And no new texture fired the event"), F.Listener->FeedEventsFor(Pawn), 1);
	return true;
}

#endif
