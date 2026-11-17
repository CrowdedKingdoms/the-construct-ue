#pragma once

#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "ConstructStudioDockHost.h"
#include "ConstructStudioDockTypes.h"
#include "ConstructStudioObservation.h"
#include "ConstructStudioDock.generated.h"

class FConstructStudioDockChannel;
class FJsonObject;
class SConstructStudioDockPlaceholder;
class SWebBrowser;
class UConstructStudioCaptureRequest;
class UConstructStudioDockBridge;
class UConstructStudioScriptWatch;
class UCrowdyStudioAgent;
enum class EConstructStudioDockRefusal : uint8;
enum class ECrowdyModScriptStop : uint8;
enum class ECrowdyStudioResult : uint8;
struct FConstructStudioDockPlatform;
struct FCrowdyClaimSearch;
struct FCrowdyModList;
struct FCrowdyModProjectStatus;
struct FCrowdyModRun;
struct FCrowdyModScriptStatusList;
struct FCrowdyStudioAgentBoot;
struct FCrowdyStudioConsent;
struct FCrowdyStudioLiveModding;
struct FCrowdyStudioModelUsage;
struct FCrowdyStudioProject;
struct FCrowdyStudioProjectInfo;
struct FWebNavigationRequest;

/**
 * The Studio dock: the platform's agent beside the game view, in the slot the panel gives it. Where the agent's page
 * cannot run (no web browser in this process, or the page is not staged), a boot ends in Failed with NotBuiltYet.
 */
UCLASS(meta=(DisplayName="Studio Dock"))
class CONSTRUCTSTUDIODOCKCORE_API UConstructStudioDock : public UWidget
{
	GENERATED_BODY()

public:
	/** Seconds an unanswered deploy prompt waits before it declines itself. */
	static constexpr double PromptSeconds = 60.0;

	/** Seconds a boot may stay in Booting before it fails. */
	static constexpr double BootSeconds = 60.0;

	/** Seconds between reads of the player's model usage while the dock is Ready. */
	static constexpr double UsageSeconds = 30.0;

	/** Seconds after the agent's last saved file before the dock reads the project back. */
	static constexpr double SaveQuietSeconds = 1.5;

	/** Seconds the agent waits for a draft test or deploy answer; a run that takes longer finishes, and its answer is not sent. */
	static constexpr double RequestWaitSeconds = 240.0;

	/** Seconds a run's answer waits for its deployed CLIENT half to start in this game before reporting it not started. */
	static constexpr double ClientStartSeconds = 20.0;

	/** The most agent requests answered later that the dock holds at once. */
	static constexpr int32 MaxHeldRequests = 32;

	/** Seconds game.observe reuses its last claim lookup while the player stays in that chunk and the dock on that grid. */
	static constexpr double ClaimReuseSeconds = 5.0;

	/** Boots the dock on a grid and returns None, or refuses at once with the reason and the state unchanged. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	EConstructStudioDockReason Open(int64 GridId, const FText& GridLabel, const FString& ProjectId = TEXT(""));

	/** Returns the dock to Closed from any state, withdrawing a pending prompt. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	void Close();

	/** Moves the dock to the grid the player now stands in; a different grid withdraws a pending prompt. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	void SetGrid(int64 GridId, const FText& GridLabel);

	UFUNCTION(BlueprintPure, Category="Construct|Studio Dock")
	EConstructStudioDockState GetState() const;

	/** The reason the last state change gave; Failed keeps its reason until Close. */
	UFUNCTION(BlueprintPure, Category="Construct|Studio Dock")
	EConstructStudioDockReason GetReason() const;

	/** The text to show for GetReason, including the agent's own message when it failed with one. */
	UFUNCTION(BlueprintPure, Category="Construct|Studio Dock")
	FText GetReasonText() const;

	UFUNCTION(BlueprintPure, Category="Construct|Studio Dock")
	static FText DescribeReason(EConstructStudioDockReason Why);

	/** The platform's provider notice to show in NeedsConsent; empty until the platform supplies it. */
	UFUNCTION(BlueprintPure, Category="Construct|Studio Dock")
	FText GetConsentNotice() const;

	/** Records the player's consent in NeedsConsent; the boot carries on. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	void GiveProviderConsent();

	/** Answers the pending prompt; Run is refused, and the prompt withdrawn, when its grid, project or revision changed. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	void AnswerDeployPrompt(int64 PromptId, bool bRun);

	/** Opens the platform's wallet page in the player's own browser. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	void OpenWallet();

	/** Sends the player's own screenshot of the game view to the agent. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	void ShareScreenshot();

	/** The host that answers CaptureView, held weakly; set it before Open. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	void SetHost(TScriptInterface<IConstructStudioDockHost> Host);

	/** Answers a CaptureView call from Blueprint. */
	UFUNCTION(BlueprintCallable, Category="Construct|Studio Dock")
	static void FinishCapture(const FOnConstructStudioCaptured& OnDone, const FConstructStudioCapture& Capture);

	UPROPERTY(BlueprintAssignable, Category="Construct|Studio Dock")
	FOnConstructStudioDockStateChanged OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category="Construct|Studio Dock")
	FOnConstructStudioDeployPrompt OnDeployPrompt;

	/** The prompt was withdrawn or declined itself; close it. */
	UPROPERTY(BlueprintAssignable, Category="Construct|Studio Dock")
	FOnConstructStudioDeployPromptWithdrawn OnDeployPromptWithdrawn;

	UPROPERTY(BlueprintAssignable, Category="Construct|Studio Dock")
	FOnConstructStudioUsageChanged OnUsageChanged;

	UPROPERTY(BlueprintAssignable, Category="Construct|Studio Dock")
	FOnConstructStudioWarning OnWarning;

	/** The dock's keyboard focus changed. */
	UPROPERTY(BlueprintAssignable, Category="Construct|Studio Dock")
	FOnConstructStudioFocusChanged OnFocusChanged;

	/** Escape was pressed inside the dock; the game decides whether to close it. */
	UPROPERTY(BlueprintAssignable, Category="Construct|Studio Dock")
	FOnConstructStudioCloseRequested OnCloseRequested;

#if !UE_BUILD_SHIPPING
	/** Queues Text as the player's next prompt to the agent; false, and nothing sent, unless the dock is Ready. For unattended checks. */
	bool SendPrompt(const FString& Text);

	/** Whether a draft test or deploy the player approved is being authorized, run or answered now. For unattended checks. */
	bool IsRunInFlightForChecks() const;
#endif

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	friend class UConstructStudioCaptureRequest;
	friend class UConstructStudioDockBridge;
	friend class UConstructStudioScriptWatch;
	friend struct FConstructStudioDockTestAccess;
	// The stand-in replays recorded agent traffic through Outbound and the Receive functions; nothing else reaches them.
	friend class UStandInC11;

	/** What the dock tells the agent's page and the platform: a message name and a short detail. */
	static TMulticastDelegate<void(UConstructStudioDock&, FName, const FString&)> Outbound;
	static TWeakObjectPtr<UConstructStudioDock> OpenDock;
	static int64 LastPromptId;

	void ClaimBoot();
	void ReceiveBootOutcome(int64 InGeneration, EConstructStudioDockState NewState, EConstructStudioDockReason NewReason, const FText& Text);
	void ReceiveProject(int64 InGeneration, const FString& InProjectId, const FString& Name, const FString& InRevision, bool bInReachesVisitors);
	void ReceiveFileChanged(int64 InGeneration, const FString& Path, const FString& InRevision);
	/** A deploy request; RequestId is the agent's, answered with the run's result, or empty when nothing waits for one. */
	void ReceiveDeployRequest(int64 InGeneration, EConstructStudioDeployKind Kind, const FString& RequestId = FString());
	void ReceiveRunAnswer(int64 InGeneration, bool bOk);
	void ReceiveScreenshotRequest(int64 InGeneration, const FString& Label, const FString& RequestId = FString());
	void ReceiveUsage(int64 InGeneration, const FConstructStudioUsage& Usage);
	void ReceiveWarning(int64 InGeneration, const FText& Text);
	void ReceiveFocus(int64 InGeneration, bool bInHasFocus);
	void ReceiveCloseRequest(int64 InGeneration);
	void CaptureAnswered(UConstructStudioCaptureRequest& Request, const FConstructStudioCapture& Capture);

	bool IsCurrent(int64 InGeneration) const;
	bool Advance(float DeltaSeconds);
	void SetState(EConstructStudioDockState NewState, EConstructStudioDockReason NewReason, const FText& Text = FText::GetEmpty());
	void Fail(EConstructStudioDockReason Why, const FText& Text);
	void CloseFor(EConstructStudioDockReason Why);
	void Teardown();
	void WithdrawPrompt(const TCHAR* Answer);
	void RefuseCaptures(const TCHAR* Why);
	void RequestCapture(const FString& Label, bool bShared, const FString& RequestId = FString(), bool bForRun = false);
	void RaisePrompt(EConstructStudioDeployKind Kind);
	void Send(FName Message, const FString& Detail = FString());
	int64 SignedInUser(bool& bOutAccessEnded) const;
	void WorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources);
	void SlateWidgetGone();
	bool PlaceholderHasFocus() const;
	void ClearPlaceholderFocus();
	FText PlaceholderText() const;

	bool CloseIfPlayerChanged();
	bool StartLiveBoot();
	TSharedPtr<FConstructStudioDockPlatform> LivePlatform();
	UCrowdyStudioAgent* LiveAgent() const;
	void UnbindAgent();
	void ReceiveAgentBoot(int64 InGeneration, const FCrowdyStudioAgentBoot& Boot);
	void ReceiveConsent(int64 InGeneration, const FCrowdyStudioConsent& Consent);
	void ReadProject();
	void ReceiveStudioProject(int64 InGeneration, ECrowdyStudioResult Result, const FString& Message, const FCrowdyStudioProject& Project);
	void ShowPage();
	void StartRun(const FConstructStudioDeployPrompt& Approved, const FString& ServerModule, const FString& ClientModule);
	void ReceiveLiveModding(int64 InGeneration, const FConstructStudioDeployPrompt& Approved, const FString& ServerModule, const FString& ClientModule, const FCrowdyStudioLiveModding& Verdict);
	void ReceiveModRun(int64 InGeneration, const FCrowdyModRun& Run);
	void RequestUsage();
	void ReceiveModelUsage(int64 InGeneration, const FCrowdyStudioModelUsage& Usage);
	void ReceiveWorkerFileChanged(int64 InGeneration, const FJsonObject& Payload);

	/** The grid the dock is on, as the agent reads it. */
	TSharedRef<FJsonObject> GridContext() const;

	/** Sends page.context with the grid the dock is on. */
	void TellGridContext(int64 InGeneration);

	void AgentTokenChanged(int64 UserId, const FString& Token);
	UFUNCTION()
	void AgentBootChanged();
	FString AgentToken() const;

	FString BridgeBoot(const UConstructStudioDockBridge& Caller);
	FString BridgeSend(const UConstructStudioDockBridge& Caller, const FString& Frame);
	void ReceivePageFrame(const FString& Json);
	void ReceivePageNotice(int64 InGeneration, const FString& Notice, const TSharedPtr<FJsonObject>& Payload);
	void ReceiveWorkerEvent(int64 InGeneration, const FString& Event, const TSharedPtr<FJsonObject>& Payload);

	/** One request from the agent, by method and id, with its params when they are an object; answer it once. */
	void ReceiveAgentRequest(int64 InGeneration, const FString& Method, const FString& Id, const TSharedPtr<FJsonObject>& Params);

	/** Answers a request; false, and nothing sent, when InGeneration is not the current boot or the page has not joined. */
	bool ReplyToAgent(int64 InGeneration, const FString& Id, const TSharedRef<FJsonObject>& Result);
	bool RefuseToAgent(int64 InGeneration, const FString& Id, EConstructStudioDockRefusal Why, const FString& Message = FString());

	/** Holds a request answered later; false, with nothing held, for an id already held or past MaxHeldRequests. */
	bool HoldRequest(int64 InGeneration, const FString& Id);

	/** Answers a held request once; false, and nothing sent, when it is not held or InGeneration is not the current boot. */
	bool AnswerRequest(int64 InGeneration, const FString& Id, const TSharedRef<FJsonObject>& Result);
	bool FailRequest(int64 InGeneration, const FString& Id, const FString& Message);
	void RefuseHeldRequests(const TCHAR* Message);

	/** Forgets every held request and the deploy in progress, for a boot whose page is gone. */
	void ResetRequests();

	void ContinueDeploy(ECrowdyStudioResult Result, const FString& Message);

	/** Before the prompt shows: refuses a deploy that would replace a mod on the grid this project did not deploy. */
	void CheckReplacedMod();
	void FinishReplacedCheck(const FString& ModName);
	void ReceiveGridMods(int64 InGeneration, const FString& RequestId, int64 GridId, const FString& ModName, const FCrowdyModList& List);

	void FailDeploy(const FString& Message);
	void AnswerDeploy(const TSharedRef<FJsonObject>& Result);
	void ForgetDeploy();
	void ReceiveServerMod(int64 InGeneration, const FString& RequestId, const FString& ModId, const FString& Module, const FCrowdyModList& List);

	/** Goes on with a finished run's BuildReply: waits for its CLIENT half when one was deployed, else answers it. */
	void ContinueBuild();

	/** Settles BuildReply's summary and answers the deploy request with it. */
	void AnswerBuild();
	void WaitForClient();
	bool IsAwaitedScript(const FString& ModId, const FString& Digest) const;
	bool IsAwaitedClientRunning() const;
	void ClientStarted();
	void ClientFailed(const FString& Message);
	void ClientWaitEnded();
	void ReceiveScriptStarted(int64 InGeneration, const FString& ModId, const FString& Digest);
	void ReceiveScriptStopped(int64 InGeneration, const FString& ModId, const FString& Digest, ECrowdyModScriptStop Why);
	void ReceiveClientStatus(int64 InGeneration, const FString& RequestId, const FString& ModId, const FCrowdyModScriptStatusList& List);
	void CaptureForRun();
	void FinishRunCapture(const FConstructStudioCapture& Capture);
	void DropRunCapture();
	void DropScriptWatch();

	void AnswerRuntimeStatus(int64 InGeneration, const FString& Id);
	void AnswerRuntimeLogs(int64 InGeneration, const FString& Id, int32 Limit);
	void AnswerClientLogs(int64 InGeneration, const FString& Id, int32 Limit);
	void ReadClientLog(int64 InGeneration, const FString& Id, int32 Limit, const FString& ModId);
	void AnswerProjectList(int64 InGeneration, const FString& Id);
	void OpenProject(int64 InGeneration, const FString& Id, const FString& ProjectId);
	void CreateProject(int64 InGeneration, const FString& Id, const FString& Name, const FString& Template);
	void ProjectOpened(int64 InGeneration, const FString& Id, const FCrowdyStudioProjectInfo& Info);

	/** Looks up the claim at the player's chunk, then answers grid.context, or game.observe when bObserve. */
	void LookUpGrid(int64 InGeneration, const FString& Id, bool bObserve);
	void ReceiveGridClaim(int64 InGeneration, const FString& Id, bool bObserve, const FCrowdyClaimSearch& Search);
	void KeepObservedClaims(int64 InGeneration, const FInt64Vector& Chunk, int64 GridId, const FCrowdyClaimSearch& Search);
	void AnswerObservation(int64 InGeneration, const FString& Id, const TOptional<FConstructStudioObservedGrid>& Grid);
	TOptional<FInt64Vector> PlayerChunk() const;
	TSharedPtr<FJsonObject> Observe(const TOptional<FConstructStudioObservedGrid>& Grid) const;

	/** Sends a page event other than page.hello and page.token; a screenshot travels as bytesBase64, which the page turns into bytes. */
	bool TellAgent(int64 InGeneration, const FString& Event, const TSharedRef<FJsonObject>& Payload);

	void SendHello(int64 InGeneration);
	void DeliverToPage(const FString& FrameJson);
	bool IsMainFrameOnOrigin() const;
	void ShowBrowser();
	void BindBridge();
	void ReleasePage();
	bool BrowserNavigating(const FString& Url, const FWebNavigationRequest& Request);
	bool BrowserPopup(FString Url, FString Frame);

	EConstructStudioDockState State = EConstructStudioDockState::Closed;
	EConstructStudioDockReason Reason = EConstructStudioDockReason::None;
	FText ReasonText;
	FText ConsentNotice;
	int64 Generation = 0;
	int64 BootUser = 0;
	int64 CurrentGridId = 0;
	FText CurrentGridLabel;
	FString CurrentProjectId;
	FString CurrentProjectName;
	FString CurrentRevision;
	FString CurrentServerModule;
	FString CurrentClientModule;
	bool bCurrentReachesVisitors = false;
	bool bProjectRead = false;
	bool bProjectReadInFlight = false;
	// A save the project has not been read back for yet.
	bool bProjectDirty = false;
	bool bUsageInFlight = false;
	TArray<FString> ChangedFiles;
	FConstructStudioDeployPrompt Pending;
	// The module names when the agent asked; the run refuses a project whose names changed since.
	FString PendingServerModule;
	FString PendingClientModule;
	double PromptAge = 0.0;
	double BootAge = 0.0;
	double UsageAge = 0.0;
	double SaveQuietAge = 0.0;
	bool bRunInFlight = false;
	bool bBootReplayed = false;
	bool bFocusReported = false;
	TOptional<int64> UserForTests;
	bool bAccessEndedForTests = false;
	TOptional<bool> PlaceholderFocusForTests;
	TWeakObjectPtr<UObject> HostObject;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UConstructStudioCaptureRequest>> PendingCaptures;

	FTSTicker::FDelegateHandle Ticker;
	FDelegateHandle WorldCleanupHandle;
	// Weak, as UWidget holds MyWidget: the slot the panel gives the dock owns its Slate widget.
	TWeakPtr<SConstructStudioDockPlaceholder> Placeholder;

	/** The page's object, made for each boot and bound for each load of the page; never this widget. */
	UPROPERTY(Transient)
	TObjectPtr<UConstructStudioDockBridge> Bridge;

	TSharedPtr<FConstructStudioDockChannel> Channel;
	TSharedPtr<const FCrowdyStudioAgentBoot> AgentBoot;
	TSharedPtr<FConstructStudioDockPlatform> Platform;
	TSharedPtr<FConstructStudioDockPlatform> PlatformForTests;
	TSharedPtr<SWebBrowser> Browser;
	TWeakObjectPtr<UCrowdyStudioAgent> BoundAgent;
	FDelegateHandle AgentTokenHandle;
	FString StudioOrigin;
	FString WalletUrl;
	bool bLiveBoot = false;
	TOptional<FString> MainFrameUrlForTests;
	TOptional<FString> TokenForTests;
	TFunction<void(const FString&)> ScriptsForTests;

	/** The agent's requests answered later, by id; each is answered once. */
	TArray<FString> HeldRequests;

	/** The agent's draft test or deploy request in progress, from its arrival until it is answered or the agent stops waiting. */
	FString DeployRequestId;
	double DeployRequestAge = 0.0;
	EConstructStudioDeployKind DeployKind = EConstructStudioDeployKind::DraftTest;

	/** The project is being read fresh for a deploy request before its prompt shows. */
	bool bDeployReading = false;

	/** The grid's mods are being read for a deploy request before its prompt shows. */
	bool bDeployChecking = false;

	/** A finished run's answer, held while its SERVER mod is read and its deployed CLIENT half starts here and is captured. */
	TSharedPtr<FJsonObject> BuildReply;
	FString AwaitedClientModId;
	FString AwaitedClientDigest;
	int64 AwaitedClientGridId = 0;
	double ClientWaitAge = 0.0;
	bool bServerModReading = false;
	bool bAwaitingClient = false;
	bool bClientStatusReading = false;
	bool bRunCapturing = false;
	int32 CaptureCount = 0;

	/** game.observe's last claim lookup, for the chunk and grid it was made on, and its age. */
	TSharedPtr<const FCrowdyClaimSearch> ObservedClaims;
	FInt64Vector ObservedClaimChunk = FInt64Vector(0, 0, 0);
	int64 ObservedClaimGridId = 0;
	double ObservedClaimAge = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UConstructStudioScriptWatch> ScriptWatch;

	TOptional<FInt64Vector> ChunkForTests;
	TFunction<TSharedPtr<FJsonObject>(const TOptional<FConstructStudioObservedGrid>&)> ObserveForTests;
};
