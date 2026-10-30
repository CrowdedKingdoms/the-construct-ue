#include "ConstructPlayerNameWidget.h"
#include "ConstructPresencePawn.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructUIInputSubsystem.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// Access reaches real Construct operations and state; no SDK identity or service is injected.
struct FConstructPlayerNameTestAccess
{
	static void Commit(AConstructPresencePawn& Pawn, const FCrowdyPlayerProfile& Profile)
	{
		Pawn.CommitPlayerProfile(Profile);
	}

	static bool TryConfirmedCommit(AConstructPresencePawn& Pawn, const FCrowdyPlayerProfile& Profile)
	{
		return Pawn.ApplyConfirmedPlayerProfile(Profile);
	}

	static void ArmInitialRead(AConstructPresencePawn& Pawn)
	{
		Pawn.bNameLookupPending = true;
	}

	static bool IsInitialReadPending(const AConstructPresencePawn& Pawn)
	{
		return Pawn.bNameLookupPending;
	}

	static void DeliverInitialRead(AConstructPresencePawn& Pawn, const FCrowdyPlayerProfile& Profile)
	{
		Pawn.HandleNameRead(Profile);
	}

	static const FString& PawnName(const AConstructPresencePawn& Pawn)
	{
		return Pawn.PlayerName;
	}

	static FString PawnText(const AConstructPresencePawn& Pawn)
	{
		return Pawn.NameTag->Text.ToString();
	}

	static const FCrowdyPlayerProfile& CachedProfile(const AConstructPresencePawn& Pawn)
	{
		return Pawn.LocalProfile;
	}

	static bool HasSuccessfulProfile(const AConstructPresencePawn& Pawn)
	{
		return Pawn.bLocalProfileReadComplete && Pawn.bLocalProfileReadSucceeded;
	}

	static void SetModel(UConstructPlayerNameWidget& Widget, const FCrowdyPlayerProfile& Profile, bool bReplaceDraft)
	{
		Widget.SetProfileModel(Profile, bReplaceDraft);
	}

	static void SeedDraft(UConstructPlayerNameWidget& Widget, const FString& Gamertag, const FString& Identifier)
	{
		Widget.DraftGamertag = Gamertag;
		Widget.DraftDisambiguation = Identifier;
		Widget.bDirty = true;
	}

	static FString CurrentName(const UConstructPlayerNameWidget& Widget)
	{
		return Widget.CurrentGamertag;
	}

	static FString CurrentIdentifier(const UConstructPlayerNameWidget& Widget)
	{
		return Widget.CurrentDisambiguation;
	}

	static bool IsDirty(const UConstructPlayerNameWidget& Widget)
	{
		return Widget.bDirty;
	}

	static void SetMessage(UConstructPlayerNameWidget& Widget, const FText& Message)
	{
		Widget.Message = Message;
	}

	static UConstructPlayerNameRequest* MakeRequest(UConstructPlayerNameWidget& Widget)
	{
		UConstructPlayerNameRequest* Request = NewObject<UConstructPlayerNameRequest>(&Widget);
		Request->Widget = &Widget;
		Request->Pawn = Widget.OwnerPawn;
		Request->ExpectedUserID = 900001;
		Request->bSave = true;
		return Request;
	}

	static void SetActive(UConstructPlayerNameWidget& Widget, UConstructPlayerNameRequest* Request)
	{
		Widget.ActiveRequest = Request;
	}

	static UConstructPlayerNameRequest* Active(const UConstructPlayerNameWidget& Widget)
	{
		return Widget.ActiveRequest.Get();
	}

	static void Succeed(UConstructPlayerNameRequest& Request, const FCrowdyPlayerProfile& Profile)
	{
		Request.Succeeded(Profile);
	}

	static void Fail(UConstructPlayerNameRequest& Request)
	{
		Request.Failed(FCrowdyAccountError(), TEXT("Synthetic callback failure"));
	}

	static bool IsLive(const UConstructPlayerNameWidget& Widget)
	{
		return Widget.bLive;
	}

	static void SeedOpenForm(UConstructPlayerNameWidget& Widget)
	{
		Widget.bOpen = true;
		Widget.bInitialPromptPending = true;
	}

	static bool CanClose(const UConstructPlayerNameWidget& Widget)
	{
		return Widget.CanClose();
	}

	static bool CanInteract(const UConstructPlayerNameWidget& Widget)
	{
		return Widget.CanInteract();
	}
};

namespace ConstructPlayerNameTests
{
FCrowdyPlayerProfile Profile(const TCHAR* Gamertag, const TCHAR* Identifier = TEXT("0001"))
{
	FCrowdyPlayerProfile Result;
	Result.UserID = 900001;
	Result.Gamertag = Gamertag;
	Result.Disambiguation = Identifier;
	return Result;
}

struct FFixture
{
	UWorld* World = nullptr;
	AConstructPresencePawn* Pawn = nullptr;
	APlayerController* Controller = nullptr;
	TStrongObjectPtr<ULocalPlayer> LocalPlayer;
	TStrongObjectPtr<UConstructPlayerNameWidget> Widget;
	bool bPlayerAdded = false;

	~FFixture()
	{
		if (Widget.IsValid())
		{
			Widget->Shutdown();
		}
		Widget.Reset();
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

	bool Initialize(FAutomationTestBase& Test, bool bWithLocalPlayer = false)
	{
		if (!Test.TestNotNull(TEXT("Player-name fixture needs an initialized Engine"), GEngine))
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
		if (!Test.TestNotNull(TEXT("Player-name fixture owns a preview world"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		Pawn = World->SpawnActor<AConstructPresencePawn>();
		if (!Test.TestNotNull(TEXT("Player-name fixture owns the actual production pawn"), Pawn) ||
		    !Test.TestNull(TEXT("Player-name unit fixture creates no GameInstance"), World->GetGameInstance()))
		{
			return false;
		}
		if (bWithLocalPlayer)
		{
			Controller = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("Player-name modal fixture owns a real controller"), Controller))
			{
				return false;
			}
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));
			LocalPlayer->PlayerAdded(nullptr, int32(0));
			bPlayerAdded = true;
			Controller->SetPlayer(LocalPlayer.Get());
			Controller->Possess(Pawn);
		}
		// NewObject deliberately avoids NativeConstruct, viewport UI and all SDK requests.
		Widget.Reset(NewObject<UConstructPlayerNameWidget>(Pawn));
		if (Controller)
		{
			Widget->SetOwningPlayer(Controller);
		}
		Widget->InitializeForPawn(Pawn);
		return Test.TestTrue(TEXT("Production widget has a live Construct lifetime"),
		                     FConstructPlayerNameTestAccess::IsLive(*Widget));
	}
};
} // namespace ConstructPlayerNameTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPlayerNameCommitTest,
                                 "Construct.Presence.PlayerName.CommitProfile",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPlayerNameCommitTest::RunTest(const FString& Parameters)
{
	ConstructPlayerNameTests::FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const FCrowdyPlayerProfile Confirmed =
	    ConstructPlayerNameTests::Profile(TEXT("CanonicalSyntheticName"), TEXT("0042"));
	FConstructPlayerNameTestAccess::ArmInitialRead(*Fixture.Pawn);
	FConstructPlayerNameTestAccess::Commit(*Fixture.Pawn, Confirmed);
	TestFalse(TEXT("Confirmed profile invalidates the older initial lookup"),
	          FConstructPlayerNameTestAccess::IsInitialReadPending(*Fixture.Pawn));
	TestEqual(TEXT("Confirmed profile updates the actual CrowdyState name"),
	          FConstructPlayerNameTestAccess::PawnName(*Fixture.Pawn),
	          Confirmed.Gamertag);
	TestEqual(TEXT("Confirmed profile updates the actual name tag"),
	          FConstructPlayerNameTestAccess::PawnText(*Fixture.Pawn),
	          Confirmed.Gamertag);
	TestEqual(TEXT("Confirmed profile caches the authoritative identifier"),
	          FConstructPlayerNameTestAccess::CachedProfile(*Fixture.Pawn).Disambiguation,
	          Confirmed.Disambiguation);
	TestTrue(TEXT("Confirmed profile records a completed successful read"),
	         FConstructPlayerNameTestAccess::HasSuccessfulProfile(*Fixture.Pawn));
	FConstructPlayerNameTestAccess::DeliverInitialRead(
	    *Fixture.Pawn, ConstructPlayerNameTests::Profile(TEXT("OldSyntheticInitialRead")));
	TestEqual(TEXT("An older initial callback cannot overwrite the committed name"),
	          FConstructPlayerNameTestAccess::PawnName(*Fixture.Pawn),
	          Confirmed.Gamertag);
	// Without the explicit pending-state assertion above, the unavailable session could mask a missing invalidation.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPlayerNameRejectedCommitTest,
                                 "Construct.Presence.PlayerName.RejectUnavailableIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPlayerNameRejectedCommitTest::RunTest(const FString& Parameters)
{
	ConstructPlayerNameTests::FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const FCrowdyPlayerProfile Previous = ConstructPlayerNameTests::Profile(TEXT("PreviousSyntheticName"));
	FConstructPlayerNameTestAccess::Commit(*Fixture.Pawn, Previous);
	FConstructPlayerNameTestAccess::ArmInitialRead(*Fixture.Pawn);
	TestFalse(TEXT("Profile application requires a real current signed-in owner"),
	          FConstructPlayerNameTestAccess::TryConfirmedCommit(
	              *Fixture.Pawn, ConstructPlayerNameTests::Profile(TEXT("UnacceptedSyntheticName"))));
	TestEqual(TEXT("Rejected profile cannot change the pawn's current name"),
	          FConstructPlayerNameTestAccess::PawnName(*Fixture.Pawn),
	          Previous.Gamertag);
	TestEqual(TEXT("Rejected profile cannot change the actual name tag"),
	          FConstructPlayerNameTestAccess::PawnText(*Fixture.Pawn),
	          Previous.Gamertag);
	TestTrue(TEXT("Rejected profile does not consume an unrelated initial lookup"),
	         FConstructPlayerNameTestAccess::IsInitialReadPending(*Fixture.Pawn));
	// This proves aggregate rejection, not any one SDK ownership/session subcondition in isolation.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPlayerNameDirtyModelTest,
                                 "Construct.Presence.PlayerName.DirtyDraftAndCanonicalModel",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPlayerNameDirtyModelTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UConstructPlayerNameWidget> Widget(NewObject<UConstructPlayerNameWidget>());
	FConstructPlayerNameTestAccess::SetModel(
	    *Widget, ConstructPlayerNameTests::Profile(TEXT("InitialSyntheticName")), true);
	FConstructPlayerNameTestAccess::SeedDraft(*Widget, TEXT("UnsavedSyntheticDraft"), TEXT("DraftIdentifier"));
	const FCrowdyPlayerProfile Refresh =
	    ConstructPlayerNameTests::Profile(TEXT("RefreshedSyntheticName"), TEXT("0084"));
	FConstructPlayerNameTestAccess::SetModel(*Widget, Refresh, false);
	TestEqual(TEXT("Authoritative refresh updates the confirmed model"),
	          FConstructPlayerNameTestAccess::CurrentName(*Widget),
	          Refresh.Gamertag);
	TestEqual(TEXT("Authoritative refresh preserves the unsaved gamertag draft"),
	          Widget->GetGamertag(),
	          FString(TEXT("UnsavedSyntheticDraft")));
	TestEqual(TEXT("Authoritative refresh preserves the unsaved identifier draft"),
	          Widget->GetDisambiguation(),
	          FString(TEXT("DraftIdentifier")));
	TestTrue(TEXT("Refresh does not mark unsaved edits clean"), FConstructPlayerNameTestAccess::IsDirty(*Widget));
	const FCrowdyPlayerProfile Canonical =
	    ConstructPlayerNameTests::Profile(TEXT("ServerCanonicalSyntheticName"), TEXT("0105"));
	FConstructPlayerNameTestAccess::SetModel(*Widget, Canonical, true);
	TestEqual(TEXT("Accepted canonical model replaces the submitted gamertag draft"),
	          Widget->GetGamertag(),
	          Canonical.Gamertag);
	TestEqual(TEXT("Accepted canonical model replaces the submitted identifier draft"),
	          Widget->GetDisambiguation(),
	          Canonical.Disambiguation);
	TestEqual(TEXT("Accepted canonical model retains its confirmed identifier"),
	          FConstructPlayerNameTestAccess::CurrentIdentifier(*Widget),
	          Canonical.Disambiguation);
	TestFalse(TEXT("Accepted canonical model clears the dirty marker"),
	          FConstructPlayerNameTestAccess::IsDirty(*Widget));
	// This calls the production model operation; real accepted request/session validation remains a live check.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPlayerNameStaleReceiverTest,
                                 "Construct.Presence.PlayerName.StaleReceiverPreservesCurrentRequest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPlayerNameStaleReceiverTest::RunTest(const FString& Parameters)
{
	ConstructPlayerNameTests::FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	TStrongObjectPtr<UConstructPlayerNameRequest> Old(FConstructPlayerNameTestAccess::MakeRequest(*Fixture.Widget));
	TStrongObjectPtr<UConstructPlayerNameRequest> Current(FConstructPlayerNameTestAccess::MakeRequest(*Fixture.Widget));
	FConstructPlayerNameTestAccess::SetActive(*Fixture.Widget, Current.Get());
	const FText CurrentMessage = FText::FromString(TEXT("Current synthetic request"));
	FConstructPlayerNameTestAccess::SetMessage(*Fixture.Widget, CurrentMessage);
	FConstructPlayerNameTestAccess::SeedDraft(*Fixture.Widget, TEXT("CurrentSyntheticDraft"), TEXT("0123"));
	FConstructPlayerNameTestAccess::Succeed(*Old, ConstructPlayerNameTests::Profile(TEXT("OldSyntheticReply")));
	TestTrue(TEXT("Stale success cannot detach a newer active receiver"),
	         FConstructPlayerNameTestAccess::Active(*Fixture.Widget) == Current.Get());
	TestTrue(TEXT("Stale success cannot release the current busy state"), Fixture.Widget->IsBusy());
	FConstructPlayerNameTestAccess::Fail(*Old);
	TestTrue(TEXT("Stale failure cannot detach a newer active receiver"),
	         FConstructPlayerNameTestAccess::Active(*Fixture.Widget) == Current.Get());
	TestEqual(TEXT("Stale replies cannot replace the current status message"),
	          Fixture.Widget->GetMessage().ToString(),
	          CurrentMessage.ToString());
	TestEqual(TEXT("Stale replies cannot replace the user's current draft"),
	          Fixture.Widget->GetGamertag(),
	          FString(TEXT("CurrentSyntheticDraft")));
	// Removing ActiveRequest != Request is observable even with no session: ConsumeRequest would clear Current.
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPlayerNameNoSessionReplyTest,
                                 "Construct.Presence.PlayerName.CurrentReplyWithoutSession",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPlayerNameNoSessionReplyTest::RunTest(const FString& Parameters)
{
	ConstructPlayerNameTests::FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const FCrowdyPlayerProfile Previous = ConstructPlayerNameTests::Profile(TEXT("ConfirmedSyntheticName"));
	FConstructPlayerNameTestAccess::Commit(*Fixture.Pawn, Previous);
	FConstructPlayerNameTestAccess::SetModel(*Fixture.Widget, Previous, true);
	FConstructPlayerNameTestAccess::SeedDraft(*Fixture.Widget, TEXT("UnsavedSyntheticDraft"), TEXT("0134"));
	const FText PreviousMessage = FText::FromString(TEXT("Existing synthetic status"));
	FConstructPlayerNameTestAccess::SetMessage(*Fixture.Widget, PreviousMessage);
	for (bool bSuccess : {true, false})
	{
		TStrongObjectPtr<UConstructPlayerNameRequest> Current(
		    FConstructPlayerNameTestAccess::MakeRequest(*Fixture.Widget));
		FConstructPlayerNameTestAccess::SetActive(*Fixture.Widget, Current.Get());
		if (bSuccess)
		{
			FConstructPlayerNameTestAccess::Succeed(*Current,
			                                        ConstructPlayerNameTests::Profile(TEXT("UnacceptedSyntheticName")));
		}
		else
		{
			FConstructPlayerNameTestAccess::Fail(*Current);
		}
		TestFalse(TEXT("A matching reply without a valid session clears its own busy state"), Fixture.Widget->IsBusy());
		TestNull(TEXT("A matching rejected reply detaches its own receiver"),
		         FConstructPlayerNameTestAccess::Active(*Fixture.Widget));
		TestEqual(TEXT("A session-invalid reply preserves the confirmed model"),
		          FConstructPlayerNameTestAccess::CurrentName(*Fixture.Widget),
		          Previous.Gamertag);
		TestEqual(TEXT("A session-invalid reply preserves entered draft text"),
		          Fixture.Widget->GetGamertag(),
		          FString(TEXT("UnsavedSyntheticDraft")));
		TestEqual(TEXT("A session-invalid reply preserves the entered identifier"),
		          Fixture.Widget->GetDisambiguation(),
		          FString(TEXT("0134")));
		TestEqual(TEXT("A session-invalid reply preserves the current pawn name"),
		          FConstructPlayerNameTestAccess::PawnName(*Fixture.Pawn),
		          Previous.Gamertag);
		TestEqual(TEXT("A session-invalid reply cannot replace the status message"),
		          Fixture.Widget->GetMessage().ToString(),
		          PreviousMessage.ToString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPlayerNameShutdownTest,
                                 "Construct.Presence.PlayerName.ShutdownReleasesOnlyOwnModal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPlayerNameShutdownTest::RunTest(const FString& Parameters)
{
	ConstructPlayerNameTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, true))
	{
		return false;
	}
	UConstructUIInputSubsystem* Gate = Fixture.LocalPlayer->GetSubsystem<UConstructUIInputSubsystem>();
	if (!TestNotNull(TEXT("Shutdown fixture has the real LocalPlayer input gate"), Gate))
	{
		return false;
	}
	const FName NameModal(TEXT("ConstructPlayerName"));
	const FName OtherModal(TEXT("IndependentSyntheticModal"));
	const FCrowdyPlayerProfile Previous = ConstructPlayerNameTests::Profile(TEXT("ConfirmedSyntheticName"));
	FConstructPlayerNameTestAccess::Commit(*Fixture.Pawn, Previous);
	FConstructPlayerNameTestAccess::SetModel(*Fixture.Widget, Previous, true);
	FConstructPlayerNameTestAccess::SeedOpenForm(*Fixture.Widget);
	Gate->SetSessionReady(true);
	Gate->SetModal(NameModal, true);
	Gate->SetModal(OtherModal, true);
	TestFalse(TEXT("Two real modals block gameplay before shutdown"), Gate->IsGameplayInputAllowed());
	TestTrue(TEXT("Shutdown fixture begins with an open name form"), Fixture.Widget->IsOpen());
	TestTrue(TEXT("Modal policy reaches actual controller movement input"), Fixture.Controller->IsMoveInputIgnored());
	TStrongObjectPtr<UConstructPlayerNameRequest> Pending(FConstructPlayerNameTestAccess::MakeRequest(*Fixture.Widget));
	FConstructPlayerNameTestAccess::SetActive(*Fixture.Widget, Pending.Get());
	FConstructPlayerNameTestAccess::SeedDraft(*Fixture.Widget, TEXT("PreservedSyntheticDraft"), TEXT("0145"));
	Fixture.Widget->Shutdown();
	TestFalse(TEXT("Shutdown marks the widget lifetime inactive"),
	          FConstructPlayerNameTestAccess::IsLive(*Fixture.Widget));
	TestFalse(TEXT("Shutdown detaches its active save receiver"), Fixture.Widget->IsBusy());
	TestTrue(TEXT("Shutdown releases the name modal while preserving the other modal"),
	         Gate->CanUseNavigationInput(OtherModal));
	TestFalse(TEXT("Another screen's modal still blocks gameplay"), Gate->IsGameplayInputAllowed());
	Fixture.Widget->Shutdown();
	FConstructPlayerNameTestAccess::Succeed(*Pending, ConstructPlayerNameTests::Profile(TEXT("LateSyntheticName")));
	FConstructPlayerNameTestAccess::Fail(*Pending);
	TestFalse(TEXT("Late callbacks cannot recreate a request after repeated shutdown"), Fixture.Widget->IsBusy());
	TestFalse(TEXT("Late callbacks cannot reopen the editor"), Fixture.Widget->IsOpen());
	TestEqual(TEXT("Shutdown and late callbacks preserve the detached draft"),
	          Fixture.Widget->GetGamertag(),
	          FString(TEXT("PreservedSyntheticDraft")));
	TestEqual(TEXT("Late callbacks after shutdown cannot replace the confirmed widget model"),
	          FConstructPlayerNameTestAccess::CurrentName(*Fixture.Widget),
	          Previous.Gamertag);
	TestEqual(TEXT("Late callbacks after shutdown cannot replace the actual pawn name"),
	          FConstructPlayerNameTestAccess::PawnName(*Fixture.Pawn),
	          Previous.Gamertag);
	TestEqual(TEXT("Late callbacks after shutdown cannot replace the actual name tag"),
	          FConstructPlayerNameTestAccess::PawnText(*Fixture.Pawn),
	          Previous.Gamertag);
	TestTrue(TEXT("Repeated shutdown and late callbacks do not reacquire the name modal"),
	         Gate->CanUseNavigationInput(OtherModal));
	TestTrue(TEXT("Other modal continues to block actual controller movement"),
	         Fixture.Controller->IsMoveInputIgnored());
	Gate->SetModal(OtherModal, false);
	TestTrue(TEXT("Releasing the last unrelated modal restores gameplay"), Gate->IsGameplayInputAllowed());
	TestFalse(TEXT("Name-widget teardown leaks no controller movement block"),
	          Fixture.Controller->IsMoveInputIgnored());
	TestFalse(TEXT("Name-widget teardown leaks no controller look block"), Fixture.Controller->IsLookInputIgnored());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPlayerNameCloseWithoutSessionTest,
                                 "Construct.Presence.PlayerName.CloseWithoutReadySession",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructPlayerNameCloseWithoutSessionTest::RunTest(const FString& Parameters)
{
	ConstructPlayerNameTests::FFixture Fixture;
	if (!Fixture.Initialize(*this, true))
	{
		return false;
	}
	UConstructUIInputSubsystem* Gate = Fixture.LocalPlayer->GetSubsystem<UConstructUIInputSubsystem>();
	if (!TestNotNull(TEXT("Close test has the real LocalPlayer input gate"), Gate))
	{
		return false;
	}
	const FName NameModal(TEXT("ConstructPlayerName"));
	Gate->SetSessionReady(false);
	Gate->SetModal(NameModal, true);
	FConstructPlayerNameTestAccess::SeedOpenForm(*Fixture.Widget);
	TestFalse(TEXT("Form is not editable while the session is unready"),
	          FConstructPlayerNameTestAccess::CanInteract(*Fixture.Widget));
	TestTrue(TEXT("Close remains enabled for an open form"), FConstructPlayerNameTestAccess::CanClose(*Fixture.Widget));
	Fixture.Widget->CloseEditor();
	TestFalse(TEXT("Close dismisses the form without a ready session"), Fixture.Widget->IsOpen());
	TestFalse(TEXT("Closed form no longer qualifies for its close control"),
	          FConstructPlayerNameTestAccess::CanClose(*Fixture.Widget));
	Gate->SetSessionReady(true);
	TestTrue(TEXT("Closing releases the name modal and restores gameplay input"), Gate->IsGameplayInputAllowed());
	return true;
}
#endif
