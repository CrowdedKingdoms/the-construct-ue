#include "ConstructLayer1Check.h"

#include "ConstructHarnessLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Queries/Data/Channels/Types/FCrowdyChannelError.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyChannels.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
	const TCHAR* const CheckMapName = TEXT("L_Layer1Check");

	// The expected ids arrive in the environment, never on the command line, which the engine writes into the log.
	bool ReadExpectedId(const TCHAR* Variable, int64& OutId)
	{
		const FString Text = FPlatformMisc::GetEnvironmentVariable(Variable);
		return LexTryParseString(OutId, *Text) && OutId > 0;
	}

	const TCHAR* MatchWord(bool bMatch)
	{
		return bMatch ? TEXT("match") : TEXT("mismatch");
	}
}

bool UConstructLayer1CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->WorldType == EWorldType::Game;
}

void UConstructLayer1CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetGameInstance())
	{
		return;
	}
	if (InWorld.GetMapName() != CheckMapName)
	{
		// Started for the check but on another map, as when the build did not cook L_Layer1Check.
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("CONSTRUCT_EXPECT_APP_ID")).IsEmpty())
		{
			Finish(false, FString::Printf(TEXT("the build loaded %s, not %s"), *InWorld.GetMapName(), CheckMapName));
		}
		return;
	}

	StartedAt = FPlatformTime::Seconds();
	double TimeoutMinutes = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("Layer1CheckTimeoutMinutes="), TimeoutMinutes) && TimeoutMinutes > 0.0)
	{
		TimeoutSeconds = TimeoutMinutes * 60.0;
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 1.0f);

	if (!ReadExpectedId(TEXT("CONSTRUCT_EXPECT_APP_ID"), ExpectedAppId) || !ReadExpectedId(TEXT("CONSTRUCT_EXPECT_USER_ID"), ExpectedUserId))
	{
		Finish(false, TEXT("CONSTRUCT_EXPECT_APP_ID or CONSTRUCT_EXPECT_USER_ID is missing; start the check with Tools/Layer1Check"));
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: started, waiting for the shell to sign in and connect"));
}

void UConstructLayer1CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0.
	if (TickerHandle.IsValid() && Stage != EStage::Done)
	{
		Finish(false, TEXT("the check's world ended before a verdict"));
	}
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer1CheckSubsystem::Tick(float DeltaTime)
{
	if (Stage == EStage::Done)
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds)
	{
		Finish(false, FString::Printf(TEXT("timed out after %.0f minutes in stage %d"), TimeoutSeconds / 60.0, static_cast<int32>(Stage)));
		return false;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UCrowdyAuthentication* Auth = GameInstance ? GameInstance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = GameInstance ? GameInstance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	if (!Auth || !SDK)
	{
		Finish(false, TEXT("the Crowdy SDK subsystems are missing"));
		return false;
	}

	if (Stage == EStage::WaitEntry && Auth->IsSignedIn() && SDK->GetUDPConnectionState() == EUDPConnectionState::Connected)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: stimulus landed: signed in and connected"));
		Auth->OnAppTokenRefreshed.AddUniqueDynamic(this, &ThisClass::HandleRefreshed);
		Stage = EStage::WaitRefresh;
	}
	if (Stage == EStage::WaitLateCall && Now - FirstRefreshAt >= LateCallDelaySeconds)
	{
		MakeLateCall();
	}
	return true;
}

void UConstructLayer1CheckSubsystem::HandleRefreshed()
{
	if (Stage != EStage::WaitRefresh)
	{
		return;
	}
	FirstRefreshAt = FPlatformTime::Seconds();
	Stage = EStage::WaitLateCall;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: app token refreshed; the late call follows in %.0f s"), LateCallDelaySeconds);
}

void UConstructLayer1CheckSubsystem::MakeLateCall()
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UCrowdyChannels* Channels = GameInstance ? GameInstance->GetSubsystem<UCrowdyChannels>() : nullptr;
	if (!Channels)
	{
		Finish(false, TEXT("the Crowdy Channels subsystem is missing"));
		return;
	}
	Stage = EStage::CallingLate;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: late call: Get My Channels"));
	FOnMyChannelsSuccess OnSuccess;
	OnSuccess.BindDynamic(this, &ThisClass::HandleLateCallSucceeded);
	FOnChannelError OnError;
	OnError.BindDynamic(this, &ThisClass::HandleLateCallFailed);
	Channels->GetMyChannels(OnSuccess, OnError);
}

void UConstructLayer1CheckSubsystem::HandleLateCallSucceeded(const TArray<FCrowdyChannelMembership>& Memberships)
{
	if (Stage != EStage::CallingLate)
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: late call: ok"));
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyGameSession* Session = GameInstance ? GameInstance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	const bool bAppMatch = Session && Session->GetAppID() == ExpectedAppId;
	const bool bUserMatch = Session && Session->GetUserID() == ExpectedUserId;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: app id: %s"), MatchWord(bAppMatch));
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: user id: %s"), MatchWord(bUserMatch));
	Finish(bAppMatch && bUserMatch, bAppMatch && bUserMatch ? FString() : FString(TEXT("an id does not match the Node read")));
}

void UConstructLayer1CheckSubsystem::HandleLateCallFailed(FCrowdyChannelError Error, FString Message)
{
	if (Stage == EStage::CallingLate)
	{
		Finish(false, TEXT("late call: failed"));
	}
}

void UConstructLayer1CheckSubsystem::Finish(bool bPass, const FString& Why)
{
	if (Stage == EStage::Done)
	{
		return;
	}
	Stage = EStage::Done;
	if (bPass)
	{
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer1Check: PASS"));
	}
	else
	{
		UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer1Check: FAIL: %s"), *Why);
	}
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, bPass ? 0 : 1);
}
