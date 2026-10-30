#include "ConstructPresencePawn.h"
#include "ConstructPresenceSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Replication/Subsystems/CrowdyActorTracker.h"
#include "Replication/Subsystems/CrowdyEntitySubsystem.h"
#include "UObject/StrongObjectPtr.h"

struct FConstructPresenceSubsystemTestAccess
{
	static void SeedName(AConstructPresencePawn& Pawn, const FString& Name)
	{
		Pawn.SetDisplayedPlayerName(Name);
	}

	static const FString& Name(const AConstructPresencePawn& Pawn)
	{
		return Pawn.PlayerName;
	}

	static void Bind(UConstructPresenceSubsystem& Subsystem,
	                 UCrowdyEntitySubsystem& Entities,
	                 UCrowdyActorTracker& Tracker)
	{
		Subsystem.Entities = &Entities;
		Subsystem.Tracker = &Tracker;
		Subsystem.BindLifecycleEvents();
	}

	static void Unbind(UConstructPresenceSubsystem& Subsystem)
	{
		Subsystem.UnbindLifecycleEvents();
	}

	static bool IsPending(const UConstructPresenceSubsystem& Subsystem, const FGuid& UUID)
	{
		return Subsystem.PendingRemotePlayers.Contains(UUID);
	}

	static bool IsActive(const UConstructPresenceSubsystem& Subsystem, const FGuid& UUID)
	{
		return Subsystem.RemotePlayers.Contains(UUID);
	}
};

namespace ConstructPresenceSubsystemTests
{
class FLifecycleLogCounts final : public FOutputDevice
{
public:
	FLifecycleLogCounts()
	{
		if (GLog)
		{
			GLog->FlushThreadedLogs();
			GLog->AddOutputDevice(this);
			Appeared = 0;
			Departed = 0;
		}
	}

	~FLifecycleLogCounts() override
	{
		if (GLog)
		{
			GLog->RemoveOutputDevice(this);
		}
	}

	void Sync()
	{
		if (GLog)
		{
			GLog->FlushThreadedLogs();
		}
	}

	int32 AppearedCount()
	{
		Sync();
		return Appeared.Load();
	}

	int32 DepartedCount()
	{
		Sync();
		return Departed.Load();
	}

	void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Category != TEXT("LogConstructPresence") || Verbosity != ELogVerbosity::Log)
		{
			return;
		}
		if (FCString::Strcmp(Message, TEXT("ConstructPresence: remote player appeared")) == 0)
		{
			++Appeared;
		}
		else if (FCString::Strncmp(Message,
		                           TEXT("ConstructPresence: remote player left:"),
		                           FCString::Strlen(TEXT("ConstructPresence: remote player left:"))) == 0)
		{
			++Departed;
		}
	}

	TAtomic<int32> Appeared{0};
	TAtomic<int32> Departed{0};
};

struct FFixture
{
	UWorld* World = nullptr;
	TStrongObjectPtr<UCrowdyEntitySubsystem> Entities;
	TStrongObjectPtr<UCrowdyActorTracker> Tracker;
	TStrongObjectPtr<UConstructPresenceSubsystem> Presence;

	~FFixture()
	{
		if (Presence.IsValid())
		{
			FConstructPresenceSubsystemTestAccess::Unbind(*Presence);
		}
		Presence.Reset();
		Tracker.Reset();
		Entities.Reset();
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		if (!Test.TestNotNull(TEXT("Lifecycle fixture has an initialized Engine"), GEngine))
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
		if (!Test.TestNotNull(TEXT("Lifecycle fixture owns an isolated game world"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		Entities.Reset(World->GetSubsystem<UCrowdyEntitySubsystem>());
		Tracker.Reset(NewObject<UCrowdyActorTracker>(World));
		Presence.Reset(NewObject<UConstructPresenceSubsystem>(World));
		if (!Test.TestNotNull(TEXT("Fixture has an entity registry"), Entities.Get()) ||
		    !Test.TestNotNull(TEXT("Fixture has the public actor tracker"), Tracker.Get()) ||
		    !Test.TestNotNull(TEXT("Fixture has the production presence subsystem"), Presence.Get()))
		{
			return false;
		}
		FConstructPresenceSubsystemTestAccess::Bind(*Presence, *Entities, *Tracker);
		return true;
	}

	AConstructPresencePawn* SpawnPawn(FAutomationTestBase& Test)
	{
		AConstructPresencePawn* Pawn = World->SpawnActor<AConstructPresencePawn>();
		if (!Test.TestNotNull(TEXT("Fixture spawned a production presence pawn"), Pawn))
		{
			return nullptr;
		}
		UCrowdyEntityComponent* Entity = Pawn->FindComponentByClass<UCrowdyEntityComponent>();
		if (!Test.TestNotNull(TEXT("Presence pawn has its production SDK entity component"), Entity))
		{
			return nullptr;
		}
		return Pawn;
	}

	void RegisterPawn(AConstructPresencePawn* Pawn, const FGuid& UUID, ECrowdyRole Role)
	{
		FCrowdyEntityRecord Record;
		Record.NetID = UUID;
		Record.Role = Role;
		Record.Participant = Pawn;
		Entities->RegisterEntity(Record);
		// Match the SDK pool: registration is announced before identity is assigned.
		Pawn->FindComponentByClass<UCrowdyEntityComponent>()->AssignPooledIdentity(UUID, FGuid(), Role, 0);
	}

	AConstructPresencePawn* AddPawn(FAutomationTestBase& Test, const FGuid& UUID, ECrowdyRole Role)
	{
		AConstructPresencePawn* Pawn = SpawnPawn(Test);
		if (Pawn)
		{
			RegisterPawn(Pawn, UUID, Role);
		}
		return Pawn;
	}
};
} // namespace ConstructPresenceSubsystemTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceRemoteLifecycleTest,
                                 "Construct.Presence.Subsystem.RemoteLifecycleAndLateRegistration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceRemoteLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace ConstructPresenceSubsystemTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	FLifecycleLogCounts Logs;
	const FGuid UnknownID = FGuid::NewGuid();
	Fixture.Tracker->OnRemoteEntityAppeared.Broadcast(FGuid(), FInstancedStruct(), 0);
	TestFalse(TEXT("Invalid ID is ignored"),
	          FConstructPresenceSubsystemTestAccess::IsPending(*Fixture.Presence, FGuid()));
	Fixture.Tracker->OnRemoteEntityAppeared.Broadcast(UnknownID, FInstancedStruct(), 0);
	TestTrue(TEXT("Appearance without a registered actor is deferred"),
	         FConstructPresenceSubsystemTestAccess::IsPending(*Fixture.Presence, UnknownID));
	TestEqual(TEXT("Unresolved appearance emits no P4"), Logs.Appeared.Load(), 0);
	FCrowdyActorLeft UnknownDeparture;
	UnknownDeparture.UUID = UnknownID;
	Fixture.Tracker->OnRemoteEntityLeftAnnounced.Broadcast(UnknownDeparture, 0);
	TestFalse(TEXT("Announced departure clears an unresolved appearance"),
	          FConstructPresenceSubsystemTestAccess::IsPending(*Fixture.Presence, UnknownID));
	TestEqual(TEXT("Unresolved departure emits no P5"), Logs.Departed.Load(), 0);

	const FGuid RemoteID = FGuid::NewGuid();
	Fixture.Tracker->OnRemoteEntityAppeared.Broadcast(RemoteID, FInstancedStruct(), 0);
	TestTrue(TEXT("Known-valid appearance waits for registration"),
	         FConstructPresenceSubsystemTestAccess::IsPending(*Fixture.Presence, RemoteID));
	AConstructPresencePawn* Remote = Fixture.SpawnPawn(*this);
	if (!Remote)
	{
		return false;
	}
	FConstructPresenceSubsystemTestAccess::SeedName(*Remote, TEXT("Stale pooled name"));
	TestEqual(TEXT("Late copy has no component identity at registration"),
	          Remote->FindComponentByClass<UCrowdyEntityComponent>()->GetRole(),
	          ECrowdyRole::None);
	Fixture.RegisterPawn(Remote, RemoteID, ECrowdyRole::RemoteProxy);
	TestTrue(TEXT("Registration resolves the pending remote"),
	         FConstructPresenceSubsystemTestAccess::IsActive(*Fixture.Presence, RemoteID));
	TestFalse(TEXT("Resolved remote is no longer pending"),
	          FConstructPresenceSubsystemTestAccess::IsPending(*Fixture.Presence, RemoteID));
	TestEqual(TEXT("Late appearance resets pooled name presentation"),
	          Remote->FindComponentByClass<UTextRenderComponent>()->Text.ToString(),
	          FString());
	TestTrue(TEXT("Late appearance clears the previous replicated name"),
	         FConstructPresenceSubsystemTestAccess::Name(*Remote).IsEmpty());
	TestEqual(TEXT("Resolved remote emits P4 once"), Logs.AppearedCount(), 1);
	Fixture.Tracker->OnRemoteEntityAppeared.Broadcast(RemoteID, FInstancedStruct(), 1);
	TestEqual(TEXT("Repeated appearance does not duplicate P4"), Logs.AppearedCount(), 1);

	FCrowdyActorLeft Left;
	Left.UUID = RemoteID;
	Left.Reason = ECrowdyActorLeftReason::SessionReleased;
	Fixture.Tracker->OnRemoteEntityLeft.Broadcast(Left, 0);
	TestFalse(TEXT("Leave removes the active remote"),
	          FConstructPresenceSubsystemTestAccess::IsActive(*Fixture.Presence, RemoteID));
	TestEqual(TEXT("First leave emits P5 once"), Logs.DepartedCount(), 1);
	Fixture.Tracker->OnRemoteEntityLeft.Broadcast(Left, 0);
	TestEqual(TEXT("Repeated leave does not duplicate P5"), Logs.DepartedCount(), 1);

	Fixture.Tracker->OnRemoteEntityAppeared.Broadcast(RemoteID, FInstancedStruct(), 1);
	TestTrue(TEXT("Remote can appear again after departure"),
	         FConstructPresenceSubsystemTestAccess::IsActive(*Fixture.Presence, RemoteID));
	TestEqual(TEXT("Reappearance emits a new P4"), Logs.AppearedCount(), 2);
	Fixture.Tracker->OnRemoteEntityTimedOut.Broadcast(RemoteID, 0);
	TestFalse(TEXT("Timeout removes the active remote"),
	          FConstructPresenceSubsystemTestAccess::IsActive(*Fixture.Presence, RemoteID));
	TestEqual(TEXT("Timeout emits P5"), Logs.DepartedCount(), 2);

	const FGuid LocalID = FGuid::NewGuid();
	AConstructPresencePawn* Local = Fixture.AddPawn(*this, LocalID, ECrowdyRole::Owner);
	if (!Local)
	{
		return false;
	}
	UTextRenderComponent* LocalTag = Local->FindComponentByClass<UTextRenderComponent>();
	LocalTag->SetText(FText::FromString(TEXT("Local name")));
	Fixture.Tracker->OnRemoteEntityAppeared.Broadcast(LocalID, FInstancedStruct(), 1);
	TestFalse(TEXT("Local-owner echo is never tracked as remote"),
	          FConstructPresenceSubsystemTestAccess::IsActive(*Fixture.Presence, LocalID));
	TestEqual(TEXT("Local-owner echo does not reset local presentation"),
	          LocalTag->Text.ToString(),
	          FString(TEXT("Local name")));
	TestEqual(TEXT("Local-owner echo emits no extra P4"), Logs.AppearedCount(), 2);
	return true;
}
#endif
