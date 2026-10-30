#include "ConstructPresencePawn.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Templates/Atomic.h"
#include "UObject/StrongObjectPtr.h"

// Friendship reaches the same pawn operations used by its live delegates without changing SDK state.
struct FConstructPresenceTestAccess
{
	static void ApplyLocalMovement(AConstructPresencePawn& Pawn, bool bEnabled)
	{
		Pawn.ApplyLocalMovementState(bEnabled);
	}

	static void RefreshMovement(AConstructPresencePawn& Pawn)
	{
		Pawn.RefreshMovementState();
	}

	static void BlockGameplayInput(AConstructPresencePawn& Pawn)
	{
		Pawn.HandleGameplayInputChanged(false);
	}

	static void ApplyProfile(AConstructPresencePawn& Pawn, const FString& Gamertag)
	{
		FCrowdyPlayerProfile Profile;
		Profile.Gamertag = Gamertag;
		Pawn.ApplyProfileName(Profile);
	}

	static void FailLookup(AConstructPresencePawn& Pawn)
	{
		Pawn.ApplyNameLookupFailure();
	}

	static void ResetRemote(AConstructPresencePawn& Pawn)
	{
		Pawn.ResetRemoteName();
	}

	static void AssignOwnership(AConstructPresencePawn& Pawn, bool bLocallyOwned)
	{
		Pawn.HandleOwnershipAssigned(
		    FGuid(), bLocallyOwned ? ECrowdyRole::Owner : ECrowdyRole::RemoteProxy, bLocallyOwned);
	}

	static void ArmReplyWithoutSession(AConstructPresencePawn& Pawn)
	{
		Pawn.bNameLookupPending = true;
	}

	static bool HasPendingReply(const AConstructPresencePawn& Pawn)
	{
		return Pawn.bNameLookupPending;
	}

	static void DeliverProfile(AConstructPresencePawn& Pawn)
	{
		FCrowdyPlayerProfile Profile;
		Profile.Gamertag = TEXT("LateSyntheticProfile");
		Pawn.HandleNameRead(Profile);
	}

	static void DeliverFailure(AConstructPresencePawn& Pawn)
	{
		Pawn.HandleNameReadFailed(FCrowdyAccountError(), FString());
	}

	static const FString& Name(const AConstructPresencePawn& Pawn)
	{
		return Pawn.PlayerName;
	}

	static UTextRenderComponent* Tag(const AConstructPresencePawn& Pawn)
	{
		return Pawn.NameTag.Get();
	}

	static TArray<UStaticMeshComponent*> CapsuleParts(const AConstructPresencePawn& Pawn)
	{
		return {Pawn.CapsuleBody.Get(), Pawn.CapsuleTop.Get(), Pawn.CapsuleBottom.Get()};
	}

	static ECrowdyHostOverride HostOverride(const AConstructPresencePawn& Pawn)
	{
		return Pawn.Entity->HostOverride;
	}

	static void ArmReply(AConstructPresencePawn& Pawn,
	                     UCrowdyGameSession* Session,
	                     int64 UserID,
	                     const FGuid& NetID,
	                     const FGuid& OwnerID)
	{
		Pawn.NameLookupSession = Session;
		Pawn.NameLookupUser = UserID;
		Pawn.NameLookupEntity = NetID;
		Pawn.NameLookupOwner = OwnerID;
		Pawn.bNameLookupPending = true;
	}

	static void ConfigureLookMappings(UInputMappingContext* Context,
	                                  UInputAction* MouseAction,
	                                  UInputAction* GamepadAction)
	{
		AConstructPresencePawn::ConfigureLookMappings(Context, MouseAction, GamepadAction);
	}
	static float ControllerPitchDelta(float AxisY, float Scale)
	{
		return AConstructPresencePawn::ControllerPitchDelta(AxisY, Scale);
	}
};

namespace ConstructPresenceTests
{
struct FBackgroundWorld
{
	UWorld* World = nullptr;
	AConstructPresencePawn* Pawn = nullptr;

	~FBackgroundWorld()
	{
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		if (!Test.TestNotNull(TEXT("Background pawn tests require an initialized Engine"), GEngine))
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
		World = UWorld::CreateWorld(
		    EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Isolated preview world was created"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		if (!Test.TestNull(TEXT("Background fixture creates no GameInstance or SDK session"), World->GetGameInstance()))
		{
			return false;
		}
		Pawn = World->SpawnActor<AConstructPresencePawn>();
		if (!Test.TestNotNull(TEXT("Background fixture spawns the actual production pawn"), Pawn))
		{
			return false;
		}
		return Test.TestNotNull(TEXT("Production pawn owns its real text component"),
		                        FConstructPresenceTestAccess::Tag(*Pawn));
	}
};

// Only counts fixed diagnostic strings. No arbitrary message, profile, or identifier is retained.
class FPresenceLogCounts final : public FOutputDevice
{
public:
	FPresenceLogCounts()
	{
		if (GLog)
		{
			GLog->FlushThreadedLogs();
			GLog->AddOutputDevice(this);
			// AddOutputDevice replays the existing backlog; each test measures only subsequent calls.
			LocalEntries = 0;
			Named = 0;
			NoGamertag = 0;
			LookupFailed = 0;
		}
	}

	virtual ~FPresenceLogCounts() override
	{
		if (GLog)
		{
			GLog->RemoveOutputDevice(this);
		}
	}

	void Synchronize()
	{
		if (GLog)
		{
			GLog->FlushThreadedLogs();
		}
	}

	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		if (Category != PresenceCategory || Verbosity != ELogVerbosity::Log)
		{
			return;
		}
		if (FCString::Strcmp(Message, TEXT("ConstructPresence: local player entered")) == 0)
		{
			++LocalEntries;
		}
		else if (FCString::Strcmp(Message, TEXT("ConstructPresence: name set")) == 0)
		{
			++Named;
		}
		else if (FCString::Strcmp(Message, TEXT("ConstructPresence: name set to fallback: no gamertag")) == 0)
		{
			++NoGamertag;
		}
		else if (FCString::Strcmp(Message, TEXT("ConstructPresence: name set to fallback: lookup failed")) == 0)
		{
			++LookupFailed;
		}
	}

	TAtomic<int32> LocalEntries{0};
	TAtomic<int32> Named{0};
	TAtomic<int32> NoGamertag{0};
	TAtomic<int32> LookupFailed{0};

private:
	const FName PresenceCategory = TEXT("LogConstructPresence");
};
} // namespace ConstructPresenceTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceSuccessfulGamertagTest,
                                 "Construct.Presence.Name.SuccessfulGamertag",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceSuccessfulGamertagTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	ConstructPresenceTests::FPresenceLogCounts Logs;
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, TEXT("SyntheticPilot"));
	Logs.Synchronize();
	TestEqual(TEXT("Successful profile stores its gamertag"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          FString(TEXT("SyntheticPilot")));
	TestEqual(TEXT("Successful profile reaches the real text component"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("SyntheticPilot")));
	TestEqual(TEXT("Successful profile emits P2 once"), Logs.Named.Load(), 1);
	TestEqual(TEXT("Successful profile emits no empty-name fallback"), Logs.NoGamertag.Load(), 0);
	TestEqual(TEXT("Successful profile emits no lookup-failed fallback"), Logs.LookupFailed.Load(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceEmptyGamertagTest,
                                 "Construct.Presence.Name.EmptyGamertagFallback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceEmptyGamertagTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	ConstructPresenceTests::FPresenceLogCounts Logs;
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, FString());
	Logs.Synchronize();
	TestEqual(TEXT("Empty gamertag stores Player fallback"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          FString(TEXT("Player")));
	TestEqual(TEXT("Empty gamertag displays Player fallback"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Player")));
	TestEqual(TEXT("Empty gamertag emits its P3 reason once"), Logs.NoGamertag.Load(), 1);
	TestEqual(TEXT("Empty gamertag emits no successful-name P2"), Logs.Named.Load(), 0);
	TestEqual(TEXT("Empty gamertag is not a failed lookup"), Logs.LookupFailed.Load(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceLookupFailureTest,
                                 "Construct.Presence.Name.LookupFailureFallback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceLookupFailureTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, TEXT("PreviousSyntheticName"));
	ConstructPresenceTests::FPresenceLogCounts Logs;
	FConstructPresenceTestAccess::FailLookup(*Fixture.Pawn);
	Logs.Synchronize();
	TestEqual(TEXT("Failed lookup replaces stale field with Player"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          FString(TEXT("Player")));
	TestEqual(TEXT("Failed lookup replaces stale displayed name with Player"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Player")));
	TestEqual(TEXT("Failed lookup emits its P3 reason once"), Logs.LookupFailed.Load(), 1);
	TestEqual(TEXT("Failed lookup emits no successful-name P2"), Logs.Named.Load(), 0);
	TestEqual(TEXT("Failed lookup is not an empty-gamertag result"), Logs.NoGamertag.Load(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresencePreviousNameClearedTest,
                                 "Construct.Presence.Pooling.PreviousNameCleared",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresencePreviousNameClearedTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, TEXT("PreviousSyntheticName"));
	TestFalse(TEXT("Fixture starts with an existing player's name"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn).IsEmpty());
	TestFalse(TEXT("Fixture starts with an existing displayed name"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.IsEmpty());
	// This is the actual pawn operation called by the presence subsystem when a pooled copy appears.
	FConstructPresenceTestAccess::ResetRemote(*Fixture.Pawn);
	TestTrue(TEXT("Remote appearance clears the previous name field"),
	         FConstructPresenceTestAccess::Name(*Fixture.Pawn).IsEmpty());
	TestTrue(TEXT("Remote appearance immediately clears the previous displayed name"),
	         FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceSameNameReuseTest,
                                 "Construct.Presence.Pooling.SameNameReuse",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceSameNameReuseTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, TEXT("Player"));
	TestEqual(TEXT("First occupant displays the common fallback name"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Player")));
	FConstructPresenceTestAccess::ResetRemote(*Fixture.Pawn);
	// Prove the SDK's change-only notify can distinguish the next equal name from the reset field.
	// Calling a notify twice without this intermediate assertion would miss a broken field reset.
	if (!TestTrue(TEXT("Same-name reuse first empties the stored field so a change can notify"),
	              FConstructPresenceTestAccess::Name(*Fixture.Pawn).IsEmpty()))
	{
		return false;
	}
	TestTrue(TEXT("Same-name reuse clears the old displayed text while waiting"),
	         FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.IsEmpty());
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, TEXT("Player"));
	TestEqual(TEXT("Next equal name is stored on the reused pawn"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          FString(TEXT("Player")));
	TestEqual(TEXT("Next equal name is displayed on the reused pawn"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Player")));
	TestTrue(TEXT("Next equal name remains visible on the reused remote pawn"),
	         FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->IsVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceOwnerVisibilityTest,
                                 "Construct.Presence.Name.OwnerVisibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceOwnerVisibilityTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	ConstructPresenceTests::FPresenceLogCounts Logs;
	FConstructPresenceTestAccess::AssignOwnership(*Fixture.Pawn, true);
	Logs.Synchronize();
	TestFalse(TEXT("Local ownership event hides the owner's name tag"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->IsVisible());
	TestEqual(TEXT("Owner without a session safely uses Player fallback"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          FString(TEXT("Player")));
	TestEqual(TEXT("Owner without a session applies fallback to its actual tag"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Player")));
	TestEqual(TEXT("Owner entry emits P1 once"), Logs.LocalEntries.Load(), 1);
	TestEqual(TEXT("Unavailable owner lookup emits lookup-failed P3 once"), Logs.LookupFailed.Load(), 1);
	FConstructPresenceTestAccess::AssignOwnership(*Fixture.Pawn, true);
	Logs.Synchronize();
	TestEqual(TEXT("Repeated local ownership does not repeat entry"), Logs.LocalEntries.Load(), 1);
	TestEqual(TEXT("Repeated local ownership does not repeat lookup fallback"), Logs.LookupFailed.Load(), 1);
	FConstructPresenceTestAccess::ResetRemote(*Fixture.Pawn);
	TestTrue(TEXT("Remote reuse restores tag visibility after local ownership"),
	         FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->IsVisible());
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, TEXT("SyntheticRemote"));
	TestTrue(TEXT("Remote name application leaves the tag visible"),
	         FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->IsVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceRejectedCallbacksTest,
                                 "Construct.Presence.Name.RejectedCallbacks",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceRejectedCallbacksTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const FString OriginalName(TEXT("CurrentSyntheticName"));
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, OriginalName);
	ConstructPresenceTests::FPresenceLogCounts Logs;
	FConstructPresenceTestAccess::ArmReplyWithoutSession(*Fixture.Pawn);
	Fixture.Pawn->UnPossessed();
	TestFalse(TEXT("Unpossessing cancels the pending name lookup"),
	          FConstructPresenceTestAccess::HasPendingReply(*Fixture.Pawn));
	FConstructPresenceTestAccess::DeliverProfile(*Fixture.Pawn);
	TestEqual(TEXT("Late name result cannot replace canceled pawn name"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          OriginalName);
	FConstructPresenceTestAccess::DeliverFailure(*Fixture.Pawn);
	TestEqual(TEXT("Late lookup error cannot replace canceled pawn name"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          OriginalName);
	Fixture.Pawn->FindComponentByClass<UCrowdyEntityComponent>()->AssignPooledIdentity(
	    FGuid::NewGuid(), FGuid::NewGuid(), ECrowdyRole::Owner, 0);
	FConstructPresenceTestAccess::ArmReplyWithoutSession(*Fixture.Pawn);
	FConstructPresenceTestAccess::DeliverProfile(*Fixture.Pawn);
	TestEqual(TEXT("Pending result without a valid local session cannot replace pawn name"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          OriginalName);
	TestFalse(TEXT("Rejected profile reply consumes its pending request"),
	          FConstructPresenceTestAccess::HasPendingReply(*Fixture.Pawn));
	FConstructPresenceTestAccess::ArmReplyWithoutSession(*Fixture.Pawn);
	FConstructPresenceTestAccess::DeliverFailure(*Fixture.Pawn);
	TestEqual(TEXT("Pending error without a valid local session cannot replace pawn name"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          OriginalName);
	TestFalse(TEXT("Rejected error reply consumes its pending request"),
	          FConstructPresenceTestAccess::HasPendingReply(*Fixture.Pawn));
	TestEqual(TEXT("Rejected callbacks preserve the actual displayed name"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          OriginalName);
	Logs.Synchronize();
	TestEqual(TEXT("Rejected callbacks emit no successful-name P2"), Logs.Named.Load(), 0);
	TestEqual(TEXT("Rejected callbacks emit no lookup-failed P3"), Logs.LookupFailed.Load(), 0);
	TestEqual(TEXT("Rejected callbacks emit no empty-gamertag P3"), Logs.NoGamertag.Load(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceNameTagSanitizationTest,
                                 "Construct.Presence.Name.DisplayTagSanitization",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceNameTagSanitizationTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const FString HtmlBreakName(TEXT("Visible first line<br>second line\nthird line"));
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, HtmlBreakName);
	TestEqual(TEXT("The tag stops at a literal HTML break"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Visible first line")));
	TestEqual(TEXT("The HTML-break profile value remains unchanged"),
	          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
	          HtmlBreakName);

	const FString CarriageReturnName(TEXT("Visible before CR\rhidden after CR"));
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, CarriageReturnName);
	TestEqual(TEXT("The tag stops at a carriage return"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Visible before CR")));

	const FString NewlineName(TEXT("Visible before LF\nhidden after LF"));
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, NewlineName);
	TestEqual(TEXT("The tag stops at a line feed"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString(TEXT("Visible before LF")));

	const FString LongName = FString::ChrN(200, TEXT('N'));
	FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, LongName);
	TestEqual(
	    TEXT("Long replicated profile remains unchanged"), FConstructPresenceTestAccess::Name(*Fixture.Pawn), LongName);
	TestEqual(TEXT("The tag is capped at 64 characters"),
	          FConstructPresenceTestAccess::Tag(*Fixture.Pawn)->Text.ToString(),
	          FString::ChrN(64, TEXT('N')));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceIdentityGuardTest,
                                 "Construct.Presence.Name.RejectsEachMismatchedIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceIdentityGuardTest::RunTest(const FString& Parameters)
{
	ConstructPresenceTests::FBackgroundWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	struct FSessionFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UGameInstance> Instance;
		TStrongObjectPtr<UCrowdyGameSession> Session;
		~FSessionFixture()
		{
			if (World)
			{
				World->SetGameInstance(nullptr);
			}
		}
	} SessionFixture;
	SessionFixture.World = Fixture.World;
	SessionFixture.Instance.Reset(NewObject<UGameInstance>(GEngine));
	// Exercise the real session type without initializing unrelated SDK services.
	SessionFixture.Session.Reset(NewObject<UCrowdyGameSession>(SessionFixture.Instance.Get()));
	Fixture.World->SetGameInstance(SessionFixture.Instance.Get());
	UCrowdyGameSession* Session = SessionFixture.Session.Get();
	UCrowdyEntityComponent* Entity = Fixture.Pawn->FindComponentByClass<UCrowdyEntityComponent>();
	if (!TestNotNull(TEXT("Callback fixture owns a real SDK session object"), Session) ||
	    !TestNotNull(TEXT("Callback fixture has the production entity component"), Entity))
	{
		return false;
	}
	const FGuid ExpectedNet = FGuid::NewGuid();
	const FGuid ExpectedOwner = FGuid::NewGuid();
	struct FIdentityCase
	{
		const TCHAR* Label;
		int64 User;
		FGuid Net;
		FGuid Owner;
		bool bAccept;
	};
	const FIdentityCase Cases[] = {
	    {TEXT("matching identity"), 42, ExpectedNet, ExpectedOwner, true},
	    {TEXT("different user"), 43, ExpectedNet, ExpectedOwner, false},
	    {TEXT("different entity"), 42, FGuid::NewGuid(), ExpectedOwner, false},
	    {TEXT("different owner"), 42, ExpectedNet, FGuid::NewGuid(), false},
	};
	for (const FIdentityCase& Case : Cases)
	{
		for (const bool bFailure : {false, true})
		{
			// These are isolated in-memory identities, with no token or sign-in.
			Session->SetUserID(Case.User);
			Entity->AssignPooledIdentity(Case.Net, Case.Owner, ECrowdyRole::Owner, 0);
			TestTrue(TEXT("Every case passes the local-owner prerequisite"), Entity->IsLocallyOwned());
			FConstructPresenceTestAccess::ApplyProfile(*Fixture.Pawn, TEXT("CurrentSyntheticName"));
			FConstructPresenceTestAccess::ArmReply(*Fixture.Pawn, Session, 42, ExpectedNet, ExpectedOwner);
			if (bFailure)
			{
				FConstructPresenceTestAccess::DeliverFailure(*Fixture.Pawn);
			}
			else
			{
				FConstructPresenceTestAccess::DeliverProfile(*Fixture.Pawn);
			}
			const FString Expected = Case.bAccept ? (bFailure ? TEXT("Player") : TEXT("LateSyntheticProfile"))
			                                      : TEXT("CurrentSyntheticName");
			TestEqual(*FString::Printf(TEXT("%s: production %s callback applies only an accepted reply"),
			                           Case.Label,
			                           bFailure ? TEXT("failure") : TEXT("success")),
			          FConstructPresenceTestAccess::Name(*Fixture.Pawn),
			          Expected);
			TestFalse(TEXT("Every callback consumes its pending request"),
			          FConstructPresenceTestAccess::HasPendingReply(*Fixture.Pawn));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceLookSignTest,
                                 "Construct.Presence.Input.MouseAndGamepadPitchDirection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceLookSignTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UInputMappingContext> Context(NewObject<UInputMappingContext>(GetTransientPackage()));
	UInputAction* MouseAction = NewObject<UInputAction>(Context.Get());
	MouseAction->ValueType = EInputActionValueType::Axis2D;
	UInputAction* GamepadAction = NewObject<UInputAction>(Context.Get());
	GamepadAction->ValueType = EInputActionValueType::Axis2D;
	FConstructPresenceTestAccess::ConfigureLookMappings(Context.Get(), MouseAction, GamepadAction);

	const FEnhancedActionKeyMapping* MouseMapping = Context->GetMappings().FindByPredicate(
	    [MouseAction](const FEnhancedActionKeyMapping& Mapping)
	    {
		    return Mapping.Action == MouseAction && Mapping.Key == EKeys::Mouse2D;
	    });
	const FEnhancedActionKeyMapping* GamepadMapping = Context->GetMappings().FindByPredicate(
	    [GamepadAction](const FEnhancedActionKeyMapping& Mapping)
	    {
		    return Mapping.Action == GamepadAction && Mapping.Key == EKeys::Gamepad_RightY;
	    });
	if (!TestNotNull(TEXT("Mouse look mapping exists"), MouseMapping) ||
	    !TestNotNull(TEXT("Gamepad vertical look mapping exists"), GamepadMapping))
	{
		return false;
	}

	auto ApplyModifiers = [](const FEnhancedActionKeyMapping& Mapping, FInputActionValue Value)
	{
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
		{
			if (Modifier)
			{
				Value = Modifier->ModifyRaw(nullptr, Value, 1.0f / 60.0f);
			}
		}
		return Value.Get<FVector2D>();
	};
	const float MouseUpY = ApplyModifiers(*MouseMapping, FInputActionValue(FVector(0.0f, 1.0f, 0.0f))).Y;
	const float MouseDownY = ApplyModifiers(*MouseMapping, FInputActionValue(FVector(0.0f, -1.0f, 0.0f))).Y;
	const float GamepadUpY = ApplyModifiers(*GamepadMapping, FInputActionValue(FVector(1.0f, 0.0f, 0.0f))).Y;
	const float GamepadDownY = ApplyModifiers(*GamepadMapping, FInputActionValue(FVector(-1.0f, 0.0f, 0.0f))).Y;
	TestTrue(TEXT("Mouse up looks up after its Enhanced Input mapping"),
	         FConstructPresenceTestAccess::ControllerPitchDelta(MouseUpY, 1.0f) > 0.0f);
	TestTrue(TEXT("Gamepad up looks up after its Enhanced Input mapping"),
	         FConstructPresenceTestAccess::ControllerPitchDelta(GamepadUpY, 1.0f) > 0.0f);
	TestTrue(TEXT("Mouse down looks down after its Enhanced Input mapping"),
	         FConstructPresenceTestAccess::ControllerPitchDelta(MouseDownY, 1.0f) < 0.0f);
	TestTrue(TEXT("Gamepad down looks down after its Enhanced Input mapping"),
	         FConstructPresenceTestAccess::ControllerPitchDelta(GamepadDownY, 1.0f) < 0.0f);
	TestEqual(TEXT("Mapped mouse and gamepad up have the same pitch direction"),
	          FMath::Sign(FConstructPresenceTestAccess::ControllerPitchDelta(MouseUpY, 1.0f)),
	          FMath::Sign(FConstructPresenceTestAccess::ControllerPitchDelta(GamepadUpY, 1.0f)));
	TestTrue(TEXT("Gamepad pitch respects the frame-rate scale"),
	         FMath::IsNearlyEqual(FConstructPresenceTestAccess::ControllerPitchDelta(GamepadUpY, 0.25f), 0.25f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceCapsuleDefaultsTest,
                                 "Construct.Presence.Pawn.CapsuleDefaults",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceCapsuleDefaultsTest::RunTest(const FString& Parameters)
{
	const AConstructPresencePawn* Defaults = GetDefault<AConstructPresencePawn>();
	if (!TestNotNull(TEXT("Production presence class has native defaults"), Defaults))
	{
		return false;
	}
	TestTrue(TEXT("Presence entity host override is owner-only"),
	         FConstructPresenceTestAccess::HostOverride(*Defaults) == ECrowdyHostOverride::OwnerOnly);
	const TArray<UStaticMeshComponent*> Parts = FConstructPresenceTestAccess::CapsuleParts(*Defaults);
	const TArray<FString> ExpectedMeshes = {TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
	                                        TEXT("/Engine/BasicShapes/Sphere.Sphere"),
	                                        TEXT("/Engine/BasicShapes/Sphere.Sphere")};
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		const FString Label = FString::Printf(TEXT("Capsule part %d"), Index);
		UStaticMeshComponent* Part = Parts[Index];
		if (!TestNotNull(*(Label + TEXT(" is a native default component")), Part))
		{
			return false;
		}
		TestTrue(*(Label + TEXT(" is held on the native class default object")), Part->IsDefaultSubobject());
		if (!TestNotNull(*(Label + TEXT(" holds a hard mesh reference for cooking")), Part->GetStaticMesh().Get()))
		{
			return false;
		}
		TestEqual(*(Label + TEXT(" references the required Engine primitive")),
		          Part->GetStaticMesh()->GetPathName(),
		          ExpectedMeshes[Index]);
		if (!TestNotNull(*(Label + TEXT(" retains its unlit material for the dark startup map")), Part->GetMaterial(0)))
		{
			return false;
		}
		TestEqual(*(Label + TEXT(" references the presence-owned material")),
		          Part->GetMaterial(0)->GetPathName(),
		          FString(TEXT("/ConstructPresence/Materials/M_ConstructPresenceCapsule.M_ConstructPresenceCapsule")));
		TestTrue(*(Label + TEXT(" has no movement collision")),
		         Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestFalse(*(Label + TEXT(" generates no overlaps")), Part->GetGenerateOverlapEvents());
		TestFalse(*(Label + TEXT(" does not affect navigation")), Part->CanEverAffectNavigation());
		TestTrue(*(Label + TEXT(" attaches directly to the movement root")),
		         Part->GetAttachParent() == Defaults->GetRootComponent());
	}
	UCapsuleComponent* Collision = Defaults->GetCapsuleComponent();
	if (!TestNotNull(TEXT("Presence character retains its native capsule collider"), Collision))
	{
		return false;
	}
	TestTrue(TEXT("Character capsule is the movement root"), Defaults->GetRootComponent() == Collision);
	TestEqual(
	    TEXT("Character collision radius matches its visible body"), Collision->GetUnscaledCapsuleRadius(), 35.0f);
	TestEqual(TEXT("Character collision half-height matches its visible body"),
	          Collision->GetUnscaledCapsuleHalfHeight(),
	          80.0f);
	TestTrue(TEXT("Presence characters ignore other Pawn collision"),
	         Collision->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore);
	TestTrue(TEXT("Character capsule blocks world geometry"),
	         Collision->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
	TestFalse(TEXT("Unused inherited skeletal mesh is hidden"), Defaults->GetMesh()->IsVisible());
	TestTrue(TEXT("Unused inherited mesh has no additional movement collision"),
	         Defaults->GetMesh()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	UCharacterMovementComponent* Movement = Defaults->GetCharacterMovement();
	if (!TestNotNull(TEXT("Presence uses stock CharacterMovement"), Movement))
	{
		return false;
	}
	TestTrue(TEXT("CharacterMovement updates the native capsule"), Movement->UpdatedComponent == Collision);
	TestFalse(TEXT("CharacterMovement cannot auto-activate on pooled copies"), Movement->bAutoActivate);
	TestFalse(TEXT("CharacterMovement starts inactive"), Movement->IsActive());
	TestFalse(TEXT("CharacterMovement starts without ticking"), Movement->IsComponentTickEnabled());
	TestTrue(TEXT("CharacterMovement starts without a simulation mode"), Movement->MovementMode == MOVE_None);
	TestFalse(TEXT("Uncontrolled copies cannot run Character physics"), Movement->bRunPhysicsWithNoController);
	TestEqual(TEXT("Character uses normal gravity"), Movement->GravityScale, 1.0f);
	TestEqual(TEXT("Character walks at the requested speed"), Movement->MaxWalkSpeed, 600.0f);
	TestEqual(TEXT("Character uses the requested jump velocity"), Movement->JumpZVelocity, 420.0f);
	TestEqual(TEXT("Presence permits one grounded jump"), Defaults->JumpMaxCount, 1);
	TestFalse(TEXT("Crowdy remains the character's replication owner"), Defaults->GetIsReplicated());
	TestFalse(TEXT("Character does not enable Unreal movement replication"), Defaults->IsReplicatingMovement());
	TestTrue(TEXT("Character requests Enhanced Input components"),
	         Defaults->GetOverrideInputComponentClass().Get() == UEnhancedInputComponent::StaticClass());
	const UCameraComponent* Camera = Defaults->FindComponentByClass<UCameraComponent>();
	if (!TestNotNull(TEXT("Presence character has its native first-person camera"), Camera))
	{
		return false;
	}
	TestTrue(TEXT("Camera lives on the native defaults"), Camera->IsDefaultSubobject());
	TestTrue(TEXT("Camera attaches directly to the capsule"), Camera->GetAttachParent() == Collision);
	TestTrue(TEXT("Camera uses the requested eye height"),
	         Camera->GetRelativeLocation().Equals(FVector(0.0f, 0.0f, 64.0f), 0.01));
	TestTrue(TEXT("First-person camera follows local control rotation"), Camera->bUsePawnControlRotation);
	return true;
}

namespace ConstructPresenceMovementTests
{
constexpr float StepSeconds = 1.0f / 60.0f;
constexpr float FloorTop = 0.0f;

// Real runtime collision and CharacterMovement, with no GameInstance, BeginPlay,
// SDK registration, input injection, synthetic role, or SDK-owned state mutation.
// The fixture drives the production activation operation after local PC possession.
// Its fixed steps call the actual CMC tick; normal Engine actor scheduling and the
// positive SDK/local-player gate require separate runtime coverage.
struct FPhysicsWorld
{
	UWorld* World = nullptr;
	AConstructPresencePawn* Pawn = nullptr;
	APlayerController* Controller = nullptr;
	UBoxComponent* Floor = nullptr;
	UCharacterMovementComponent* Movement = nullptr;
	TStrongObjectPtr<ULocalPlayer> LocalPlayer;
	bool bPlayerAdded = false;

	~FPhysicsWorld()
	{
		if (bPlayerAdded)
		{
			LocalPlayer->PlayerRemoved();
		}
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	bool Initialize(FAutomationTestBase& Test, float SpawnZ = 350.0f, bool bEnableLocalMovement = true)
	{
		if (!Test.TestNotNull(TEXT("Movement fixture requires the initialized Engine"), GEngine))
		{
			return false;
		}
		UWorld::InitializationValues Init;
		Init.InitializeScenes(true)
		    .AllowAudioPlayback(false)
		    .CreatePhysicsScene(true)
		    .CreateNavigation(false)
		    .CreateAISystem(false)
		    .ShouldSimulatePhysics(false)
		    .EnableTraceCollision(true)
		    .ForceUseMovementComponentInNonGameWorld(true);
		World = UWorld::CreateWorld(
		    EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Isolated movement world exists"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		if (!Test.TestNull(TEXT("Movement fixture creates no GameInstance"), World->GetGameInstance()) ||
		    !Test.TestNotNull(TEXT("Movement fixture creates a real physics scene"), World->GetPhysicsScene()))
		{
			return false;
		}
		AWorldSettings* Settings = World->GetWorldSettings();
		Settings->bGlobalGravitySet = true;
		Settings->GlobalGravityZ = -980.0f;

		AActor* FloorActor = World->SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("Movement fixture has a floor actor"), FloorActor))
		{
			return false;
		}
		Floor = NewObject<UBoxComponent>(FloorActor, TEXT("MovementFixtureFloor"));
		FloorActor->AddInstanceComponent(Floor);
		FloorActor->SetRootComponent(Floor);
		Floor->SetBoxExtent(FVector(2000.0f, 2000.0f, 50.0f));
		Floor->SetCollisionProfileName(TEXT("BlockAll"));
		Floor->SetGenerateOverlapEvents(false);
		Floor->SetMobility(EComponentMobility::Static);
		FloorActor->SetActorLocation(FVector(0.0f, 0.0f, FloorTop - 50.0f));
		Floor->RegisterComponent();

		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Pawn = World->SpawnActor<AConstructPresencePawn>(FVector(0.0f, 0.0f, SpawnZ), FRotator::ZeroRotator, Spawn);
		Controller = World->SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Movement fixture spawns the production character"), Pawn) ||
		    !Test.TestNotNull(TEXT("Movement fixture spawns a plain runtime controller"), Controller))
		{
			return false;
		}
		// This Engine checkout requires a real LocalPlayer when no NetDriver exists.
		// Use the same isolated LocalPlayer lifecycle as the existing shell fixture.
		LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));
		LocalPlayer->PlayerAdded(nullptr, int32(0));
		bPlayerAdded = true;
		Controller->SetPlayer(LocalPlayer.Get());
		Controller->Possess(Pawn);
		Movement = Pawn->GetCharacterMovement();
		if (!Test.TestNotNull(TEXT("Production character owns real CharacterMovement"), Movement))
		{
			return false;
		}
		if (!Test.TestTrue(TEXT("Local standalone controller controls the fixture character"),
		                   Pawn->IsLocallyControlled()) ||
		    !Test.TestTrue(TEXT("CharacterMovement updates its actual capsule"),
		                   Movement->UpdatedComponent == Pawn->GetCapsuleComponent()))
		{
			return false;
		}

		// Validate the fixture independently before judging the character's falling or landing.
		FHitResult Hit;
		const bool bHitFloor = World->LineTraceSingleByChannel(
		    Hit, FVector(100.0f, 0.0f, 200.0f), FVector(100.0f, 0.0f, -100.0f), ECC_Visibility);
		if (!Test.TestTrue(TEXT("A real collision query finds the fixture floor at zero height"),
		                   bHitFloor && Hit.GetComponent() == Floor &&
		                       FMath::IsNearlyEqual(Hit.ImpactPoint.Z, double(FloorTop), 0.1)))
		{
			return false;
		}
		FConstructPresenceTestAccess::ApplyLocalMovement(*Pawn, bEnableLocalMovement);
		return true;
	}

	void Step(int32 Frames = 1)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			// Advance world time without scheduling actors, then run one real CMC step.
			// Do not call PerformMovement directly: TickComponent owns jump processing.
			World->Tick(LEVELTICK_TimeOnly, StepSeconds);
			if (Movement->IsActive() && Movement->IsComponentTickEnabled())
			{
				Movement->TickComponent(StepSeconds, LEVELTICK_All, &Movement->PrimaryComponentTick);
			}
		}
	}

	bool Settle(FAutomationTestBase& Test)
	{
		for (int32 Frame = 0; Frame < 180 && !Movement->IsMovingOnGround(); ++Frame)
		{
			Step();
		}
		// Starting in MOVE_Walking is not proof of contact: give floor detection a full step.
		Step(3);
		for (int32 Frame = 0; Frame < 180 && !Movement->IsMovingOnGround(); ++Frame)
		{
			Step();
		}
		return Test.TestTrue(TEXT("Fixture character actually settles on its collision floor"),
		                     Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor() &&
		                         Movement->CurrentFloor.HitResult.GetComponent() == Floor);
	}

	bool IsAtFloor() const
	{
		const double ExpectedZ = FloorTop + Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		// Stock CharacterMovement keeps a small floor distance above the contact surface.
		return FMath::Abs(Pawn->GetActorLocation().Z - ExpectedZ) < 5.0;
	}
};
} // namespace ConstructPresenceMovementTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceGravityLandingTest,
                                 "Construct.Presence.Movement.GravityAndLanding",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceGravityLandingTest::RunTest(const FString& Parameters)
{
	ConstructPresenceMovementTests::FPhysicsWorld Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const double InitialZ = Fixture.Pawn->GetActorLocation().Z;
	Fixture.Step(15);
	TestTrue(TEXT("Enabled character actually falls from an unsupported position"),
	         Fixture.Pawn->GetActorLocation().Z < InitialZ - 10.0);
	TestTrue(TEXT("Unsupported character acquires downward velocity"), Fixture.Movement->Velocity.Z < -50.0);
	Fixture.Step(165);
	TestTrue(TEXT("Falling character lands on the floor instead of flying or falling through it"),
	         Fixture.Movement->IsMovingOnGround() && Fixture.Movement->CurrentFloor.IsWalkableFloor() &&
	             Fixture.Movement->CurrentFloor.HitResult.GetComponent() == Fixture.Floor);
	TestTrue(TEXT("Landed capsule stands at its actual half-height above the floor"), Fixture.IsAtFloor());
	const double LandedZ = Fixture.Pawn->GetActorLocation().Z;
	Fixture.Step(60);
	TestTrue(TEXT("Grounded character remains on the floor without upward input"),
	         FMath::Abs(Fixture.Pawn->GetActorLocation().Z - LandedZ) < 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceJumpLandingTest,
                                 "Construct.Presence.Movement.JumpAndLand",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceJumpLandingTest::RunTest(const FString& Parameters)
{
	ConstructPresenceMovementTests::FPhysicsWorld Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Settle(*this))
	{
		return false;
	}
	const double GroundZ = Fixture.Pawn->GetActorLocation().Z;
	Fixture.Pawn->Jump();
	Fixture.Step();
	TestTrue(TEXT("A grounded jump produces actual upward velocity"), Fixture.Movement->Velocity.Z > 200.0);
	TestTrue(TEXT("Jump leaves the walking movement mode"), Fixture.Movement->IsFalling());
	double PeakZ = Fixture.Pawn->GetActorLocation().Z;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Fixture.Step();
		PeakZ = FMath::Max(PeakZ, Fixture.Pawn->GetActorLocation().Z);
	}
	TestTrue(TEXT("Jump raises the capsule a useful distance above the floor"), PeakZ > GroundZ + 60.0);
	TestTrue(TEXT("Jump arc returns to the collision floor"),
	         Fixture.Movement->IsMovingOnGround() && Fixture.IsAtFloor());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceSingleJumpTest,
                                 "Construct.Presence.Movement.NoAirJump",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceSingleJumpTest::RunTest(const FString& Parameters)
{
	ConstructPresenceMovementTests::FPhysicsWorld Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Settle(*this))
	{
		return false;
	}
	Fixture.Pawn->Jump();
	Fixture.Step(10);
	Fixture.Pawn->StopJumping();
	Fixture.Step();
	const double BeforeSecondPress = Fixture.Movement->Velocity.Z;
	TestTrue(TEXT("First jump is still airborne before the second press"),
	         Fixture.Movement->IsFalling() && BeforeSecondPress > 50.0);
	Fixture.Pawn->Jump();
	Fixture.Step();
	TestTrue(TEXT("Second airborne press does not inject another upward impulse"),
	         Fixture.Movement->Velocity.Z <= BeforeSecondPress + 1.0);
	Fixture.Pawn->StopJumping();
	Fixture.Step(120);
	TestTrue(TEXT("Character lands after rejecting an airborne second jump"),
	         Fixture.Movement->IsMovingOnGround() && Fixture.IsAtFloor());
	Fixture.Pawn->Jump();
	Fixture.Step();
	TestTrue(TEXT("Landing permits a new grounded jump"), Fixture.Movement->Velocity.Z > 200.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceProxyMovementTest,
                                 "Construct.Presence.Movement.ProxyStaysAtReceivedPose",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceProxyMovementTest::RunTest(const FString& Parameters)
{
	ConstructPresenceMovementTests::FPhysicsWorld Fixture;
	if (!Fixture.Initialize(*this, 350.0f, false))
	{
		return false;
	}
	const FVector InitialPose = Fixture.Pawn->GetActorLocation();
	Fixture.Step(120);
	TestTrue(TEXT("Disabled copy does not fall away from an incoming pose"),
	         Fixture.Pawn->GetActorLocation().Equals(InitialPose, 0.01));
	FConstructPresenceTestAccess::ApplyLocalMovement(*Fixture.Pawn, true);
	Fixture.Step(5);
	TestTrue(TEXT("Ownership-refresh fixture can actually simulate before gating"),
	         Fixture.Movement->Velocity.Z < -20.0);
	FConstructPresenceTestAccess::RefreshMovement(*Fixture.Pawn);
	const FVector UnownedPose = Fixture.Pawn->GetActorLocation();
	Fixture.Step(30);
	TestTrue(TEXT("Refreshing without SDK ownership stops an otherwise moving character"),
	         Fixture.Pawn->GetActorLocation().Equals(UnownedPose, 0.01));
	Fixture.Pawn->SetActorLocation(FVector(0.0f, 0.0f, 700.0f), false, nullptr, ETeleportType::TeleportPhysics);
	// ACharacter::Restart normally restores a default mode. The production override must gate it afterward.
	Fixture.Pawn->Restart();
	Fixture.Step(120);
	TestTrue(TEXT("Restart without SDK ownership cannot make a received copy fall"),
	         FMath::Abs(Fixture.Pawn->GetActorLocation().Z - 700.0) < 0.01);
	TestTrue(TEXT("Unowned restarted character stays outside walking and falling simulation"),
	         Fixture.Movement->MovementMode == MOVE_None && !Fixture.Movement->IsComponentTickEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceMovementReleaseTest,
                                 "Construct.Presence.Movement.ReleaseStopsSimulation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceMovementReleaseTest::RunTest(const FString& Parameters)
{
	ConstructPresenceMovementTests::FPhysicsWorld Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Settle(*this))
	{
		return false;
	}
	Fixture.Pawn->Jump();
	Fixture.Step(8);
	TestTrue(TEXT("Release fixture starts with real upward motion"), Fixture.Movement->Velocity.Z > 100.0);
	FConstructPresenceTestAccess::ApplyLocalMovement(*Fixture.Pawn, false);
	const FVector ReleasedPose = Fixture.Pawn->GetActorLocation();
	Fixture.Step(60);
	TestTrue(TEXT("Releasing movement freezes the copy at its received pose"),
	         Fixture.Pawn->GetActorLocation().Equals(ReleasedPose, 0.01));
	TestTrue(TEXT("Releasing movement removes stale velocity"), Fixture.Movement->Velocity.IsNearlyZero());
	FConstructPresenceTestAccess::ApplyLocalMovement(*Fixture.Pawn, true);
	Fixture.Step(180);
	TestTrue(TEXT("A newly enabled local character resumes gravity and lands"),
	         Fixture.Movement->IsMovingOnGround() && Fixture.IsAtFloor());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPresenceBlockedJumpTest,
                                 "Construct.Presence.Movement.InputBlockCancelsQueuedJump",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPresenceBlockedJumpTest::RunTest(const FString& Parameters)
{
	ConstructPresenceMovementTests::FPhysicsWorld Fixture;
	if (!Fixture.Initialize(*this) || !Fixture.Settle(*this))
	{
		return false;
	}
	const double GroundZ = Fixture.Pawn->GetActorLocation().Z;
	Fixture.Pawn->Jump();
	// The production input-policy callback must cancel a jump queued before a modal/input block.
	FConstructPresenceTestAccess::BlockGameplayInput(*Fixture.Pawn);
	double PeakZ = GroundZ;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Fixture.Step();
		PeakZ = FMath::Max(PeakZ, Fixture.Pawn->GetActorLocation().Z);
	}
	TestTrue(TEXT("Input block cancels a queued jump before it lifts the character"), PeakZ < GroundZ + 1.0);
	TestTrue(TEXT("Blocked jump leaves the character standing on its floor"),
	         Fixture.Movement->IsMovingOnGround() && Fixture.IsAtFloor());
	return true;
}
#endif
