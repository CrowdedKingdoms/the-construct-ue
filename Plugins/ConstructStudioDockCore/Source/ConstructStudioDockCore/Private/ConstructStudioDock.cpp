#include "ConstructStudioDock.h"

#include "ConstructStudioCaptureRequest.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructStudioDock"

/** The dock's slot until the agent's page draws there: takes keyboard focus on a click, reports Escape and its own end. */
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
		ChildSlot
		[
			SNew(SBorder)
			.Padding(FMargin(16.f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InArgs._Text)
				.AutoWrapText(true)
			]
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

	virtual FReply OnFocusReceived(const FGeometry& Geometry, const FFocusEvent& Event) override
	{
		FocusChanged(true);
		return FReply::Handled();
	}

	virtual void OnFocusLost(const FFocusEvent& Event) override
	{
		FocusChanged(false);
	}

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
	bCurrentReachesVisitors = false;
	bProjectRead = false;
	ChangedFiles.Reset();
	Pending = FConstructStudioDeployPrompt();
	ConsentNotice = FText::GetEmpty();
	bRunInFlight = false;
	bBootReplayed = false;
	bFocusReported = false;
	OpenDock = this;
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UConstructStudioDock::Advance));
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UConstructStudioDock::WorldCleanup);
	SetState(EConstructStudioDockState::Booting, EConstructStudioDockReason::None);
	Send(TEXT("Open"), LexToString(GridId));
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
	PendingCaptures.Reset();
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
}

void UConstructStudioDock::AnswerDeployPrompt(int64 PromptId, bool bRun)
{
	if (PromptId == 0 || PromptId != Pending.PromptId)
	{
		return;
	}
	if (!bRun)
	{
		Pending = FConstructStudioDeployPrompt();
		Send(TEXT("AnswerDeployPrompt"), TEXT("NotNow"));
		return;
	}
	if (Pending.GridId != CurrentGridId || !Pending.ProjectId.Equals(CurrentProjectId, ESearchCase::CaseSensitive)
		|| !Pending.Revision.Equals(CurrentRevision, ESearchCase::CaseSensitive))
	{
		WithdrawPrompt(TEXT("Refused"));
		return;
	}
	Pending = FConstructStudioDeployPrompt();
	bRunInFlight = true;
	Send(TEXT("AnswerDeployPrompt"), TEXT("Run"));
}

void UConstructStudioDock::OpenWallet()
{
	if (State == EConstructStudioDockState::Closed)
	{
		return;
	}
	// The wallet URL arrives with the agent's boot material, which this build does not read yet.
	Send(TEXT("OpenWallet"), TEXT("no wallet URL"));
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

void UConstructStudioDock::ReceiveDeployRequest(int64 InGeneration, EConstructStudioDeployKind Kind)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Ready)
	{
		return;
	}
	const TCHAR* Message = Kind == EConstructStudioDeployKind::Live ? TEXT("studio.deployLive") : TEXT("studio.draftTest");
	if (Pending.PromptId != 0 || bRunInFlight)
	{
		Send(Message, TEXT("refused: busy"));
		return;
	}
	// A project id passed to Open is not enough: the prompt needs the revision the platform read for it.
	if (!bProjectRead || CurrentRevision.IsEmpty())
	{
		Send(Message, TEXT("refused: no project"));
		return;
	}
	Pending.PromptId = ++LastPromptId;
	Pending.Kind = Kind;
	Pending.GridId = CurrentGridId;
	Pending.GridLabel = CurrentGridLabel;
	Pending.ProjectId = CurrentProjectId;
	Pending.ProjectName = CurrentProjectName;
	Pending.Revision = CurrentRevision;
	Pending.bReachesVisitors = bCurrentReachesVisitors;
	Pending.ChangedFiles = ChangedFiles;
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

void UConstructStudioDock::ReceiveScreenshotRequest(int64 InGeneration, const FString& Label)
{
	if (!IsCurrent(InGeneration) || State != EConstructStudioDockState::Ready)
	{
		return;
	}
	RequestCapture(Label, false);
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
}

bool UConstructStudioDock::IsCurrent(int64 InGeneration) const
{
	return InGeneration == Generation && State != EConstructStudioDockState::Closed;
}

bool UConstructStudioDock::Advance(float DeltaSeconds)
{
	const int64 Ticking = Generation;
	bool bAccessEnded = false;
	const int64 User = SignedInUser(bAccessEnded);
	if (User != BootUser)
	{
		CloseFor(bAccessEnded ? EConstructStudioDockReason::AccessEnded
			: User <= 0 ? EConstructStudioDockReason::SignedOut
			: EConstructStudioDockReason::UserChanged);
		return false;
	}
	if (State == EConstructStudioDockState::Booting && !bBootReplayed)
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
	if (Pending.PromptId != 0)
	{
		PromptAge += DeltaSeconds;
		if (PromptAge >= PromptSeconds)
		{
			WithdrawPrompt(TEXT("NotNow"));
		}
	}
	return IsCurrent(Ticking);
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
	bRunInFlight = false;
	if (IsCurrent(Failing))
	{
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
}

void UConstructStudioDock::RefuseCaptures(const TCHAR* Why)
{
	const TArray<TObjectPtr<UConstructStudioCaptureRequest>> Refused = MoveTemp(PendingCaptures);
	PendingCaptures.Reset();
	for (const UConstructStudioCaptureRequest* Request : Refused)
	{
		Send(Request->bShared ? TEXT("page.capture") : TEXT("studio.screenshot"), FString::Printf(TEXT("refused: %s"), Why));
	}
}

void UConstructStudioDock::RequestCapture(const FString& Label, bool bShared)
{
	const TCHAR* Message = bShared ? TEXT("page.capture") : TEXT("studio.screenshot");
	UObject* Host = HostObject.Get();
	if (!Host)
	{
		Send(Message, TEXT("refused: no host"));
		return;
	}
	UConstructStudioCaptureRequest* Request = NewObject<UConstructStudioCaptureRequest>(this);
	Request->Dock = this;
	Request->Generation = Generation;
	Request->bShared = bShared;
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
	return Pinned.IsValid() && FSlateApplication::IsInitialized() && Pinned->HasAnyUserFocus().IsSet();
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
		if (User.HasFocus(Pinned).IsSet())
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

#undef LOCTEXT_NAMESPACE
