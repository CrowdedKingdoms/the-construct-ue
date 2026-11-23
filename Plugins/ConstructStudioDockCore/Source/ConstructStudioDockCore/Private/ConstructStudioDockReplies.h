#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDockTypes.h"
#include "Dom/JsonObject.h"
#include "Subsystem/CrowdyClaims.h"
#include "Subsystem/CrowdyModScriptTypes.h"
#include "Subsystem/CrowdyMods.h"
#include "Subsystem/CrowdyStudioProjects.h"

/** The dock's answers to the agent's requests, in the shapes the harness reads. */
namespace ConstructStudioDockReplies
{
	/** The most build log one answer carries; the end is kept. */
	inline constexpr int32 MaxBuildLogChars = 12000;

	inline constexpr const TCHAR* NoProjectText = TEXT("No project is open in Crowdy Studio.");
	inline constexpr const TCHAR* NotReachableText = TEXT("The Studio cannot be reached from the game right now.");
	inline constexpr const TCHAR* NotReadyText = TEXT("The Studio dock is not ready yet.");
	inline constexpr const TCHAR* NothingToObserveText = TEXT("Nothing to observe right now: the player is not in the world.");

	/** Ok only when the run went to its end and every half it has reached Deployed or Running; a crate that does not compile is not. */
	bool RunIsOk(const FCrowdyModRun& Run);

	/** The run's CLIENT half when it was deployed, else null. */
	const FCrowdyModRunTarget* DeployedClient(const FCrowdyModRun& Run);

	/** The run's SERVER half when it is deployed but not reported running, else null. */
	const FCrowdyModRunTarget* DeployedServer(const FCrowdyModRun& Run);

	const TCHAR* TargetName(ECrowdyStudioTarget Target);

	/** A phase as the Studio names it. */
	const TCHAR* PhaseName(ECrowdyModRunPhase Phase);

	/** {runtime: [...]}: each half's target, phase, module, running and saved revision, and message. */
	TSharedRef<FJsonObject> RuntimeReply(const TArray<FCrowdyModRunTarget>& Targets);

	/**
	 * The harness's BuildResult: ok (RunIsOk), mode, summary, diagnostics, build log and runtime. A run that did not go to
	 * its end keeps its own message as the summary; one that did is worded by Settle.
	 */
	TSharedRef<FJsonObject> BuildResult(const FCrowdyModRun& Run);

	/** A BuildResult's ok. */
	bool IsOk(const FJsonObject& Result);

	/** Whether a BuildResult reports the half running. */
	bool IsHalfRunning(const FJsonObject& Result, ECrowdyStudioTarget Target);

	/** The SERVER half as its mod says: running when switched on and not blocked, else the result is not ok and says why. */
	void ApplyServerMod(FJsonObject& Result, const FCrowdyMod& Mod);

	/** Reports a BuildResult's CLIENT half as running in this game. */
	void MarkClientRunning(FJsonObject& Result);

	/** Reports a BuildResult's CLIENT half as not started in this game, for Why; only a half heard failing makes the result not ok. */
	void MarkClientNotStarted(FJsonObject& Result, const FString& Why, bool bFailed);

	/** Words a finished run's summary from ok and each half's phase; it says running only when every half is RUNNING. */
	void Settle(FJsonObject& Result);

	/** A screenshot with its bytes as bytesBase64, which the dock's page turns into binary. */
	TSharedRef<FJsonObject> Screenshot(const FConstructStudioCapture& Capture, int32 Number);

	TSharedRef<FJsonObject> ProjectSummary(const FCrowdyStudioProject& Project);

	/** page.project's payload for the project now open. */
	TSharedRef<FJsonObject> ProjectEvent(const FCrowdyStudioProject& Project);

	/** {project: ...} for an opened or created project. */
	TSharedRef<FJsonObject> ProjectReply(const FCrowdyStudioProject& Project);

	/** {projects, currentProjectId, more}: more says the platform holds projects past this list. */
	TSharedRef<FJsonObject> ProjectList(const TArray<FCrowdyStudioProject>& Projects, const FString& CurrentProjectId, bool bMore);

	/** The last Limit of a mod's lines as "time module level: text", oldest first, and whether more were held. */
	TSharedRef<FJsonObject> ModLog(const TArray<FCrowdyModLogLine>& Lines, const FString& Module, int32 Limit);

	/** The same for a Mod Script's lines, which count the lines it dropped as more held. */
	TSharedRef<FJsonObject> ScriptLog(const FCrowdyModScriptLog& Log, int32 Limit);

	TSharedRef<FJsonObject> NoLines();

	/** {grid: ...}: the claim's chunk as the grid's box, whether the player owns it, its channels, and no sessions. */
	TSharedRef<FJsonObject> GridReply(int64 AppId, const FCrowdyChunkClaimInfo& Claim, const TArray<FCrowdyChannel>& Channels);

	/** {grid: null}. */
	TSharedRef<FJsonObject> NoGridReply();

	/** studio.projectCreate's template: server, client, or both halves for anything else. */
	ECrowdyStudioProjectKind KindForTemplate(const FString& Template);

	/** Params' field Name when it is a real JSON string, cut to MaxChars. */
	bool ParamString(const TSharedPtr<FJsonObject>& Params, const TCHAR* Name, int32 MaxChars, FString& Out);

	/** A log request's limit: a whole number from 1 to 400, else Fallback. */
	int32 ParamLimit(const TSharedPtr<FJsonObject>& Params, int32 Fallback);

	/** Cargo.toml, README.md, or src/ then letters, digits, _, - and / in non-empty segments, ending in .rs: a file the SDK reads from a project. */
	bool IsReadablePath(const FString& Path);

	/** Text with its control, format and bidirectional characters removed, so what a prompt shows is what it says. */
	FString PlainText(const FString& Text);

	FString Iso8601(int64 UnixMs);
}
