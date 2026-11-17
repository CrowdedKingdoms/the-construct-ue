#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDockTypes.h"
#include "Subsystem/CrowdyMods.h"
#include "Subsystem/CrowdyStudioAgent.h"
#include "Subsystem/CrowdyStudioProjects.h"

class UGameInstance;

/** The SDK calls one boot of the dock makes. Each answers once, later, on the game thread; tests answer them themselves. */
struct FConstructStudioDockPlatform
{
	TFunction<void(TFunction<void(const FCrowdyStudioAgentBoot&)>)> GetAgentBoot;
	TFunction<void(TFunction<void(const FCrowdyStudioConsent&)>)> GetProviderConsent;
	TFunction<void(TFunction<void(const FCrowdyStudioConsent&)>)> RecordProviderConsent;
	TFunction<FText()> GetConsentNotice;
	TFunction<void(TFunction<void(const FCrowdyStudioModelUsage&)>)> GetModelUsage;
	TFunction<void(ECrowdyStudioRunMode, TFunction<void(const FCrowdyStudioLiveModding&)>)> AuthorizeLiveModding;
	TFunction<void(int64 GridId, const FString& ProjectId, const FString& Revision, ECrowdyStudioRunMode Mode, const FString& ServerModule,
		const FString& ClientModule, TFunction<void(const FCrowdyModRun&)>)> RunProject;
	TFunction<void(TFunction<void(const FCrowdyStudioProjectList&)>)> ListProjects;
	TFunction<void(const FString& ProjectId, TFunction<void(const FCrowdyStudioProjectInfo&)>)> GetProject;

	/** The calls on Game's subsystems, held weakly; a call whose subsystem is gone never answers. */
	static TSharedRef<FConstructStudioDockPlatform> Live(UGameInstance& Game);

	/** The platform's usage as the dock reports it: the payer in lowercase, the charge and limit in US dollars. */
	static FConstructStudioUsage ToUsage(const FCrowdyStudioModelUsage& Usage);

	/** Millionths of a US dollar as a decimal string with at least two places. */
	static FString DecimalUsd(int64 MicroUsd);
};
