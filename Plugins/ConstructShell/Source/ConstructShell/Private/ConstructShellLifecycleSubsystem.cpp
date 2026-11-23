#include "ConstructShellLifecycleSubsystem.h"
#include "ConstructShellDiagnostics.h"

#include "Engine/GameInstance.h"
#include "Misc/App.h"
#include "Engine/LocalPlayer.h"
#include "ConstructUIInputSubsystem.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogConstructShell, Log, All);

void UConstructShellLifecycleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCrowdySDKSubsystem>();
	Collection.InitializeDependency<UCrowdyAuthentication>();
	Collection.InitializeDependency<UCrowdyGameSession>();
	UCrowdySDKSubsystem* SDK = GetGameInstance()->GetSubsystem<UCrowdySDKSubsystem>();
	UCrowdyAuthentication* Auth = GetGameInstance()->GetSubsystem<UCrowdyAuthentication>();
	UCrowdyGameSession* Session = GetGameInstance()->GetSubsystem<UCrowdyGameSession>();
	SDK->OnLogin.AddUniqueDynamic(this, &ThisClass::Login);
	SDK->OnRegister.AddUniqueDynamic(this, &ThisClass::Register);
	SDK->OnUDPConnectionSuccess.AddUniqueDynamic(this, &ThisClass::Connected);
	SDK->OnUDPTimedOut.AddUniqueDynamic(this, &ThisClass::ConnectionLost);
	SDK->OnUDPAddressNotify.AddUniqueDynamic(this, &ThisClass::Address);
	SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::Logout);
	Auth->OnAppTokenRefreshed.AddUniqueDynamic(this, &ThisClass::Refreshed);
	if (FApp::CanEverRender() && !IsRunningDedicatedServer() && !IsRunningCommandlet())
	{
		Auth->OnLegalAcceptanceRequired.AddUniqueDynamic(this, &ThisClass::LegalRequired);
		Auth->OnLegalAcceptanceEnded.AddUniqueDynamic(this, &ThisClass::LegalEnded);
	}
	Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
	Session->OnAppPausedChanged.AddUniqueDynamic(this, &ThisClass::Paused);
}

void UConstructShellLifecycleSubsystem::Deinitialize()
{
	if (UCrowdySDKSubsystem* SDK = GetGameInstance()->GetSubsystem<UCrowdySDKSubsystem>())
	{
		SDK->OnLogin.RemoveDynamic(this, &ThisClass::Login);
		SDK->OnRegister.RemoveDynamic(this, &ThisClass::Register);
		SDK->OnUDPConnectionSuccess.RemoveDynamic(this, &ThisClass::Connected);
		SDK->OnUDPTimedOut.RemoveDynamic(this, &ThisClass::ConnectionLost);
		SDK->OnUDPAddressNotify.RemoveDynamic(this, &ThisClass::Address);
		SDK->OnLogout.RemoveDynamic(this, &ThisClass::Logout);
	}
	if (UCrowdyAuthentication* Auth = GetGameInstance()->GetSubsystem<UCrowdyAuthentication>())
	{
		Auth->OnAppTokenRefreshed.RemoveDynamic(this, &ThisClass::Refreshed);
		Auth->OnLegalAcceptanceRequired.RemoveDynamic(this, &ThisClass::LegalRequired);
		Auth->OnLegalAcceptanceEnded.RemoveDynamic(this, &ThisClass::LegalEnded);
	}
	if (UCrowdyGameSession* Session = GetGameInstance()->GetSubsystem<UCrowdyGameSession>())
	{
		Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
		Session->OnAppPausedChanged.RemoveDynamic(this, &ThisClass::Paused);
	}
	Super::Deinitialize();
}

void UConstructShellLifecycleSubsystem::Login(bool bSuccess, FString Message)
{
	if (bSuccess)
	{
		UE_LOG(LogConstructShell, Log, TEXT("ConstructShell: signed in"));
	}
	else
	{
		UE_LOG(LogConstructShell,
		       Log,
		       TEXT("ConstructShell: sign-in failed: %s"),
		       *ConstructShellDiagnostics::Issue(Message));
	}
}
void UConstructShellLifecycleSubsystem::Register(bool bSuccess, FString Message)
{
	Login(bSuccess, Message);
}
void UConstructShellLifecycleSubsystem::Connected()
{
	UE_LOG(LogConstructShell, Log, TEXT("ConstructShell: connected"));
}
void UConstructShellLifecycleSubsystem::ConnectionLost()
{
	UE_LOG(LogConstructShell, Log, TEXT("ConstructShell: connection lost"));
}
void UConstructShellLifecycleSubsystem::Address(bool bSuccess, bool GateKeep)
{
	if (GateKeep)
	{
		UE_LOG(LogConstructShell, Log, TEXT("ConstructShell: gate kept"));
	}
}
void UConstructShellLifecycleSubsystem::Refreshed()
{
	UE_LOG(LogConstructShell, Log, TEXT("ConstructShell: app token refreshed"));
}
void UConstructShellLifecycleSubsystem::AccessEnded(const FString& Reason, const FString& BlockedUntil)
{
	UE_LOG(LogConstructShell,
	       Log,
	       TEXT("ConstructShell: access ended: %s until %s"),
	       *ConstructShellDiagnostics::Issue(Reason),
	       *ConstructShellDiagnostics::Deadline(BlockedUntil));
}
void UConstructShellLifecycleSubsystem::Paused(bool bPaused, const FString& Reason)
{
	UE_LOG(LogConstructShell,
	       Log,
	       TEXT("ConstructShell: app paused=%s %s"),
	       bPaused ? TEXT("true") : TEXT("false"),
	       *ConstructShellDiagnostics::Issue(Reason));
}
void UConstructShellLifecycleSubsystem::Logout(bool bSuccess)
{
	UE_LOG(LogConstructShell, Log, TEXT("ConstructShell: %s"), bSuccess ? TEXT("signed out") : TEXT("sign-out failed"));
}

void UConstructShellLifecycleSubsystem::LegalRequired()
{
	for (ULocalPlayer* Local : GetGameInstance()->GetLocalPlayers())
	{
		if (auto* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>())
		{
			Gate->SetModal(TEXT("CrowdyLegalConsent"), true);
		}
	}
}
void UConstructShellLifecycleSubsystem::LegalEnded()
{
	for (ULocalPlayer* Local : GetGameInstance()->GetLocalPlayers())
	{
		if (auto* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>())
		{
			Gate->SetModal(TEXT("CrowdyLegalConsent"), false);
		}
	}
}
