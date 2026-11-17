#include "ConstructStudioDockPlatform.h"

#include "Engine/GameInstance.h"

TSharedRef<FConstructStudioDockPlatform> FConstructStudioDockPlatform::Live(UGameInstance& Game)
{
	const TWeakObjectPtr<UCrowdyStudioAgent> Agent = Game.GetSubsystem<UCrowdyStudioAgent>();
	const TWeakObjectPtr<UCrowdyStudioProjects> Projects = Game.GetSubsystem<UCrowdyStudioProjects>();
	const TWeakObjectPtr<UCrowdyMods> Mods = Game.GetSubsystem<UCrowdyMods>();
	const TSharedRef<FConstructStudioDockPlatform> Calls = MakeShared<FConstructStudioDockPlatform>();
	Calls->GetAgentBoot = [Agent](TFunction<void(const FCrowdyStudioAgentBoot&)> OnDone)
	{
		if (UCrowdyStudioAgent* Subsystem = Agent.Get())
		{
			Subsystem->GetAgentBoot(MoveTemp(OnDone));
		}
	};
	Calls->GetProviderConsent = [Agent](TFunction<void(const FCrowdyStudioConsent&)> OnDone)
	{
		if (UCrowdyStudioAgent* Subsystem = Agent.Get())
		{
			Subsystem->GetProviderConsent(MoveTemp(OnDone));
		}
	};
	Calls->RecordProviderConsent = [Agent](TFunction<void(const FCrowdyStudioConsent&)> OnDone)
	{
		if (UCrowdyStudioAgent* Subsystem = Agent.Get())
		{
			Subsystem->RecordProviderConsent(MoveTemp(OnDone), true);
		}
	};
	Calls->GetConsentNotice = [Agent]()
	{
		const UCrowdyStudioAgent* Subsystem = Agent.Get();
		return Subsystem ? Subsystem->GetConsentNotice() : FText::GetEmpty();
	};
	Calls->GetModelUsage = [Agent](TFunction<void(const FCrowdyStudioModelUsage&)> OnDone)
	{
		if (UCrowdyStudioAgent* Subsystem = Agent.Get())
		{
			Subsystem->GetModelUsage(MoveTemp(OnDone));
		}
	};
	Calls->AuthorizeLiveModding = [Agent](ECrowdyStudioRunMode Mode, TFunction<void(const FCrowdyStudioLiveModding&)> OnDone)
	{
		if (UCrowdyStudioAgent* Subsystem = Agent.Get())
		{
			Subsystem->AuthorizeLiveModding(Mode, MoveTemp(OnDone));
		}
	};
	Calls->RunProject = [Mods](int64 GridId, const FString& ProjectId, const FString& Revision, ECrowdyStudioRunMode Mode, const FString& ServerModule,
		const FString& ClientModule, TFunction<void(const FCrowdyModRun&)> OnDone)
	{
		if (UCrowdyMods* Subsystem = Mods.Get())
		{
			Subsystem->RunProject(GridId, ProjectId, Revision, Mode, ServerModule, ClientModule, MoveTemp(OnDone));
		}
	};
	Calls->ListProjects = [Projects](TFunction<void(const FCrowdyStudioProjectList&)> OnDone)
	{
		if (UCrowdyStudioProjects* Subsystem = Projects.Get())
		{
			Subsystem->ListProjects(MoveTemp(OnDone));
		}
	};
	Calls->GetProject = [Projects](const FString& ProjectId, TFunction<void(const FCrowdyStudioProjectInfo&)> OnDone)
	{
		if (UCrowdyStudioProjects* Subsystem = Projects.Get())
		{
			Subsystem->GetProject(ProjectId, MoveTemp(OnDone));
		}
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
