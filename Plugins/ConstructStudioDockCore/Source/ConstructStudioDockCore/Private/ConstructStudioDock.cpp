#include "ConstructStudioDock.h"

#include "ConstructStudioCaptureRequest.h"
#include "ConstructStudioDockBridge.h"
#include "ConstructStudioDockFrames.h"
#include "ConstructStudioDockLog.h"
#include "ConstructStudioDockPlatform.h"
#include "ConstructStudioDockReplies.h"
#include "ConstructStudioDockScheme.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "HAL/PlatformProcess.h"
#include "IWebBrowserWindow.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SWebBrowser.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdyStudioAgent.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructStudioDock"

/** The dock's slot: the agent's page once it boots, a line of text before. Takes keyboard focus on a click, reports Escape and its own end. */
class SConstructStudioDockPlaceholder final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConstructStudioDockPlaceholder) {}
		SLATE_ATTRIBUTE(FText, Text)
	SLATE_END_ARGS()

	virtual ~SConstructStudioDockPlaceholder() override
	{
		if (Destroyed)
		{
			Destroyed();
		}
	}

	void Construct(const FArguments& InArgs, TFunction<void(bool)> InFocusChanged, TFunction<void()> InEscape, TFunction<void()> InDestroyed)
	{
		FocusChanged = MoveTemp(InFocusChanged);
		Escape = MoveTemp(InEscape);
		Destroyed = MoveTemp(InDestroyed);
		Text = InArgs._Text;
		ShowText();
	}

	void ShowText()
	{
		ChildSlot
		[
			SNew(SBorder)
			.Padding(FMargin(16.f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Text)
				.AutoWrapText(true)
			]
		];
	}

	void ShowContent(const TSharedRef<SWidget>& Content)
	{
		ChildSlot
		[
			Content
		];
	}

	/** A released slot that outlives the release no longer closes the dock when it goes. */
	void Detach()
	{
		Destroyed.Reset();
	}

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
	{
		return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
	}

	// Called for focus anywhere under the slot, so focus inside the agent's page counts as the dock's.
	virtual void OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& Event) override
	{
		SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, Event);
		FocusChanged(NewWidgetPath.ContainsWidget(this));
	}

	// The browser hands back a key its page did not handle, so Escape inside the page reaches this too.
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
	{
		if (Event.GetKey() != EKeys::Escape)
		{
			return FReply::Unhandled();
		}
		Escape();
		return FReply::Handled();
	}

private:
	TFunction<void(bool)> FocusChanged;
	TFunction<void()> Escape;
	TFunction<void()> Destroyed;
	TAttribute<FText> Text;
};

TMulticastDelegate<void(UConstructStudioDock&, FName, const FString&)> UConstructStudioDock::Outbound;
TWeakObjectPtr<UConstructStudioDock> UConstructStudioDock::OpenDock;
int64 UConstructStudioDock::LastPromptId = 0;

void UConstructStudioCaptureRequest::Done(const FConstructStudioCapture& Capture)
{
	if (bAnswered || !ensureMsgf(IsInGameThread(), TEXT("CaptureView answered off the game thread; the answer is dropped.")))
	{
		return;
	}
	bAnswered = true;
	if (UConstructStudioDock* Owner = Dock.Get())
	{
		Owner->CaptureAnswered(*this, Capture);
	}
}

FString UConstructStudioDockBridge::Boot()
{
	UConstructStudioDock* Owner = Dock.Get();
	return Owner ? Owner->BridgeBoot(*this) : ConstructStudioDockFrames::ErrorReply(TEXT("closed"));
}

FString UConstructStudioDockBridge::Send(const FString& Frame)
{
	UConstructStudioDock* Owner = Dock.Get();
	return Owner ? Owner->BridgeSend(*this, Frame) : ConstructStudioDockFrames::ErrorReply(TEXT("closed"));
}

EConstructStudioDockReason UConstructStudioDock::Open(int64 GridId, const FText& GridLabel, const FString& ProjectId)
{
	if (State != EConstructStudioDockState::Closed)
	{
		return EConstructStudioDockReason::AlreadyOpen;
	}
	if (OpenDock.IsValid() && OpenDock.Get() != this)
	{
		return EConstructStudioDockReason::NotAvailable;
	}
	if constexpr (!PLATFORM_WINDOWS)
	{
		return EConstructStudioDockReason::NotAvailable;
	}
#if UE_BUILD_SHIPPING
	// A DevTools port would let any local process drive the page and read the player's token.
	FString DebugPort;
	if (FParse::Param(FCommandLine::Get(), TEXT("cefdebug")) || FParse::Value(FCommandLine::Get(), TEXT("cefdebug="), DebugPort))
	{
		return EConstructStudioDockReason::NotAvailable;
	}
#endif
	bool bAccessEnded = false;
	const int64 User = SignedInUser(bAccessEnded);
	if (User <= 0)
	{
		return EConstructStudioDockReason::NotSignedIn;
	}
	if (GridId <= 0)
	{
		return EConstructStudioDockReason::NoGrid;
	}

	++Generation;
	BootUser = User;
	CurrentGridId = GridId;
	CurrentGridLabel = GridLabel;
	CurrentProjectId = ProjectId;
	CurrentProjectName.Reset();
	CurrentRevision.Reset();
	CurrentServerModule.Reset();
	CurrentClientModule.Reset();
	bCurrentReachesVisitors = false;
	bProjectRead = false;
	bProjectReadInFlight = false;
	bProjectDirty = false;
	bUsageInFlight = false;
	ChangedFiles.Reset();
	Pending = FConstructStudioDeployPrompt();
	ConsentNotice = FText::GetEmpty();
	UsageAge = 0.0;
	SaveQuietAge = 0.0;
	bRunInFlight = false;
	bBootReplayed = false;
	bLiveBoot = false;
	bFocusReported = false;
	ResetRequests();
	CaptureCount = 0;
	if (!Channel.IsValid())
	{
		Channel = MakeShared<FConstructStudioDockChannel>([this](const FString& FrameJson) { DeliverToPage(FrameJson); });
	}
	OpenDock = this;
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UConstructStudioDock::Advance));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UConstructStudioDock::WorldCleanup);
	SetState(EConstructStudioDockState::Booting, EConstructStudioDockReason::None);
	Send(TEXT("Open"), LexToString(GridId));
	// A stand-in that claimed the boot while hearing Open replays it instead.
	if (!bBootReplayed && State == EConstructStudioDockState::Booting)
	{
		StartLiveBoot();
	}
	// Slate sends no focus event for focus the slot already held while the dock was closed.
	if (PlaceholderHasFocus())
	{
		ReceiveFocus(Generation, true);
	}
	return EConstructStudioDockReason::None;
}

void UConstructStudioDock::Close()
{
	CloseFor(EConstructStudioDockReason::Closed);
}

void UConstructStudioDock::CloseFor(EConstructStudioDockReason Why)
{
	if (State == EConstructStudioDockState::Closed)
	{
		return;
	}
	const int64 Withdrawn = Pending.PromptId;
	const bool bHadFocus = bFocusReported;
	Pending = FConstructStudioDeployPrompt();
	RefuseCaptures(TEXT("closed"));
	RefuseHeldRequests(TEXT("The Studio dock closed."));
	TellAgent(Generation, TEXT("page.bye"), MakeShared<FJsonObject>());
	Teardown();
	ClearPlaceholderFocus();
	Reason = Why;
	ReasonText = DescribeReason(Why);
	if (Withdrawn != 0)
	{
		OnDeployPromptWithdrawn.Broadcast(Withdrawn);
		Send(TEXT("AnswerDeployPrompt"), TEXT("Refused"));
	}
	Send(TEXT("Close"), StaticEnum<EConstructStudioDockReason>()->GetNameStringByValue(static_cast<int64>(Why)));
	if (bHadFocus)
	{
		OnFocusChanged.Broadcast(false);
	}
	OnStateChanged.Broadcast(EConstructStudioDockState::Closed, Why);
}

void UConstructStudioDock::Teardown()
{
	State = EConstructStudioDockState::Closed;
	bFocusReported = false;
	bRunInFlight = false;
	bProjectReadInFlight = false;
	bProjectDirty = false;
	bUsageInFlight = false;
	PendingCaptures.Reset();
	ReleasePage();
	UnbindAgent();
	Platform.Reset();
	bLiveBoot = false;
	StudioOrigin.Reset();
	WalletUrl.Reset();
	FTSTicker::RemoveTicker(Ticker);
	Ticker.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	WorldCleanupHandle.Reset();
	if (!OpenDock.IsValid() || OpenDock.Get() == this)
	{
		OpenDock.Reset();
	}
}

void UConstructStudioDock::SetGrid(int64 GridId, const FText& GridLabel)
{
	if (State == EConstructStudioDockState::Closed || GridId <= 0)
	{
		return;
	}
	if (GridId != CurrentGridId)
	{
		WithdrawPrompt(TEXT("Refused"));
	}
	CurrentGridId = GridId;
	CurrentGridLabel = GridLabel;
	Send(TEXT("SetGrid"), LexToString(GridId));
	TellGridContext(Generation);
}

void UConstructStudioDock::TellGridContext(int64 InGeneration)
{
	const TSharedRef<FJsonObject> Context = MakeShared<FJsonObject>();
	Context->SetObjectField(TEXT("grid"), GridContext());
	TellAgent(InGeneration, TEXT("page.context"), Context);
}

TSharedRef<FJsonObject> UConstructStudioDock::GridContext() const
{
	const TSharedRef<FJsonObject> Grid = MakeShared<FJsonObject>();
	Grid->SetStringField(TEXT("gridId"), LexToString(CurrentGridId));
	Grid->SetStringField(TEXT("label"), CurrentGridLabel.ToString());
	return Grid;
}

EConstructStudioDockState UConstructStudioDock::GetState() const
{
	return State;
}

EConstructStudioDockReason UConstructStudioDock::GetReason() const
{
	return Reason;
}

FText UConstructStudioDock::GetReasonText() const
{
	return ReasonText;
}

FText UConstructStudioDock::DescribeReason(EConstructStudioDockReason Why)
{
	switch (Why)
	{
	case EConstructStudioDockReason::NotSignedIn: return LOCTEXT("NotSignedIn", "Sign in to open the Studio dock.");
	case EConstructStudioDockReason::NoGrid: return LOCTEXT("NoGrid", "Stand in a chunk you claimed to open the Studio dock.");
	case EConstructStudioDockReason::AlreadyOpen: return LOCTEXT("AlreadyOpen", "The Studio dock is already open.");
	case EConstructStudioDockReason::NotAvailable: return LOCTEXT("NotAvailable", "The Studio dock is not available here.");
	case EConstructStudioDockReason::NeedsConsent: return LOCTEXT("NeedsConsent", "Agree to the provider notice to start the agent.");
	case EConstructStudioDockReason::NoModel: return LOCTEXT("NoModel", "No model is available for the agent.");
	case EConstructStudioDockReason::Unavailable: return LOCTEXT("Unavailable", "The Studio could not be reached.");
	case EConstructStudioDockReason::HarnessFailed: return LOCTEXT("HarnessFailed", "The agent failed to start.");
	case EConstructStudioDockReason::BootTimedOut: return LOCTEXT("BootTimedOut", "The agent took too long to start.");
	case EConstructStudioDockReason::NotBuiltYet: return LOCTEXT("NotBuiltYet", "The Studio dock is not ready yet.");
	case EConstructStudioDockReason::Closed: return LOCTEXT("Closed", "The Studio dock closed.");
	case EConstructStudioDockReason::SignedOut: return LOCTEXT("SignedOut", "The Studio dock closed because you signed out.");
	case EConstructStudioDockReason::UserChanged: return LOCTEXT("UserChanged", "The Studio dock closed because the signed-in account changed.");
	case EConstructStudioDockReason::AccessEnded: return LOCTEXT("AccessEnded", "The Studio dock closed because app access ended.");
	case EConstructStudioDockReason::WorldEnded: return LOCTEXT("WorldEnded", "The Studio dock closed with the world.");
	default: return FText::GetEmpty();
	}
}

FText UConstructStudioDock::GetConsentNotice() const
{
	return ConsentNotice;
}

void UConstructStudioDock::GiveProviderConsent()
{
	if (State != EConstructStudioDockState::NeedsConsent)
	{
		return;
	}
	SetState(EConstructStudioDockState::Booting, EConstructStudioDockReason::None);
	Send(TEXT("GiveProviderConsent"));
	if (!bLiveBoot || !Platform.IsValid() || State != EConstructStudioDockState::Booting)
	{
		return;
	}
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const int64 Consenting = Generation;
	Platform->RecordProviderConsent([Weak, Consenting](const FCrowdyStudioConsent& Consent)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveConsent(Consenting, Consent);
		}
	});
}

void UConstructStudioDock::AnswerDeployPrompt(int64 PromptId, bool bRun)
{
	if (PromptId == 0 || PromptId != Pending.PromptId)
	{
		return;
	}
	if (!bRun)
	{
		const bool bLive = Pending.Kind == EConstructStudioDeployKind::Live;
		Pending = FConstructStudioDeployPrompt();
		Send(TEXT("AnswerDeployPrompt"), TEXT("NotNow"));
		FailDeploy(bLive ? TEXT("The player declined the live deploy. Keep working with draft tests.")
			: TEXT("The player declined the draft test. Ask them before testing again."));
		return;
	}
	if (State != EConstructStudioDockState::Ready || Pending.GridId != CurrentGridId
		|| !Pending.ProjectId.Equals(CurrentProjectId, ESearchCase::CaseSensitive) || !Pending.Revision.Equals(CurrentRevision, ESearchCase::CaseSensitive))
	{
		WithdrawPrompt(TEXT("Refused"));
		return;
	}
	const FConstructStudioDeployPrompt Approved = Pending;
	Pending = FConstructStudioDeployPrompt();
	bRunInFlight = true;
	Send(TEXT("AnswerDeployPrompt"), TEXT("Run"));
	StartRun(Approved, PendingServerModule, PendingClientModule);
}

void UConstructStudioDock::StartRun(const FConstructStudioDeployPrompt& Approved, const FString& ServerModule, const FString& ClientModule)
{
	if (!bRunInFlight || !bLiveBoot || !Platform.IsValid())
	{
		return;
	}
	const ECrowdyStudioRunMode Mode = Approved.Kind == EConstructStudioDeployKind::Live ? ECrowdyStudioRunMode::Live : ECrowdyStudioRunMode::DraftTest;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const int64 Running = Generation;
	// Neither RunProject nor the platform's build and deploy calls check the app's Allow Live Modding switch.
	Platform->AuthorizeLiveModding(Mode, [Weak, Running, Approved, ServerModule, ClientModule](const FCrowdyStudioLiveModding& Verdict)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveLiveModding(Running, Approved, ServerModule, ClientModule, Verdict);
		}
	});
}

void UConstructStudioDock::ReceiveLiveModding(int64 InGeneration, const FConstructStudioDeployPrompt& Approved, const FString& ServerModule, const FString& ClientModule, const FCrowdyStudioLiveModding& Verdict)
{
	if (!IsCurrent(InGeneration) || !bRunInFlight || !Platform.IsValid())
	{
		return;
	}
	if (Verdict.Result != ECrowdyStudioResult::Success || !Verdict.bAllowed)
	{
		ReceiveRunAnswer(InGeneration, false);
		const TCHAR* Why = Verdict.Result == ECrowdyStudioResult::Success ? TEXT("Allow Live Modding is off for this app, so nothing was built or deployed.")
			: TEXT("The app's Allow Live Modding switch could not be read, so nothing was built or deployed.");
		FailDeploy(Verdict.Message.IsEmpty() ? FString(Why) : Verdict.Message);
		return;
	}
	const ECrowdyStudioRunMode Mode = Approved.Kind == EConstructStudioDeployKind::Live ? ECrowdyStudioRunMode::Live : ECrowdyStudioRunMode::DraftTest;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->RunProject(Approved.GridId, Approved.ProjectId, Approved.Revision, Mode, ServerModule, ClientModule, [Weak, InGeneration](const FCrowdyModRun& Run)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveModRun(InGeneration, Run);
		}
	});
}

void UConstructStudioDock::ReceiveModRun(int64 InGeneration, const FCrowdyModRun& Run)
{
	if (!IsCurrent(InGeneration) || !bRunInFlight)
	{
		return;
	}
	const bool bOk = ConstructStudioDockReplies::RunIsOk(Run);
	ReceiveRunAnswer(InGeneration, bOk);
	// The agent stopped waiting for this run, so its answer is not sent.
	if (DeployRequestId.IsEmpty())
	{
		return;
	}
	// Refused before anything was built (busy, a changed revision, signed out), so there is no half to report.
	if (Run.Targets.IsEmpty())
	{
		FailDeploy(Run.Message.IsEmpty() ? FString(TEXT("The run did not start, so nothing was built or deployed.")) : Run.Message);
		return;
	}
	if (Run.Result != ECrowdyModResult::Success)
	{
		AnswerDeploy(ConstructStudioDockReplies::BuildResult(Run));
		return;
	}
	BuildReply = ConstructStudioDockReplies::BuildResult(Run);
	const FCrowdyModRunTarget* Client = bOk ? ConstructStudioDockReplies::DeployedClient(Run) : nullptr;
	AwaitedClientModId = Client ? Client->ModId : FString();
	AwaitedClientDigest = Client ? Client->Digest : FString();
	AwaitedClientGridId = Run.GridId;
	const FCrowdyModRunTarget* Server = bOk ? ConstructStudioDockReplies::DeployedServer(Run) : nullptr;
	if (!Server || !Platform.IsValid() || !Platform->ListMods)
	{
		ContinueBuild();
		return;
	}
	// Deployed is not running: the SERVER mod's own switch and block decide whether it runs.
	bServerModReading = true;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->ListMods(Run.GridId, [Weak, InGeneration, RequestId = DeployRequestId, ModId = Server->ModId, Module = Server->Module](const FCrowdyModList& List)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveServerMod(InGeneration, RequestId, ModId, Module, List);
		}
	});
}

void UConstructStudioDock::OpenWallet()
{
	if (State == EConstructStudioDockState::Closed)
	{
		return;
	}
	// Only the wallet URL the platform gave with the boot, never one from the page.
	if (WalletUrl.IsEmpty())
	{
		Send(TEXT("OpenWallet"), TEXT("no wallet URL"));
		return;
	}
	FPlatformProcess::LaunchURL(*WalletUrl, nullptr, nullptr);
	Send(TEXT("OpenWallet"), TEXT("opened"));
}

void UConstructStudioDock::ShareScreenshot()
{
	if (State != EConstructStudioDockState::Ready)
	{
		return;
	}
	RequestCapture(TEXT("Player screenshot"), true);
}

void UConstructStudioDock::SetHost(TScriptInterface<IConstructStudioDockHost> Host)
{
	UObject* Object = Host.GetObject();
	HostObject = Object && Object->GetClass()->ImplementsInterface(UConstructStudioDockHost::StaticClass()) ? Object : nullptr;
}

void UConstructStudioDock::FinishCapture(const FOnConstructStudioCaptured& OnDone, const FConstructStudioCapture& Capture)
{
	OnDone.ExecuteIfBound(Capture);
}

void UConstructStudioDock::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (const TSharedPtr<SConstructStudioDockPlaceholder> Released = Placeholder.Pin())
	{
		Released->Detach();
	}
	Placeholder.Reset();
	SlateWidgetGone();
	// The page never outlives its slot, whether or not the dock closed with it.
	ReleasePage();
}

void UConstructStudioDock::BeginDestroy()
{
	if (State != EConstructStudioDockState::Closed)
	{
		Teardown();
	}
	Super::BeginDestroy();
}

#if WITH_EDITOR
const FText UConstructStudioDock::GetPaletteCategory()
{
	return LOCTEXT("Palette", "Construct");
}
#endif

TSharedRef<SWidget> UConstructStudioDock::RebuildWidget()
{
	TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const TSharedRef<SConstructStudioDockPlaceholder> Built = SNew(SConstructStudioDockPlaceholder,
		[Weak](bool bFocused)
		{
			if (UConstructStudioDock* Dock = Weak.Get())
			{
				Dock->ReceiveFocus(Dock->Generation, bFocused);
			}
		},
		[Weak]()
		{
			if (UConstructStudioDock* Dock = Weak.Get())
			{
				Dock->ReceiveCloseRequest(Dock->Generation);
			}
		},
		[Weak]()
		{
			if (UConstructStudioDock* Dock = Weak.Get())
			{
				Dock->SlateWidgetGone();
			}
		})
		.Text_Lambda([Weak]()
		{
			const UConstructStudioDock* Dock = Weak.Get();
			return Dock ? Dock->PlaceholderText() : FText::GetEmpty();
		});
	Placeholder = Built;
	ShowBrowser();
	return Built;
}

void UConstructStudioDock::ClaimBoot()
{
	if (State == EConstructStudioDockState::Booting)
	{
		bBootReplayed = true;
	}
}

void UConstructStudioDock::ReceiveBootOutcome(int64 InGeneration, EConstructStudioDockState NewState, EConstructStudioDockReason NewReason, const FText& Text)
{
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	if (NewState == EConstructStudioDockState::Failed)
	{
		if (State != EConstructStudioDockState::Failed)
		{
			Fail(NewReason == EConstructStudioDockReason::None ? EConstructStudioDockReason::HarnessFailed : NewReason, Text);
		}
		return;
	}
	if (State != EConstructStudioDockState::Booting)
	{
		return;
	}
	if (NewState == EConstructStudioDockState::NeedsConsent)
	{
		ConsentNotice = Text;
		SetState(EConstructStudioDockState::NeedsConsent, EConstructStudioDockReason::NeedsConsent);
		return;
	}
	if (NewState == EConstructStudioDockState::Ready)
	{
		SetState(EConstructStudioDockState::Ready, EConstructStudioDockReason::None);
		RequestUsage();
	}
}

void UConstructStudioDock::ReceiveProject(int64 InGeneration, const FString& InProjectId, const FString& Name, const FString& InRevision, bool bInReachesVisitors)
{
	if (!IsCurrent(InGeneration) || State == EConstructStudioDockState::Failed || InProjectId.IsEmpty())
	{
		return;
	}
	if (!InProjectId.Equals(CurrentProjectId, ESearchCase::CaseSensitive))
	{
		ChangedFiles.Reset();
	}
	CurrentProjectId = InProjectId;
	CurrentProjectName = Name;
	CurrentRevision = InRevision;
	bCurrentReachesVisitors = bInReachesVisitors;
	bProjectRead = !InRevision.IsEmpty();
}

void UConstructStudioDock::ReceiveFileChanged(int64 InGeneration, const FString& Path, const FString& InRevision)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Ready)
	{
		return;
	}
	ChangedFiles.AddUnique(Path);
	CurrentRevision = InRevision;
	if (Pending.PromptId != 0 && !Pending.Revision.Equals(CurrentRevision, ESearchCase::CaseSensitive))
	{
		WithdrawPrompt(TEXT("Refused"));
	}
}

void UConstructStudioDock::ReceiveDeployRequest(int64 InGeneration, EConstructStudioDeployKind Kind, const FString& RequestId)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Ready)
	{
		FailRequest(InGeneration, RequestId, ConstructStudioDockReplies::NotReadyText);
		return;
	}
	const TCHAR* Message = Kind == EConstructStudioDeployKind::Live ? TEXT("studio.deployLive") : TEXT("studio.draftTest");
	if (Pending.PromptId != 0 || bRunInFlight || bDeployReading || bDeployChecking || BuildReply.IsValid())
	{
		Send(Message, TEXT("refused: busy"));
		FailRequest(InGeneration, RequestId, TEXT("A deploy is already waiting for the player or running. Wait for it before asking again."));
		return;
	}
	// A live dock reads the project fresh, so the prompt carries the revision saved now, not the last one heard of.
	if (bLiveBoot && !CurrentProjectId.IsEmpty())
	{
		DeployKind = Kind;
		DeployRequestId = RequestId;
		DeployRequestAge = 0.0;
		bDeployReading = true;
		// A read already in flight is waited for; a save after it began marks the project dirty, and it is read again.
		if (!bProjectReadInFlight)
		{
			bProjectDirty = false;
			ReadProject();
		}
		return;
	}
	// A project id passed to Open is not enough: the prompt needs the revision the platform read for it, after the last save.
	if (bLiveBoot || !bProjectRead || CurrentRevision.IsEmpty() || bProjectDirty || bProjectReadInFlight)
	{
		Send(Message, TEXT("refused: no project"));
		FailRequest(InGeneration, RequestId, ConstructStudioDockReplies::NoProjectText);
		return;
	}
	RaisePrompt(Kind);
}

void UConstructStudioDock::ContinueDeploy(ECrowdyStudioResult Result, const FString& Message)
{
	// A save while the project was read: read it again, so the prompt never shows a stale revision.
	if (bProjectDirty && Result == ECrowdyStudioResult::Success)
	{
		bProjectDirty = false;
		ReadProject();
		return;
	}
	bDeployReading = false;
	if (Result != ECrowdyStudioResult::Success || CurrentRevision.IsEmpty())
	{
		Send(DeployKind == EConstructStudioDeployKind::Live ? TEXT("studio.deployLive") : TEXT("studio.draftTest"), TEXT("refused: no project"));
		FailDeploy(Message.IsEmpty() ? FString(ConstructStudioDockReplies::NoProjectText) : Message);
		return;
	}
	CheckReplacedMod();
}

void UConstructStudioDock::RaisePrompt(EConstructStudioDeployKind Kind)
{
	// A prompt is only ever raised on a running dock, whatever answer arrives late.
	if (State != EConstructStudioDockState::Ready)
	{
		ForgetDeploy();
		return;
	}
	Pending.PromptId = ++LastPromptId;
	Pending.Kind = Kind;
	Pending.GridId = CurrentGridId;
	Pending.GridLabel = CurrentGridLabel;
	Pending.ProjectId = CurrentProjectId;
	// The agent may have written the name and the paths, and the player reads them in the prompt.
	Pending.ProjectName = ConstructStudioDockReplies::PlainText(CurrentProjectName);
	Pending.Revision = CurrentRevision;
	Pending.bReachesVisitors = bCurrentReachesVisitors;
	Pending.ChangedFiles.Reset(ChangedFiles.Num());
	for (const FString& File : ChangedFiles)
	{
		Pending.ChangedFiles.Add(ConstructStudioDockReplies::PlainText(File));
	}
	PendingServerModule = CurrentServerModule;
	PendingClientModule = CurrentClientModule;
	PromptAge = 0.0;
	const FConstructStudioDeployPrompt Shown = Pending;
	OnDeployPrompt.Broadcast(Shown);
}

void UConstructStudioDock::ReceiveRunAnswer(int64 InGeneration, bool bOk)
{
	if (!IsCurrent(InGeneration) || !bRunInFlight)
	{
		return;
	}
	bRunInFlight = false;
	Send(TEXT("RunProject"), bOk ? TEXT("ok") : TEXT("failed"));
}

void UConstructStudioDock::ReceiveScreenshotRequest(int64 InGeneration, const FString& Label, const FString& RequestId)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Ready)
	{
		FailRequest(InGeneration, RequestId, ConstructStudioDockReplies::NotReadyText);
		return;
	}
	RequestCapture(Label, false, RequestId);
}

void UConstructStudioDock::ReceiveUsage(int64 InGeneration, const FConstructStudioUsage& Usage)
{
	if (IsCurrent(InGeneration))
	{
		OnUsageChanged.Broadcast(Usage);
	}
}

void UConstructStudioDock::ReceiveWarning(int64 InGeneration, const FText& Text)
{
	if (IsCurrent(InGeneration))
	{
		OnWarning.Broadcast(Text);
	}
}

void UConstructStudioDock::ReceiveFocus(int64 InGeneration, bool bInHasFocus)
{
	if (!IsCurrent(InGeneration) || bInHasFocus == bFocusReported)
	{
		return;
	}
	bFocusReported = bInHasFocus;
	OnFocusChanged.Broadcast(bInHasFocus);
}

void UConstructStudioDock::ReceiveCloseRequest(int64 InGeneration)
{
	if (IsCurrent(InGeneration))
	{
		OnCloseRequested.Broadcast();
	}
}

void UConstructStudioDock::CaptureAnswered(UConstructStudioCaptureRequest& Request, const FConstructStudioCapture& Capture)
{
	if (Request.Generation != Generation || PendingCaptures.Remove(&Request) == 0)
	{
		return;
	}
	const TCHAR* Message = Request.bShared ? TEXT("page.capture") : TEXT("studio.screenshot");
	Send(Message, Capture.bCaptured
		? FString::Printf(TEXT("captured %dx%d"), Capture.Width, Capture.Height)
		: FString::Printf(TEXT("refused: %s"), *Capture.Refusal));
	if (Request.bForRun)
	{
		FinishRunCapture(Capture);
		return;
	}
	const bool bShot = Capture.bCaptured && !Capture.PngBase64.IsEmpty();
	if (!Request.RequestId.IsEmpty() && bShot)
	{
		AnswerRequest(Request.Generation, Request.RequestId, ConstructStudioDockReplies::Screenshot(Capture, ++CaptureCount));
		return;
	}
	if (!Request.RequestId.IsEmpty())
	{
		FailRequest(Request.Generation, Request.RequestId, Capture.Refusal.IsEmpty() ? FString(TEXT("Nothing to capture right now.")) : Capture.Refusal);
		return;
	}
	if (!Request.bShared || !Capture.bCaptured || Capture.PngBase64.IsEmpty())
	{
		return;
	}
	const TSharedRef<FJsonObject> Shot = MakeShared<FJsonObject>();
	Shot->SetStringField(TEXT("bytesBase64"), Capture.PngBase64);
	Shot->SetStringField(TEXT("mediaType"), TEXT("image/png"));
	Shot->SetNumberField(TEXT("width"), Capture.Width);
	Shot->SetNumberField(TEXT("height"), Capture.Height);
	Shot->SetStringField(TEXT("caption"), Capture.Caption);
	TellAgent(Request.Generation, TEXT("page.capture"), Shot);
}

bool UConstructStudioDock::IsCurrent(int64 InGeneration) const
{
	return InGeneration == Generation && State != EConstructStudioDockState::Closed;
}

bool UConstructStudioDock::Advance(float DeltaSeconds)
{
	const int64 Ticking = Generation;
	if (CloseIfPlayerChanged())
	{
		return false;
	}
	if (State == EConstructStudioDockState::Booting && !bBootReplayed && !bLiveBoot)
	{
		Fail(EConstructStudioDockReason::NotBuiltYet, FText::GetEmpty());
		return IsCurrent(Ticking);
	}
	if (State == EConstructStudioDockState::Booting)
	{
		BootAge += DeltaSeconds;
		if (BootAge >= BootSeconds)
		{
			Fail(EConstructStudioDockReason::BootTimedOut, FText::GetEmpty());
			return IsCurrent(Ticking);
		}
	}
	if (State == EConstructStudioDockState::Ready)
	{
		UsageAge += DeltaSeconds;
		if (UsageAge >= UsageSeconds)
		{
			RequestUsage();
		}
	}
	// The agent saves once per tool call, so a burst of saves is read back once, after it ends.
	if (State == EConstructStudioDockState::Ready && bProjectDirty)
	{
		SaveQuietAge += DeltaSeconds;
		if (SaveQuietAge >= SaveQuietSeconds && !bProjectReadInFlight)
		{
			bProjectDirty = false;
			ReadProject();
		}
	}
	if (Pending.PromptId != 0)
	{
		PromptAge += DeltaSeconds;
		if (PromptAge >= PromptSeconds)
		{
			WithdrawPrompt(TEXT("NotNow"));
		}
	}
	if (!DeployRequestId.IsEmpty())
	{
		DeployRequestAge += DeltaSeconds;
		// The agent has stopped waiting: a run still going finishes on its grid, and its answer is not sent.
		if (DeployRequestAge >= RequestWaitSeconds)
		{
			HeldRequests.Remove(DeployRequestId);
			ForgetDeploy();
		}
	}
	// The wait for the CLIENT half and its screenshot share one clock, so the answer always goes.
	if (BuildReply.IsValid() && (bAwaitingClient || bRunCapturing))
	{
		ClientWaitAge += DeltaSeconds;
		if (ClientWaitAge >= ClientStartSeconds)
		{
			ClientWaitEnded();
		}
	}
	ObservedClaimAge += DeltaSeconds;
	return IsCurrent(Ticking);
}

bool UConstructStudioDock::CloseIfPlayerChanged()
{
	bool bAccessEnded = false;
	const int64 User = SignedInUser(bAccessEnded);
	if (User == BootUser)
	{
		return false;
	}
	CloseFor(bAccessEnded ? EConstructStudioDockReason::AccessEnded
		: User <= 0 ? EConstructStudioDockReason::SignedOut
		: EConstructStudioDockReason::UserChanged);
	return true;
}

void UConstructStudioDock::RequestUsage()
{
	// The clock keeps running, so the tick after the answer reads again.
	if (bUsageInFlight)
	{
		return;
	}
	UsageAge = 0.0;
	if (State != EConstructStudioDockState::Ready || !bLiveBoot || !Platform.IsValid())
	{
		return;
	}
	bUsageInFlight = true;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const int64 Reading = Generation;
	Platform->GetModelUsage([Weak, Reading](const FCrowdyStudioModelUsage& Usage)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveModelUsage(Reading, Usage);
		}
	});
}

void UConstructStudioDock::ReceiveModelUsage(int64 InGeneration, const FCrowdyStudioModelUsage& Usage)
{
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	bUsageInFlight = false;
	if (State != EConstructStudioDockState::Ready || Usage.Result != ECrowdyStudioResult::Success)
	{
		return;
	}
	ReceiveUsage(InGeneration, FConstructStudioDockPlatform::ToUsage(Usage));
}

void UConstructStudioDock::SetState(EConstructStudioDockState NewState, EConstructStudioDockReason NewReason, const FText& Text)
{
	State = NewState;
	Reason = NewReason;
	ReasonText = Text.IsEmpty() ? DescribeReason(NewReason) : Text;
	BootAge = 0.0;
	OnStateChanged.Broadcast(NewState, NewReason);
}

void UConstructStudioDock::Fail(EConstructStudioDockReason Why, const FText& Text)
{
	const int64 Failing = Generation;
	WithdrawPrompt(TEXT("Refused"));
	RefuseCaptures(TEXT("failed"));
	RefuseHeldRequests(TEXT("The Studio agent stopped."));
	ForgetDeploy();
	bRunInFlight = false;
	bProjectDirty = false;
	bUsageInFlight = false;
	if (IsCurrent(Failing))
	{
		// A failed dock keeps no page: the harness stops, the token is no longer sent and the failure text shows.
		ReleasePage();
		SetState(EConstructStudioDockState::Failed, Why, Text);
	}
}

void UConstructStudioDock::WithdrawPrompt(const TCHAR* Answer)
{
	if (Pending.PromptId == 0)
	{
		return;
	}
	const int64 Withdrawn = Pending.PromptId;
	Pending = FConstructStudioDeployPrompt();
	OnDeployPromptWithdrawn.Broadcast(Withdrawn);
	Send(TEXT("AnswerDeployPrompt"), Answer);
	FailDeploy(FCString::Strcmp(Answer, TEXT("NotNow")) == 0
		? TEXT("The player did not answer the deploy prompt within 60 seconds. Ask them before trying again.")
		: TEXT("The deploy prompt was withdrawn because the grid, project or saved revision changed. Ask again."));
}

void UConstructStudioDock::RefuseCaptures(const TCHAR* Why)
{
	const TArray<TObjectPtr<UConstructStudioCaptureRequest>> Refused = MoveTemp(PendingCaptures);
	PendingCaptures.Reset();
	for (const UConstructStudioCaptureRequest* Request : Refused)
	{
		Send(Request->bShared ? TEXT("page.capture") : TEXT("studio.screenshot"), FString::Printf(TEXT("refused: %s"), Why));
		FailRequest(Request->Generation, Request->RequestId, FString::Printf(TEXT("No screenshot: the Studio dock %s."), Why));
	}
}

void UConstructStudioDock::RequestCapture(const FString& Label, bool bShared, const FString& RequestId, bool bForRun)
{
	const TCHAR* Message = bShared ? TEXT("page.capture") : TEXT("studio.screenshot");
	UObject* Host = HostObject.Get();
	if (!Host)
	{
		Send(Message, TEXT("refused: no host"));
		FailRequest(Generation, RequestId, TEXT("This game is not taking screenshots for the agent right now."));
		return;
	}
	UConstructStudioCaptureRequest* Request = NewObject<UConstructStudioCaptureRequest>(this);
	Request->Dock = this;
	Request->Generation = Generation;
	Request->RequestId = RequestId;
	Request->bShared = bShared;
	Request->bForRun = bForRun;
	PendingCaptures.Add(Request);
	FOnConstructStudioCaptured OnDone;
	OnDone.BindUFunction(Request, GET_FUNCTION_NAME_CHECKED(UConstructStudioCaptureRequest, Done));
	IConstructStudioDockHost::Execute_CaptureView(Host, Label, OnDone);
}

void UConstructStudioDock::Send(FName Message, const FString& Detail)
{
	Outbound.Broadcast(*this, Message, Detail);
}

int64 UConstructStudioDock::SignedInUser(bool& bOutAccessEnded) const
{
	bOutAccessEnded = false;
	if (UserForTests.IsSet())
	{
		bOutAccessEnded = bAccessEndedForTests;
		return bAccessEndedForTests ? 0 : UserForTests.GetValue();
	}
	const UWorld* World = GetWorld();
	const UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	if (!Auth || !Session || !Auth->IsSignedIn())
	{
		return 0;
	}
	if (Session->HasAccessEnded())
	{
		bOutAccessEnded = true;
		return 0;
	}
	return Session->GetUserID();
}

void UConstructStudioDock::WorldCleanup(UWorld* CleanedWorld, bool bSessionEnded, bool bCleanupResources)
{
	if (CleanedWorld && CleanedWorld == GetWorld())
	{
		CloseFor(EConstructStudioDockReason::WorldEnded);
	}
}

void UConstructStudioDock::SlateWidgetGone()
{
	// A placeholder still alive means the gone one was replaced; an object being destroyed tears down in BeginDestroy.
	if (Placeholder.IsValid() || IsUnreachable() || HasAnyFlags(RF_BeginDestroyed))
	{
		return;
	}
	CloseFor(EConstructStudioDockReason::Closed);
}

bool UConstructStudioDock::PlaceholderHasFocus() const
{
	if (PlaceholderFocusForTests.IsSet())
	{
		return PlaceholderFocusForTests.GetValue();
	}
	const TSharedPtr<SConstructStudioDockPlaceholder> Pinned = Placeholder.Pin();
	return Pinned.IsValid() && FSlateApplication::IsInitialized() && Pinned->HasAnyUserFocusOrFocusedDescendants();
}

void UConstructStudioDock::ClearPlaceholderFocus()
{
	if (PlaceholderFocusForTests.IsSet())
	{
		PlaceholderFocusForTests = false;
		return;
	}
	const TSharedPtr<SConstructStudioDockPlaceholder> Pinned = Placeholder.Pin();
	if (!Pinned.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	FSlateApplication::Get().ForEachUser([&Pinned](FSlateUser& User)
	{
		if (User.HasFocus(Pinned).IsSet() || User.HasFocusedDescendants(Pinned.ToSharedRef()))
		{
			FSlateApplication::Get().ClearUserFocus(User.GetUserIndex(), EFocusCause::Cleared);
		}
	});
}

FText UConstructStudioDock::PlaceholderText() const
{
	if (State == EConstructStudioDockState::Failed)
	{
		return ReasonText;
	}
	return FText::Format(LOCTEXT("Placeholder", "The Studio dock is not ready yet ({0})."), UEnum::GetDisplayValueAsText(State));
}

bool UConstructStudioDock::StartLiveBoot()
{
	Platform = PlatformForTests.IsValid() ? PlatformForTests : LivePlatform();
	if (!Platform.IsValid())
	{
		return false;
	}
	bLiveBoot = true;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const int64 Booting = Generation;
	Platform->GetAgentBoot([Weak, Booting](const FCrowdyStudioAgentBoot& Boot)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveAgentBoot(Booting, Boot);
		}
	});
	return true;
}

TSharedPtr<FConstructStudioDockPlatform> UConstructStudioDock::LivePlatform()
{
	UCrowdyStudioAgent* Agent = LiveAgent();
	UGameInstance* Game = Agent ? Agent->GetGameInstance() : nullptr;
	if (!Game)
	{
		return nullptr;
	}
	if (BoundAgent.Get() != Agent)
	{
		UnbindAgent();
		BoundAgent = Agent;
		AgentTokenHandle = Agent->OnAgentTokenChanged.AddUObject(this, &UConstructStudioDock::AgentTokenChanged);
		Agent->OnAgentBootChanged.AddDynamic(this, &UConstructStudioDock::AgentBootChanged);
	}
	return FConstructStudioDockPlatform::Live(*Game, GetWorld());
}

UCrowdyStudioAgent* UConstructStudioDock::LiveAgent() const
{
	if (UserForTests.IsSet() || !ConstructStudioDockScheme::IsRegistered())
	{
		return nullptr;
	}
	const UWorld* World = GetWorld();
	const UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
	UCrowdyStudioAgent* Agent = Game ? Game->GetSubsystem<UCrowdyStudioAgent>() : nullptr;
	if (Agent && !ConstructStudioDockScheme::IsStaged())
	{
		UE_LOG(LogConstructStudioDock, Warning, TEXT("The Studio dock's page or harness is missing from %s."), *ConstructStudioDockScheme::ServedRoot());
		return nullptr;
	}
	return Agent;
}

void UConstructStudioDock::UnbindAgent()
{
	if (UCrowdyStudioAgent* Agent = BoundAgent.Get())
	{
		Agent->OnAgentTokenChanged.Remove(AgentTokenHandle);
		Agent->OnAgentBootChanged.RemoveDynamic(this, &UConstructStudioDock::AgentBootChanged);
	}
	BoundAgent.Reset();
	AgentTokenHandle.Reset();
}

void UConstructStudioDock::ReceiveAgentBoot(int64 InGeneration, const FCrowdyStudioAgentBoot& Boot)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Booting || !bLiveBoot || AgentBoot.IsValid() || !Platform.IsValid())
	{
		return;
	}
	// A refusal's message names its cause (no model, no permission, the agent turned off), which a player cannot fix by retrying.
	if (Boot.Result != ECrowdyStudioResult::Success || Boot.Models.IsEmpty())
	{
		const bool bNoModel = Boot.Result == ECrowdyStudioResult::NoModel || Boot.Result == ECrowdyStudioResult::Success;
		Fail(bNoModel ? EConstructStudioDockReason::NoModel : EConstructStudioDockReason::Unavailable, FText::FromString(Boot.Message.Left(200)));
		return;
	}
	AgentBoot = MakeShared<FCrowdyStudioAgentBoot>(Boot);
	StudioOrigin = ConstructStudioDockScheme::NormalizeOrigin(Boot.StudioOrigin);
	WalletUrl = Boot.WalletUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase) ? Boot.WalletUrl : FString();
	ConstructStudioDockScheme::SetConnectOrigins(Boot.ConnectOrigins);
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	Platform->GetProviderConsent([Weak, InGeneration](const FCrowdyStudioConsent& Consent)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveConsent(InGeneration, Consent);
		}
	});
}

void UConstructStudioDock::ReceiveConsent(int64 InGeneration, const FCrowdyStudioConsent& Consent)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Booting || !AgentBoot.IsValid() || Bridge || !Platform.IsValid())
	{
		return;
	}
	if (Consent.Result != ECrowdyStudioResult::Success)
	{
		Fail(EConstructStudioDockReason::Unavailable, FText::FromString(Consent.Message.Left(200)));
		return;
	}
	if (!Consent.bConsented)
	{
		ConsentNotice = Platform->GetConsentNotice();
		SetState(EConstructStudioDockState::NeedsConsent, EConstructStudioDockReason::NeedsConsent);
		return;
	}
	ReadProject();
}

void UConstructStudioDock::ReadProject()
{
	if (!bLiveBoot || !Platform.IsValid())
	{
		return;
	}
	if (bProjectReadInFlight)
	{
		return;
	}
	bProjectReadInFlight = true;
	const TWeakObjectPtr<UConstructStudioDock> Weak(this);
	const int64 Reading = Generation;
	if (CurrentProjectId.IsEmpty())
	{
		Platform->ListProjects([Weak, Reading](const FCrowdyStudioProjectList& List)
		{
			if (UConstructStudioDock* Dock = Weak.Get())
			{
				Dock->ReceiveStudioProject(Reading, List.Result, List.Message, List.Projects.IsEmpty() ? FCrowdyStudioProject() : List.Projects[0]);
			}
		});
		return;
	}
	Platform->GetProject(CurrentProjectId, [Weak, Reading](const FCrowdyStudioProjectInfo& Info)
	{
		if (UConstructStudioDock* Dock = Weak.Get())
		{
			Dock->ReceiveStudioProject(Reading, Info.Result, Info.Message, Info.Project);
		}
	});
}

void UConstructStudioDock::ReceiveStudioProject(int64 InGeneration, ECrowdyStudioResult Result, const FString& Message, const FCrowdyStudioProject& Project)
{
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	bProjectReadInFlight = false;
	// The boot's own read, before the page exists, decides which project the page opens.
	const bool bBootRead = State == EConstructStudioDockState::Booting && !Bridge;
	if (bBootRead && Result != ECrowdyStudioResult::Success)
	{
		Fail(EConstructStudioDockReason::Unavailable, FText::FromString(Message.Left(200)));
		return;
	}
	const bool bApplies = bBootRead || Project.ProjectId.Equals(CurrentProjectId, ESearchCase::CaseSensitive);
	if (Result == ECrowdyStudioResult::Success && bApplies && !Project.ProjectId.IsEmpty())
	{
		CurrentServerModule = Project.ServerModule;
		CurrentClientModule = Project.ClientModule;
		ReceiveProject(InGeneration, Project.ProjectId, Project.Name, Project.Revision, !Project.ClientModule.IsEmpty());
	}
	if (Pending.PromptId != 0 && !Pending.Revision.Equals(CurrentRevision, ESearchCase::CaseSensitive))
	{
		WithdrawPrompt(TEXT("Refused"));
	}
	if (bBootRead)
	{
		ShowPage();
		return;
	}
	if (bDeployReading)
	{
		ContinueDeploy(Result, Message);
	}
}

void UConstructStudioDock::ShowPage()
{
	if (!IsCurrent(Generation) || State != EConstructStudioDockState::Booting || Bridge)
	{
		return;
	}
	Bridge = NewObject<UConstructStudioDockBridge>(this);
	Bridge->Dock = this;
	ShowBrowser();
}

void UConstructStudioDock::ReceiveWorkerFileChanged(int64 InGeneration, const FJsonObject& Payload)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Ready || !Payload.HasTypedField<EJson::String>(TEXT("path"))
		|| !Payload.HasTypedField<EJson::String>(TEXT("target")))
	{
		return;
	}
	const FString Path = Payload.GetStringField(TEXT("path"));
	const FString Target = Payload.GetStringField(TEXT("target"));
	// The page's events are untrusted: only a half's file the project can hold is listed, at most two full crates' files.
	const bool bHalf = Target.Equals(TEXT("SERVER"), ESearchCase::CaseSensitive) || Target.Equals(TEXT("CLIENT"), ESearchCase::CaseSensitive);
	if (!bHalf || Path.Len() > 256 || !ConstructStudioDockReplies::IsReadablePath(Path))
	{
		return;
	}
	if (ChangedFiles.Num() < 2 * UCrowdyMods::MaxFiles)
	{
		ChangedFiles.AddUnique(Target / Path);
	}
	if (!CurrentProjectId.IsEmpty())
	{
		bProjectDirty = true;
		SaveQuietAge = 0.0;
	}
}

void UConstructStudioDock::AgentTokenChanged(int64 UserId, const FString& Token)
{
	// A sign-out or another player's sign-in closes the dock at once, not on the next tick.
	if (State == EConstructStudioDockState::Closed || CloseIfPlayerChanged() || !Channel.IsValid() || UserId != BootUser)
	{
		return;
	}
	Channel->Token(Generation, Token);
}

void UConstructStudioDock::AgentBootChanged()
{
	// Before the material arrives, the read in flight already carries the new addresses.
	if (!bLiveBoot || !AgentBoot.IsValid() || State == EConstructStudioDockState::Closed || State == EConstructStudioDockState::Failed)
	{
		return;
	}
	const int64 Restarting = Generation;
	WithdrawPrompt(TEXT("Refused"));
	RefuseCaptures(TEXT("restarted"));
	RefuseHeldRequests(TEXT("The Studio agent is restarting."));
	TellAgent(Generation, TEXT("page.bye"), MakeShared<FJsonObject>());
	if (!IsCurrent(Restarting))
	{
		return;
	}
	ForgetDeploy();
	bRunInFlight = false;
	bProjectReadInFlight = false;
	bProjectDirty = false;
	bUsageInFlight = false;
	ReleasePage();
	++Generation;
	SetState(EConstructStudioDockState::Booting, EConstructStudioDockReason::None);
	if (IsCurrent(Generation) && !StartLiveBoot())
	{
		Fail(EConstructStudioDockReason::Unavailable, FText::GetEmpty());
	}
}

FString UConstructStudioDock::AgentToken() const
{
	if (TokenForTests.IsSet())
	{
		return TokenForTests.GetValue();
	}
	const UCrowdyStudioAgent* Agent = BoundAgent.Get();
	return Agent ? Agent->GetAgentToken() : FString();
}

FString UConstructStudioDock::BridgeBoot(const UConstructStudioDockBridge& Caller)
{
	if (Bridge.Get() != &Caller || State == EConstructStudioDockState::Closed || !AgentBoot.IsValid() || !Channel.IsValid())
	{
		return ConstructStudioDockFrames::ErrorReply(TEXT("closed"));
	}
	if (!IsMainFrameOnOrigin())
	{
		return ConstructStudioDockFrames::ErrorReply(TEXT("off origin"));
	}
	// Each load of the page boots a fresh harness, so it gets a fresh nonce and channel.
	const FString Nonce = ConstructStudioDockFrames::NewSecret();
	const FString ChannelName = TEXT("crowdy-dsh-") + ConstructStudioDockFrames::NewSecret();
	Channel->Begin(Generation, Nonce);
	return ConstructStudioDockFrames::BootReply(*AgentBoot, CurrentProjectId, ConstructStudioDockFrames::PersistScope(AgentBoot->AppId, BootUser), ChannelName, Nonce);
}

FString UConstructStudioDock::BridgeSend(const UConstructStudioDockBridge& Caller, const FString& Frame)
{
	if (Bridge.Get() != &Caller || State == EConstructStudioDockState::Closed || !Channel.IsValid())
	{
		return ConstructStudioDockFrames::ErrorReply(TEXT("closed"));
	}
	if (!IsMainFrameOnOrigin())
	{
		return ConstructStudioDockFrames::ErrorReply(TEXT("off origin"));
	}
	ReceivePageFrame(Frame);
	return ConstructStudioDockFrames::OkReply();
}

void UConstructStudioDock::ReceivePageFrame(const FString& Json)
{
	const TOptional<FConstructStudioDockFrame> Frame = Channel->Parse(Json);
	if (!Frame.IsSet())
	{
		return;
	}
	const int64 FrameGeneration = Channel->GetGeneration();
	switch (Frame->Kind)
	{
	case FConstructStudioDockFrame::EKind::Request:
		ReceiveAgentRequest(FrameGeneration, Frame->Name, Frame->Id, Frame->Body);
		return;
	case FConstructStudioDockFrame::EKind::WorkerEvent:
		ReceiveWorkerEvent(FrameGeneration, Frame->Name, Frame->Body);
		return;
	case FConstructStudioDockFrame::EKind::PageNotice:
		ReceivePageNotice(FrameGeneration, Frame->Name, Frame->Body);
		return;
	}
}

void UConstructStudioDock::ReceivePageNotice(int64 InGeneration, const FString& Notice, const TSharedPtr<FJsonObject>& Payload)
{
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	if (Notice.Equals(TEXT("joined"), ESearchCase::CaseSensitive))
	{
		Channel->SetPageJoined(InGeneration);
		SendHello(InGeneration);
		return;
	}
	if (Notice.Equals(TEXT("booted"), ESearchCase::CaseSensitive))
	{
		ReceiveBootOutcome(InGeneration, EConstructStudioDockState::Ready, EConstructStudioDockReason::None, FText::GetEmpty());
		return;
	}
	if (!Notice.Equals(TEXT("failed"), ESearchCase::CaseSensitive))
	{
		return;
	}
	FString Message;
	if (Payload.IsValid())
	{
		Payload->TryGetStringField(TEXT("message"), Message);
	}
	ReceiveBootOutcome(InGeneration, EConstructStudioDockState::Failed, EConstructStudioDockReason::HarnessFailed, FText::FromString(Message.Left(200)));
}

void UConstructStudioDock::ReceiveWorkerEvent(int64 InGeneration, const FString& Event, const TSharedPtr<FJsonObject>& Payload)
{
	if (!IsCurrent(InGeneration))
	{
		return;
	}
	// The worker sends ready when it starts and after each page.hello; the hello sent on joined may arrive before it starts.
	if (Event.Equals(TEXT("worker.ready"), ESearchCase::CaseSensitive))
	{
		if (Channel.IsValid() && Channel->GreetReady(InGeneration))
		{
			SendHello(InGeneration);
			TellGridContext(InGeneration);
		}
		return;
	}
	// The event names a saved file but not the project's new revision, which is read back from the platform.
	if (Event.Equals(TEXT("worker.fileChanged"), ESearchCase::CaseSensitive) && Payload.IsValid())
	{
		ReceiveWorkerFileChanged(InGeneration, *Payload);
		return;
	}
	FString Message;
	if (Event.Equals(TEXT("worker.warning"), ESearchCase::CaseSensitive) && Payload.IsValid() && Payload->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
	{
		ReceiveWarning(InGeneration, FText::FromString(Message.Left(500)));
	}
}

bool UConstructStudioDock::ReplyToAgent(int64 InGeneration, const FString& Id, const TSharedRef<FJsonObject>& Result)
{
	return IsCurrent(InGeneration) && Channel.IsValid() && Channel->Reply(InGeneration, Id, Result);
}

bool UConstructStudioDock::RefuseToAgent(int64 InGeneration, const FString& Id, EConstructStudioDockRefusal Why, const FString& Message)
{
	return IsCurrent(InGeneration) && Channel.IsValid() && Channel->Refuse(InGeneration, Id, Why, Message);
}

bool UConstructStudioDock::TellAgent(int64 InGeneration, const FString& Event, const TSharedRef<FJsonObject>& Payload)
{
	return IsCurrent(InGeneration) && Channel.IsValid() && Channel->Tell(InGeneration, Event, Payload);
}

void UConstructStudioDock::SendHello(int64 InGeneration)
{
	bool bAccessEnded = false;
	if (!IsCurrent(InGeneration) || !Channel.IsValid() || !AgentBoot.IsValid() || SignedInUser(bAccessEnded) != BootUser)
	{
		return;
	}
	Channel->Hello(InGeneration, LexToString(AgentBoot->AppId), CurrentProjectId, AgentToken());
}

void UConstructStudioDock::DeliverToPage(const FString& FrameJson)
{
	if (!IsMainFrameOnOrigin())
	{
		return;
	}
	// A frame may carry a whole screenshot, so the script is built in one allocation.
	FString Script;
	Script.Reserve(FrameJson.Len() + 64);
	Script += TEXT("window.constructStudioDock&&window.constructStudioDock.deliver(");
	Script += FrameJson;
	Script += TEXT(");");
	if (ScriptsForTests)
	{
		ScriptsForTests(Script);
		return;
	}
	if (Browser.IsValid())
	{
		Browser->ExecuteJavascript(Script);
	}
}

bool UConstructStudioDock::IsMainFrameOnOrigin() const
{
	if (MainFrameUrlForTests.IsSet())
	{
		return ConstructStudioDockScheme::IsOnOrigin(MainFrameUrlForTests.GetValue());
	}
	return Browser.IsValid() && ConstructStudioDockScheme::IsOnOrigin(Browser->GetUrl());
}

void UConstructStudioDock::ShowBrowser()
{
	const TSharedPtr<SConstructStudioDockPlaceholder> Host = Placeholder.Pin();
	if (!Host.IsValid() || !Bridge || !AgentBoot.IsValid())
	{
		return;
	}
	if (!Browser.IsValid())
	{
		Browser = SNew(SWebBrowser)
			.InitialURL(ConstructStudioDockScheme::PageUrl)
			.ShowControls(false)
			.ShowAddressBar(false)
			.ShowErrorMessage(false)
			.ShowInitialThrobber(false)
			.SupportsTransparency(false)
			.BackgroundColor(FColor(16, 17, 17))
			.OnLoadStarted(FSimpleDelegate::CreateUObject(this, &UConstructStudioDock::BindBridge))
			.OnLoadCompleted(FSimpleDelegate::CreateUObject(this, &UConstructStudioDock::BindBridge))
			.OnBeforeNavigation(SWebBrowser::FOnBeforeBrowse::CreateUObject(this, &UConstructStudioDock::BrowserNavigating))
			.OnBeforePopup(FOnBeforePopupDelegate::CreateUObject(this, &UConstructStudioDock::BrowserPopup));
	}
	Host->ShowContent(Browser.ToSharedRef());
}

void UConstructStudioDock::BindBridge()
{
	// Not permanent: the browser drops the binding when the page goes, and it is bound again for each load.
	if (!Browser.IsValid() || !Bridge || !IsMainFrameOnOrigin())
	{
		return;
	}
	Browser->BindUObject(ConstructStudioDockFrames::BridgeName, Bridge.Get(), false);
}

void UConstructStudioDock::ReleasePage()
{
	ResetRequests();
	if (Channel.IsValid())
	{
		Channel->End();
	}
	if (Bridge)
	{
		Bridge->Dock.Reset();
		if (Browser.IsValid())
		{
			Browser->UnbindUObject(ConstructStudioDockFrames::BridgeName, Bridge.Get(), false);
		}
	}
	Bridge = nullptr;
	if (const TSharedPtr<SConstructStudioDockPlaceholder> Host = Placeholder.Pin())
	{
		Host->ShowText();
	}
	Browser.Reset();
	if (AgentBoot.IsValid())
	{
		ConstructStudioDockScheme::SetConnectOrigins(TArray<FString>());
	}
	AgentBoot.Reset();
}

bool UConstructStudioDock::BrowserNavigating(const FString& Url, const FWebNavigationRequest& Request)
{
	const ConstructStudioDockScheme::ENavigation Decision = ConstructStudioDockScheme::Navigation(Url, Request.bIsMainFrame, StudioOrigin);
	if (Decision == ConstructStudioDockScheme::ENavigation::OpenOutside)
	{
		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
	}
	return Decision != ConstructStudioDockScheme::ENavigation::Allow;
}

bool UConstructStudioDock::BrowserPopup(FString Url, FString Frame)
{
	if (ConstructStudioDockScheme::Popup(Url, StudioOrigin) == ConstructStudioDockScheme::ENavigation::OpenOutside)
	{
		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
