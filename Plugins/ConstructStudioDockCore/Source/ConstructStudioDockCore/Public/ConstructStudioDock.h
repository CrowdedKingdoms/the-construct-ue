#pragma once

#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "ConstructStudioDockHost.h"
#include "ConstructStudioDockTypes.h"
#include "ConstructStudioDock.generated.h"

class FConstructStudioDockChannel;
class FJsonObject;
class SConstructStudioDockPlaceholder;
class SWebBrowser;
class UConstructStudioCaptureRequest;
class UConstructStudioDockBridge;
class UCrowdyStudioAgent;
enum class EConstructStudioDockRefusal : uint8;
enum class ECrowdyStudioResult : uint8;
struct FConstructStudioDockPlatform;
struct FCrowdyModRun;
struct FCrowdyStudioAgentBoot;
struct FCrowdyStudioConsent;
struct FCrowdyStudioLiveModding;
struct FCrowdyStudioModelUsage;
struct FCrowdyStudioProject;
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
	void ReceiveDeployRequest(int64 InGeneration, EConstructStudioDeployKind Kind);
	void ReceiveRunAnswer(int64 InGeneration, bool bOk);
	void ReceiveScreenshotRequest(int64 InGeneration, const FString& Label);
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
	void RequestCapture(const FString& Label, bool bShared);
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
	bool RefuseToAgent(int64 InGeneration, const FString& Id, EConstructStudioDockRefusal Why);

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
};
