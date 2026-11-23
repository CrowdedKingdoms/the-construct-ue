#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDockTypes.h"
#include "Subsystem/CrowdyClaims.h"
#include "Subsystem/CrowdyModScriptTypes.h"
#include "Subsystem/CrowdyMods.h"
#include "Subsystem/CrowdyStudioAgent.h"
#include "Subsystem/CrowdyStudioProjects.h"

class UGameInstance;
class UWorld;

/**
 * The SDK calls one boot of the dock makes. Each answers once on the game thread: later, or at once as failed when its
 * subsystem is gone. Tests answer them themselves.
 */
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
	TFunction<void(const FString& Name, ECrowdyStudioProjectKind Kind, TFunction<void(const FCrowdyStudioProjectInfo&)>)> CreateProject;
	TFunction<void(int64 GridId, const FString& ProjectId, TFunction<void(const FCrowdyModProjectStatus&)>)> GetProjectStatus;
	TFunction<void(int64 GridId, const FString& ModName, int32 Limit, TFunction<void(const FCrowdyModLogs&)>)> GetModLogs;
	TFunction<void(int64 GridId, const FString& ModId, TFunction<void(const FCrowdyModScriptLog&)>)> GetScriptLog;
	TFunction<void(const FInt64Vector& Chunk, TFunction<void(const FCrowdyClaimSearch&)>)> FindClaimAt;
	TFunction<void(int64 GridId, TFunction<void(const FCrowdyGridChannels&)>)> ListGridChannels;
	TFunction<void(int64 GridId, TFunction<void(const FCrowdyModList&)>)> ListMods;
	TFunction<void(int64 GridId, TFunction<void(const FCrowdyModScriptStatusList&)>)> GetScriptStatus;

	/** The Mod Scripts running in this game now; none when the world has no Mod Scripts. */
	TFunction<TArray<FCrowdyModScript>()> GetRunningScripts;

	/** The calls on Game's subsystems and World's Mod Scripts, held weakly; a call whose subsystem is gone answers Failed at once. */
	static TSharedRef<FConstructStudioDockPlatform> Live(UGameInstance& Game, UWorld* World);

	/** The platform's usage as the dock reports it: the payer in lowercase, the charge and limit in US dollars. */
	static FConstructStudioUsage ToUsage(const FCrowdyStudioModelUsage& Usage);

	/** Millionths of a US dollar as a decimal string with at least two places. */
	static FString DecimalUsd(int64 MicroUsd);
};
