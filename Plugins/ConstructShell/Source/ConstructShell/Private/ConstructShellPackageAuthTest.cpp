#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructSignInWidget.h"
#include "ConstructShellFramework.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogConstructShellPackageAuth, Log, All);

namespace
{
constexpr double RuntimeWaitSeconds = 60.0;
constexpr double RestoreSettleSeconds = 5.0;
constexpr double SetupTimeoutSeconds = 900.0;
constexpr double ReadyStabilitySeconds = 2.0;
const TCHAR* SetupEmailVariable = TEXT("CONSTRUCT_LAYER1_SETUP_EMAIL");

/** Drives the real widget only. It never creates a world, identity, legal answer or session. */
class FConstructPackageAuthWait final : public IAutomationLatentCommand
{
public:
	explicit FConstructPackageAuthWait(FAutomationTestBase* InTest) : Test(InTest), StartedAt(FPlatformTime::Seconds())
	{
	}

	virtual ~FConstructPackageAuthWait() override
	{
		if (!bCompleted && IsInGameThread())
		{
			CancelOwnedAttempt();
		}
	}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - StartedAt > SetupTimeoutSeconds)
		{
			return Fail(TEXT("ConstructShellSignInSetup: timed out waiting for real authenticated runtime readiness"));
		}
		if (!World.IsValid())
		{
			if (bFoundRuntime)
			{
				return Fail(TEXT("ConstructShellSignInSetup: runtime world ended during setup"));
			}
			if (!FindRuntime())
			{
				if (Now - StartedAt > RuntimeWaitSeconds)
				{
					return Fail(TEXT(
					    "ConstructShellSignInSetup: startup runtime, owning player and login widget were not found"));
				}
				return false;
			}
			bFoundRuntime = true;
			ObservedAt = Now;
			UE_LOG(LogConstructShellPackageAuth,
			       Display,
			       TEXT("ConstructShellSignInSetup: observing actual runtime login"));
		}

		APlayerController* PC = Controller.Get();
		UConstructSignInWidget* Login = Widget.Get();
		UCrowdyAuthentication* Auth = Authentication.Get();
		UGameInstance* Instance = World->GetGameInstance();
		AConstructShellHUD* HUD = PC ? Cast<AConstructShellHUD>(PC->GetHUD()) : nullptr;
		UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
		UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
		ULocalPlayer* Local = PC ? PC->GetLocalPlayer() : nullptr;
		UConstructUIInputSubsystem* Gate = Local ? Local->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
		if (!PC || PC->GetWorld() != World.Get() || !Login || Login->GetOwningPlayer() != PC || !Auth || !HUD || !SDK ||
		    !Session || !Gate)
		{
			return Fail(TEXT(
			    "ConstructShellSignInSetup: owning runtime context changed or required public services are missing"));
		}
		if (HUD->IsLocalPreviewEnabled() || Login->GetDesignPreview())
		{
			return Fail(TEXT("ConstructShellSignInSetup: a design or local preview cannot satisfy runtime sign-in"));
		}
		if (Session->HasAccessEnded())
		{
			return Fail(TEXT("ConstructShellSignInSetup: application access ended; no access bypass was attempted"));
		}

		if (!Auth->IsSignedIn() && !bSubmittedEmail && Now - ObservedAt >= RestoreSettleSeconds)
		{
			// The real HUD starts saved-session restore. Do not overlap or cancel that request.
			if (!Login->IsBusy() && !Auth->IsSignInPending() && !Auth->IsLegalAcceptancePending())
			{
				FString Email = FPlatformMisc::GetEnvironmentVariable(SetupEmailVariable).TrimStartAndEnd();
				if (Email.IsEmpty())
				{
					return Fail(TEXT("ConstructShellSignInSetup: required email environment variable is missing"));
				}
				bSubmittedEmail = true;
				SubmittedAt = Now;
				Login->SubmitEmailLink(Email);
				Email.Reset();
				UE_LOG(LogConstructShellPackageAuth,
				       Display,
				       TEXT("ConstructShellSignInSetup: waiting for email callback"));
			}
		}
		if (bSubmittedEmail && Now - SubmittedAt > 2.0 && !Auth->IsSignedIn() && !Auth->IsSignInPending() &&
		    !Login->IsBusy() && !Auth->IsLegalAcceptancePending())
		{
			// Deliberately do not copy backend/UI error text: it may contain account data.
			return Fail(TEXT(
			    "ConstructShellSignInSetup: the real email sign-in request ended without an authenticated session"));
		}

		const bool bReady = Auth->IsSignedIn() && !Auth->IsSignInPending() && !Auth->IsLegalAcceptancePending() &&
		                    SDK->GetUDPConnectionState() == EUDPConnectionState::Connected && !Session->IsAppPaused() &&
		                    !Login->IsBusy() && !HUD->IsSignInVisible() && !Login->IsVisible() &&
		                    Gate->IsGameplayInputAllowed() && PC->GetPawn() &&
		                    PC->GetPawn() != PC->GetSpectatorPawn() && !PC->IsMoveInputIgnored() &&
		                    !PC->IsLookInputIgnored();
		if (!bReady)
		{
			ReadySince = 0.0;
			return false;
		}
		if (ReadySince == 0.0)
		{
			ReadySince = Now;
			return false;
		}
		if (Now - ReadySince < ReadyStabilitySeconds)
		{
			return false;
		}
		if (Test->HasAnyErrors())
		{
			return Fail(
			    TEXT("ConstructShellSignInSetup: runtime readiness was reached but automation recorded errors"));
		}

		// Engine's Automation SoftQuit waits for test completion and requests a non-forced exit.
		// Queue it only after real readiness. Never log out: the SDK owns persisted-session lifetime.
		if (!GEngine->Exec(World.Get(), TEXT("Automation SoftQuit")))
		{
			return Fail(TEXT("ConstructShellSignInSetup: Automation SoftQuit was unavailable"));
		}
		Test->AddInfo(TEXT("ConstructShellSignInSetup: real session connected, login hidden and controls ready"));
		UE_LOG(LogConstructShellPackageAuth, Display, TEXT("ConstructShellSignInSetup: success; graceful exit queued"));
		bCompleted = true;
		return true;
	}

private:
	bool FindRuntime()
	{
		if (!GEngine)
		{
			return false;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Context.WorldType != EWorldType::Game || !Candidate || !Candidate->HasBegunPlay() ||
			    Candidate->GetOutermost()->GetFName() != FName(TEXT("/Game/Construct/Maps/L_ConstructStart")))
			{
				continue;
			}
			APlayerController* PC = Candidate->GetFirstPlayerController();
			UGameInstance* Instance = Candidate->GetGameInstance();
			if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer() || !Instance ||
			    !Cast<AConstructShellHUD>(PC->GetHUD()))
			{
				continue;
			}
			for (TObjectIterator<UConstructSignInWidget> It; It; ++It)
			{
				if (It->GetWorld() == Candidate && It->GetOwningPlayer() == PC && It->IsInViewport() &&
				    !It->GetDesignPreview())
				{
					World = Candidate;
					Controller = PC;
					Widget = *It;
					Authentication = Instance->GetSubsystem<UCrowdyAuthentication>();
					return Authentication.IsValid();
				}
			}
		}
		return false;
	}

	void CancelOwnedAttempt()
	{
		// Public cancellation abandons only this still-pending attempt, without clearing saved sessions.
		if (bSubmittedEmail && Authentication.IsValid() && !Authentication->IsSignedIn() &&
		    Authentication->IsSignInPending())
		{
			Authentication->CancelSignIn();
		}
	}

	bool Fail(const TCHAR* FixedMessage)
	{
		CancelOwnedAttempt();
		Test->AddError(FixedMessage);
		bCompleted = true;
		return true;
	}

	FAutomationTestBase* Test;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<UConstructSignInWidget> Widget;
	TWeakObjectPtr<UCrowdyAuthentication> Authentication;
	double StartedAt;
	double ObservedAt = 0.0;
	double SubmittedAt = 0.0;
	double ReadySince = 0.0;
	bool bFoundRuntime = false;
	bool bSubmittedEmail = false;
	bool bCompleted = false;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructShellPackageAuthTest,
                                 "Construct.Shell.Packaged.RuntimeSignInSetup",
                                 EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructShellPackageAuthTest::RunTest(const FString& Parameters)
{
	if (GIsEditor || IsRunningCommandlet() || !FPlatformProperties::RequiresCookedData())
	{
		AddError(TEXT("ConstructShellSignInSetup: this setup runs only in the cooked development client"));
		return false;
	}
	if (FPlatformMisc::GetEnvironmentVariable(SetupEmailVariable).TrimStartAndEnd().IsEmpty())
	{
		AddError(TEXT(
		    "ConstructShellSignInSetup: CONSTRUCT_LAYER1_SETUP_EMAIL must be supplied through the process environment"));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FConstructPackageAuthWait(this));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
