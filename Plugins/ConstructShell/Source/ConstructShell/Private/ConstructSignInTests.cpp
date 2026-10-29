#include "ConstructSignInWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructSignInValidationTest,
                                 "Construct.Shell.SignIn.Validation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructSignInValidationTest::RunTest(const FString& Parameters)
{
	UConstructSignInWidget* Widget = NewObject<UConstructSignInWidget>();
	Widget->SubmitPassword(TEXT(""), TEXT(""), false);
	TestTrue(TEXT("Missing login credentials have a visible validation error"), Widget->HasError());
	TestFalse(TEXT("Invalid login does not start authentication"), Widget->IsBusy());
	Widget->SubmitEmailLink(TEXT("   "));
	TestTrue(TEXT("An email link requires a nonblank email"), Widget->HasError());
	TestFalse(TEXT("Invalid link request does not enter a waiting state"), Widget->IsBusy());
	Widget->RestoreSavedSession();
	TestTrue(TEXT("A widget without an SDK explains the unavailable service"), Widget->HasError());
	TestFalse(TEXT("An unavailable SDK cannot leave the screen busy"), Widget->IsBusy());
	Widget->ShowAccessEnded(TEXT("Access suspended"), TEXT("2030-01-01T00:00:00Z"));
	Widget->SubmitPassword(TEXT(""), TEXT(""), false);
	TestTrue(TEXT("Access deadline remains visible beside later form errors"),
	         Widget->GetMessage().ToString().Contains(TEXT("2030-01-01T00:00:00Z")));
	Widget->SetDesignPreview(true);
	TestEqual(TEXT("Reference preview includes two example provider rows"), Widget->GetProviders().Num(), 2);
	Widget->SubmitPassword(TEXT("preview@example.invalid"), TEXT("preview-only"), false);
	TestFalse(TEXT("Preview actions never start real authentication"), Widget->IsBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructSignInRequestIsolationTest,
                                 "Construct.Shell.SignIn.CanceledReplyIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructSignInRequestIsolationTest::RunTest(const FString& Parameters)
{
	UConstructSignInWidget* Widget = NewObject<UConstructSignInWidget>();
	UConstructSignInRequest* OldRequest = NewObject<UConstructSignInRequest>(Widget);
	OldRequest->Initialize(Widget);
	UConstructSignInRequest* NewRequest = NewObject<UConstructSignInRequest>(Widget);
	NewRequest->Initialize(Widget);
	Widget->ActiveRequest = NewRequest;
	Widget->bPending = true;
	Widget->SetMessage(FText::FromString(TEXT("Current request")));
	OldRequest->Failed(TEXT("Canceled old request"));
	TestTrue(TEXT("Canceled error cannot release the current request"), Widget->IsBusy());
	TestEqual(TEXT("Canceled error cannot replace the current message"),
	          Widget->GetMessage().ToString(),
	          FString(TEXT("Current request")));
	OldRequest->Succeeded(FCrowdyAuthResult());
	TestTrue(TEXT("Canceled success cannot finish the current request"), Widget->IsBusy());
	NewRequest->Failed(TEXT("Current request error"));
	TestFalse(TEXT("The current result releases its own busy state"), Widget->IsBusy());
	TestTrue(TEXT("The current error is displayed"), Widget->HasError());
	TestEqual(TEXT("The current error text is preserved"),
	          Widget->GetMessage().ToString(),
	          FString(TEXT("Current request error")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructSignInCancellationTest,
                                 "Construct.Shell.SignIn.PendingRestoreCancellation",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructSignInCancellationTest::RunTest(const FString& Parameters)
{
	// The red version calls real SDK Logout, so refuse to run against a user's normal saved-session namespace.
	FString SessionNamespace;
	if (!FParse::Value(FCommandLine::Get(), TEXT("crowdysession="), SessionNamespace) ||
	    !SessionNamespace.Equals(TEXT("Layer1Regression"), ESearchCase::IgnoreCase))
	{
		AddError(TEXT("Pending-restore cancellation regression requires -crowdysession=Layer1Regression."));
		return false;
	}
	if (!TestNotNull(TEXT("An Engine is available for the isolated game instance"), GEngine))
	{
		return false;
	}

	UWorld* RuntimeWorld = nullptr;
	APlayerController* RuntimeController = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* Candidate = Context.World();
		if (!Candidate || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game) ||
		    !Candidate->HasBegunPlay() || Candidate->bIsTearingDown || !Candidate->GetGameInstance())
		{
			continue;
		}
		APlayerController* CandidateController = UGameplayStatics::GetPlayerController(Candidate, 0);
		if (!CandidateController || !CandidateController->IsLocalController() || !CandidateController->GetLocalPlayer())
		{
			continue;
		}
		if (RuntimeWorld)
		{
			AddError(TEXT("Pending-restore cancellation regression requires exactly one local Game/PIE fixture."));
			return false;
		}
		RuntimeWorld = Candidate;
		RuntimeController = CandidateController;
	}
	if (!RuntimeWorld)
	{
		AddError(TEXT("Start the isolated single-player Game/PIE fixture before running this regression."));
		return false;
	}
	UGameInstance* Instance = RuntimeWorld->GetGameInstance();
	UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>();
	UCrowdyAuthentication* Auth = Instance->GetSubsystem<UCrowdyAuthentication>();
	if (!TestNotNull(TEXT("The initialized runtime SDK subsystem exists"), SDK) ||
	    !TestNotNull(TEXT("The initialized runtime authentication subsystem exists"), Auth))
	{
		return false;
	}
	if (Auth->IsSignedIn() || Auth->IsSignInPending())
	{
		AddError(TEXT(
		    "Wait for the isolated fixture's restore miss; no real sign-in may be active during this regression."));
		return false;
	}

	// NewObject deliberately avoids NativeConstruct/provider discovery and never starts an actual restore/network flow.
	TStrongObjectPtr<UConstructSignInWidget> PendingRestore(NewObject<UConstructSignInWidget>(RuntimeController));
	if (!TestEqual(TEXT("The widget resolves the initialized authentication service"),
	               PendingRestore->GetAuthentication(),
	               Auth))
	{
		return false;
	}
	TStrongObjectPtr<UWidget> LogoutMarker(NewObject<UButton>(RuntimeController));
	TStrongObjectPtr<UWidget> LogoutFailureMarker(NewObject<UButton>(RuntimeController));
	LogoutMarker->SetIsEnabled(false);
	LogoutFailureMarker->SetIsEnabled(true);
	// Two reflected bool markers detect either logout result without a test UObject or runtime hook.
	SDK->OnLogout.AddDynamic(LogoutMarker.Get(), &UWidget::SetIsEnabled);
	SDK->OnLogout.AddDynamic(LogoutFailureMarker.Get(), &UWidget::SetIsEnabled);
	ON_SCOPE_EXIT
	{
		SDK->OnLogout.RemoveDynamic(LogoutMarker.Get(), &UWidget::SetIsEnabled);
		SDK->OnLogout.RemoveDynamic(LogoutFailureMarker.Get(), &UWidget::SetIsEnabled);
	};
	TestTrue(TEXT("The logout marker is subscribed to the real SDK delegate"),
	         SDK->OnLogout.IsAlreadyBound(LogoutMarker.Get(), &UWidget::SetIsEnabled));
	TestFalse(TEXT("The logout marker starts clear"), LogoutMarker->GetIsEnabled());
	TestTrue(TEXT("The failed-logout marker starts set"), LogoutFailureMarker->GetIsEnabled());

	UConstructSignInRequest* CanceledRestore = NewObject<UConstructSignInRequest>(PendingRestore.Get());
	CanceledRestore->Initialize(PendingRestore.Get());
	PendingRestore->ActiveRequest = CanceledRestore;
	PendingRestore->bPending = true;
	PendingRestore->bRestoring = true;
	TestTrue(TEXT("The simulated restore is pending before cancellation"), PendingRestore->IsBusy());
	PendingRestore->CancelSignIn();
	TestFalse(TEXT("Canceling a pending restore does not broadcast SDK OnLogout"), LogoutMarker->GetIsEnabled());
	TestTrue(TEXT("Canceling a pending restore does not broadcast a failed SDK logout"),
	         LogoutFailureMarker->GetIsEnabled());
	TestFalse(TEXT("Canceling a pending restore clears the busy state"), PendingRestore->IsBusy());
	TestFalse(TEXT("Canceling a pending restore clears the restore flag"), PendingRestore->bRestoring);
	TestNull(TEXT("The canceled request is detached before SDK cancellation can answer"),
	         PendingRestore->ActiveRequest.Get());
	const FString CanceledMessage = PendingRestore->GetMessage().ToString();
	CanceledRestore->Failed(TEXT("Late canceled-restore failure"));
	CanceledRestore->Succeeded(FCrowdyAuthResult());
	TestEqual(TEXT("Late canceled-restore replies cannot replace cancellation feedback"),
	          PendingRestore->GetMessage().ToString(),
	          CanceledMessage);
	TestFalse(TEXT("Late canceled-restore replies cannot restore a busy state"), PendingRestore->IsBusy());
	PendingRestore->CancelSignIn();
	TestFalse(TEXT("Repeated cancellation does not broadcast SDK OnLogout"), LogoutMarker->GetIsEnabled());
	TestTrue(TEXT("Repeated cancellation does not broadcast a failed SDK logout"), LogoutFailureMarker->GetIsEnabled());
	return true;
}
#endif
