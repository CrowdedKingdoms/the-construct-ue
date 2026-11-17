#include "ConstructStudioDockPlatform.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Subsystem/CrowdyModScripts.h"

namespace ConstructStudioDockPlatformParts
{
	const TCHAR* const GoneText = TEXT("The game's Crowdy services are not running, so the call was not made.");

	/** Answers OnDone at once as failed, for a call whose subsystem is gone, so a request waiting on it is still answered. */
	template <typename AnswerType>
	void AnswerGone(const TFunction<void(const AnswerType&)>& OnDone)
	{
		AnswerType Gone;
		Gone.Message = GoneText;
		OnDone(Gone);
	}
}

TSharedRef<FConstructStudioDockPlatform> FConstructStudioDockPlatform::Live(UGameInstance& Game, UWorld* World)
{
	using ConstructStudioDockPlatformParts::AnswerGone;
	const TWeakObjectPtr<UCrowdyStudioAgent> Agent = Game.GetSubsystem<UCrowdyStudioAgent>();
	const TWeakObjectPtr<UCrowdyStudioProjects> Projects = Game.GetSubsystem<UCrowdyStudioProjects>();
	const TWeakObjectPtr<UCrowdyMods> Mods = Game.GetSubsystem<UCrowdyMods>();
	const TWeakObjectPtr<UCrowdyClaims> Claims = Game.GetSubsystem<UCrowdyClaims>();
	const TWeakObjectPtr<UCrowdyModScripts> Scripts = World ? World->GetSubsystem<UCrowdyModScripts>() : nullptr;
	const TSharedRef<FConstructStudioDockPlatform> Calls = MakeShared<FConstructStudioDockPlatform>();
	Calls->GetAgentBoot = [Agent](TFunction<void(const FCrowdyStudioAgentBoot&)> OnDone)
	{
		UCrowdyStudioAgent* Subsystem = Agent.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetAgentBoot(MoveTemp(OnDone));
	};
	Calls->GetProviderConsent = [Agent](TFunction<void(const FCrowdyStudioConsent&)> OnDone)
	{
		UCrowdyStudioAgent* Subsystem = Agent.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetProviderConsent(MoveTemp(OnDone));
	};
	Calls->RecordProviderConsent = [Agent](TFunction<void(const FCrowdyStudioConsent&)> OnDone)
	{
		UCrowdyStudioAgent* Subsystem = Agent.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->RecordProviderConsent(MoveTemp(OnDone), true);
	};
	Calls->GetConsentNotice = [Agent]()
	{
		const UCrowdyStudioAgent* Subsystem = Agent.Get();
		return Subsystem ? Subsystem->GetConsentNotice() : FText::GetEmpty();
	};
	Calls->GetModelUsage = [Agent](TFunction<void(const FCrowdyStudioModelUsage&)> OnDone)
	{
		UCrowdyStudioAgent* Subsystem = Agent.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetModelUsage(MoveTemp(OnDone));
	};
	Calls->AuthorizeLiveModding = [Agent](ECrowdyStudioRunMode Mode, TFunction<void(const FCrowdyStudioLiveModding&)> OnDone)
	{
		UCrowdyStudioAgent* Subsystem = Agent.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->AuthorizeLiveModding(Mode, MoveTemp(OnDone));
	};
	Calls->RunProject = [Mods](int64 GridId, const FString& ProjectId, const FString& Revision, ECrowdyStudioRunMode Mode, const FString& ServerModule,
		const FString& ClientModule, TFunction<void(const FCrowdyModRun&)> OnDone)
	{
		UCrowdyMods* Subsystem = Mods.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->RunProject(GridId, ProjectId, Revision, Mode, ServerModule, ClientModule, MoveTemp(OnDone));
	};
	Calls->ListProjects = [Projects](TFunction<void(const FCrowdyStudioProjectList&)> OnDone)
	{
		UCrowdyStudioProjects* Subsystem = Projects.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->ListProjects(MoveTemp(OnDone));
	};
	Calls->GetProject = [Projects](const FString& ProjectId, TFunction<void(const FCrowdyStudioProjectInfo&)> OnDone)
	{
		UCrowdyStudioProjects* Subsystem = Projects.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetProject(ProjectId, MoveTemp(OnDone));
	};
	Calls->CreateProject = [Projects](const FString& Name, ECrowdyStudioProjectKind Kind, TFunction<void(const FCrowdyStudioProjectInfo&)> OnDone)
	{
		UCrowdyStudioProjects* Subsystem = Projects.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->CreateProject(Name, Kind, MoveTemp(OnDone));
	};
	Calls->GetProjectStatus = [Mods](int64 GridId, const FString& ProjectId, TFunction<void(const FCrowdyModProjectStatus&)> OnDone)
	{
		UCrowdyMods* Subsystem = Mods.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetProjectStatus(GridId, ProjectId, MoveTemp(OnDone));
	};
	Calls->GetModLogs = [Mods](int64 GridId, const FString& ModName, int32 Limit, TFunction<void(const FCrowdyModLogs&)> OnDone)
	{
		UCrowdyMods* Subsystem = Mods.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetModLogs(GridId, ModName, Limit, MoveTemp(OnDone));
	};
	Calls->ListMods = [Mods](int64 GridId, TFunction<void(const FCrowdyModList&)> OnDone)
	{
		UCrowdyMods* Subsystem = Mods.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->ListMods(GridId, MoveTemp(OnDone));
	};
	Calls->GetScriptLog = [Scripts](int64 GridId, const FString& ModId, TFunction<void(const FCrowdyModScriptLog&)> OnDone)
	{
		UCrowdyModScripts* Subsystem = Scripts.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetScriptLog(GridId, ModId, MoveTemp(OnDone));
	};
	Calls->GetScriptStatus = [Scripts](int64 GridId, TFunction<void(const FCrowdyModScriptStatusList&)> OnDone)
	{
		UCrowdyModScripts* Subsystem = Scripts.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->GetScriptStatus(GridId, MoveTemp(OnDone));
	};
	Calls->GetRunningScripts = [Scripts]()
	{
		const UCrowdyModScripts* Subsystem = Scripts.Get();
		return Subsystem ? Subsystem->GetRunning() : TArray<FCrowdyModScript>();
	};
	Calls->FindClaimAt = [Claims](const FInt64Vector& Chunk, TFunction<void(const FCrowdyClaimSearch&)> OnDone)
	{
		UCrowdyClaims* Subsystem = Claims.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->FindClaims(Chunk.X, Chunk.Y, Chunk.Z, Chunk.X, Chunk.Y, Chunk.Z, MoveTemp(OnDone));
	};
	Calls->ListGridChannels = [Claims](int64 GridId, TFunction<void(const FCrowdyGridChannels&)> OnDone)
	{
		UCrowdyClaims* Subsystem = Claims.Get();
		if (!Subsystem)
		{
			AnswerGone(OnDone);
			return;
		}
		Subsystem->ListGridChannels(GridId, MoveTemp(OnDone));
	};
	return Calls;
}

FConstructStudioUsage FConstructStudioDockPlatform::ToUsage(const FCrowdyStudioModelUsage& Usage)
{
	FConstructStudioUsage Shown;
	Shown.Payer = StaticEnum<ECrowdyStudioPayer>()->GetNameStringByValue(static_cast<int64>(Usage.Payer)).ToLower();
	Shown.TodayRequests = Usage.TodayRequests;
	Shown.TodayCharge = DecimalUsd(Usage.TodayChargeMicroUsd);
	Shown.DayLimit = DecimalUsd(Usage.DayLimitMicroUsd);
	return Shown;
}

FString FConstructStudioDockPlatform::DecimalUsd(int64 MicroUsd)
{
	const int64 Micro = FMath::Max<int64>(MicroUsd, 0);
	FString Fraction = FString::Printf(TEXT("%06lld"), Micro % 1000000);
	while (Fraction.Len() > 2 && Fraction.EndsWith(TEXT("0")))
	{
		Fraction.LeftChopInline(1);
	}
	return FString::Printf(TEXT("%lld.%s"), Micro / 1000000, *Fraction);
}
