#include "ConstructLayer11Check.h"

#include "Blueprint/GameViewportSubsystem.h"
#include "ConstructHarnessLog.h"
#include "ConstructStudioDock.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"

namespace
{
	constexpr double Layer11SignalSeconds = 0.25;
	constexpr int32 Layer11MaxProjectIdChars = 64;
	constexpr int32 Layer11MaxGridIdDigits = 19;
	constexpr double Layer11MaxSeq = 1.0e9;

	FString Layer11EnumName(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameStringByValue(Value) : FString::Printf(TEXT("%lld"), Value);
	}
}

bool AConstructLayer11CheckPawn::ShouldUseLocalCharacterMovement() const
{
	return false;
}

AConstructLayer11CheckGameMode::AConstructLayer11CheckGameMode()
{
	DefaultPawnClass = AConstructLayer11CheckPawn::StaticClass();
}

FConstructStudioCapture UConstructLayer11CheckHost::SolidCapture(const FString& Label)
{
	FConstructStudioCapture Capture;
	IImageWrapperModule* Wrapper = FModuleManager::LoadModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (!Wrapper)
	{
		Capture.Refusal = TEXT("the check cannot encode an image");
		return Capture;
	}
	TArray<FColor> Pixels;
	Pixels.Init(FColor(40, 120, 200), ImageWidth * ImageHeight);
	TArray64<uint8> Png;
	if (!Wrapper->CompressImage(Png, EImageFormat::PNG, FImageView(Pixels.GetData(), ImageWidth, ImageHeight)) || Png.IsEmpty())
	{
		Capture.Refusal = TEXT("the check cannot encode an image");
		return Capture;
	}
	Capture.bCaptured = true;
	Capture.PngBase64 = FBase64::Encode(Png.GetData(), static_cast<uint32>(Png.Num()));
	Capture.Width = ImageWidth;
	Capture.Height = ImageHeight;
	Capture.Caption = Label;
	return Capture;
}

void UConstructLayer11CheckHost::CaptureView_Implementation(const FString& Label, const FOnConstructStudioCaptured& OnDone)
{
	++Captures;
	// Answered on a later tick, never inside the dock's own call; the dock drops an answer from a closed boot.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([OnDone, Answer = SolidCapture(Label)](float)
	{
		OnDone.ExecuteIfBound(Answer);
		return false;
	}));
}

bool UConstructLayer11CheckSubsystem::IsValidGridId(const FString& Text, int64& OutGridId)
{
	if (Text.IsEmpty() || Text.Len() > Layer11MaxGridIdDigits)
	{
		return false;
	}
	for (const TCHAR Char : Text)
	{
		if (Char < TEXT('0') || Char > TEXT('9'))
		{
			return false;
		}
	}
	const int64 Parsed = FCString::Strtoi64(*Text, nullptr, 10);
	if (Parsed <= 0 || LexToString(Parsed) != Text)
	{
		return false;
	}
	OutGridId = Parsed;
	return true;
}

bool UConstructLayer11CheckSubsystem::IsValidProjectId(const FString& Id)
{
	if (Id.IsEmpty() || Id.Len() > Layer11MaxProjectIdChars)
	{
		return false;
	}
	for (const TCHAR Char : Id)
	{
		const bool bDigit = Char >= TEXT('0') && Char <= TEXT('9');
		const bool bLetter = (Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('A') && Char <= TEXT('Z'));
		if (!bDigit && !bLetter && Char != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

FText UConstructLayer11CheckSubsystem::GridLabelFor(const int64 ChunkX, const int64 ChunkY, const int64 ChunkZ)
{
	return FText::FromString(FString::Printf(TEXT("Chunk %lld, %lld, %lld"), ChunkX, ChunkY, ChunkZ));
}

FVector UConstructLayer11CheckSubsystem::SpotFor(const double ChunkSize)
{
	return FVector(CheckChunkX + 0.5, CheckChunkY + 0.5, CheckChunkZ + 0.5) * ChunkSize;
}

EConstructLayer11Step UConstructLayer11CheckSubsystem::StepFor(const EConstructStudioDockState State, const EConstructStudioDockReason Reason,
                                                               const bool bInMayConsent, const bool bInConsentGiven)
{
	switch (State)
	{
	case EConstructStudioDockState::Booting:
		return EConstructLayer11Step::Wait;
	case EConstructStudioDockState::Ready:
		return EConstructLayer11Step::Ready;
	case EConstructStudioDockState::NeedsConsent:
		if (bInConsentGiven)
		{
			return EConstructLayer11Step::Wait;
		}
		return bInMayConsent ? EConstructLayer11Step::Consent : EConstructLayer11Step::NotRun;
	case EConstructStudioDockState::Failed:
		return Reason == EConstructStudioDockReason::NoModel || Reason == EConstructStudioDockReason::NotBuiltYet ? EConstructLayer11Step::NotRun
		                                                                                                          : EConstructLayer11Step::Failed;
	default:
		return EConstructLayer11Step::Failed;
	}
}

FString UConstructLayer11CheckSubsystem::NotRunReason(const EConstructStudioDockState State, const EConstructStudioDockReason Reason)
{
	if (State == EConstructStudioDockState::NeedsConsent)
	{
		return TEXT("provider consent is not recorded and the check may not record it");
	}
	if (Reason == EConstructStudioDockReason::NoModel)
	{
		return TEXT("the dock found no model for the agent");
	}
	if (Reason == EConstructStudioDockReason::NotBuiltYet)
	{
		return TEXT("the dock's page or browser is not in this package");
	}
	return FString::Printf(TEXT("the dock ended %s %s"), *StateName(State), *ReasonName(Reason));
}

bool UConstructLayer11CheckSubsystem::ParseSignal(const FString& Json, const int64 InLastSeq, FConstructLayer11Signal& OutSignal)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return false;
	}
	FString KindText;
	// Typed, because a JSON string of digits would otherwise read as a number.
	if (!Object->HasTypedField<EJson::Number>(TEXT("seq")) || !Object->TryGetStringField(TEXT("kind"), KindText))
	{
		return false;
	}
	const double SeqValue = Object->GetNumberField(TEXT("seq"));
	if (SeqValue != FMath::FloorToDouble(SeqValue) || SeqValue <= static_cast<double>(InLastSeq) || SeqValue > Layer11MaxSeq)
	{
		return false;
	}
	FConstructLayer11Signal Signal;
	Signal.Seq = static_cast<int64>(SeqValue);
	bool bKnown = false;
	for (const FConstructLayer11Signal::EKind Kind : {FConstructLayer11Signal::EKind::Turn, FConstructLayer11Signal::EKind::Draft, FConstructLayer11Signal::EKind::Close})
	{
		if (KindText.Equals(SignalKindName(Kind), ESearchCase::CaseSensitive))
		{
			Signal.Kind = Kind;
			bKnown = true;
		}
	}
	if (!bKnown)
	{
		return false;
	}
	if (Signal.Kind != FConstructLayer11Signal::EKind::Close)
	{
		if (!Object->TryGetStringField(TEXT("text"), Signal.Text) || Signal.Text.TrimStartAndEnd().IsEmpty() || Signal.Text.Len() > MaxPromptChars)
		{
			return false;
		}
	}
	OutSignal = MoveTemp(Signal);
	return true;
}

const TCHAR* UConstructLayer11CheckSubsystem::SignalKindName(const FConstructLayer11Signal::EKind Kind)
{
	switch (Kind)
	{
	case FConstructLayer11Signal::EKind::Turn:
		return TEXT("turn");
	case FConstructLayer11Signal::EKind::Draft:
		return TEXT("draft");
	default:
		return TEXT("close");
	}
}

FString UConstructLayer11CheckSubsystem::StateName(const EConstructStudioDockState State)
{
	return Layer11EnumName(StaticEnum<EConstructStudioDockState>(), static_cast<int64>(State));
}

FString UConstructLayer11CheckSubsystem::ReasonName(const EConstructStudioDockReason Reason)
{
	return Layer11EnumName(StaticEnum<EConstructStudioDockReason>(), static_cast<int64>(Reason));
}

FString UConstructLayer11CheckSubsystem::DeployKindName(const EConstructStudioDeployKind Kind)
{
	return Layer11EnumName(StaticEnum<EConstructStudioDeployKind>(), static_cast<int64>(Kind));
}

FString UConstructLayer11CheckSubsystem::ResultToJson(const FConstructLayer11Result& In)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("outcome"), In.Outcome);
	Root->SetStringField(TEXT("problem"), In.Problem);
	Root->SetNumberField(TEXT("openedAt"), In.OpenedAt);
	Root->SetNumberField(TEXT("readyAt"), In.ReadyAt);
	Root->SetBoolField(TEXT("consentGiven"), In.bConsentGiven);
	Root->SetNumberField(TEXT("captures"), In.Captures);
	Root->SetNumberField(TEXT("withdrawn"), In.Withdrawn);

	TArray<TSharedPtr<FJsonValue>> States;
	for (const FConstructLayer11Result::FState& Entry : In.States)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("state"), Entry.State);
		Item->SetStringField(TEXT("reason"), Entry.Reason);
		Item->SetNumberField(TEXT("at"), Entry.At);
		States.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("states"), States);

	TArray<TSharedPtr<FJsonValue>> Prompts;
	for (const FConstructLayer11Result::FPrompt& Entry : In.Prompts)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("seq"), static_cast<double>(Entry.Seq));
		Item->SetStringField(TEXT("kind"), Entry.Kind);
		Item->SetBoolField(TEXT("sent"), Entry.bSent);
		Item->SetNumberField(TEXT("at"), Entry.At);
		Prompts.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("prompts"), Prompts);

	TArray<TSharedPtr<FJsonValue>> Deploys;
	for (const FConstructLayer11Result::FDeploy& Entry : In.DeployPrompts)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("kind"), Entry.Kind);
		Item->SetStringField(TEXT("answer"), Entry.Answer);
		Item->SetNumberField(TEXT("at"), Entry.At);
		Item->SetNumberField(TEXT("changedFiles"), Entry.ChangedFiles);
		Item->SetBoolField(TEXT("reachesVisitors"), Entry.bReachesVisitors);
		Item->SetBoolField(TEXT("gridMatches"), Entry.bGridMatches);
		Item->SetBoolField(TEXT("projectMatches"), Entry.bProjectMatches);
		Item->SetBoolField(TEXT("runInFlight"), Entry.bRunInFlight);
		Item->SetBoolField(TEXT("watched"), Entry.bWatched);
		Deploys.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("deployPrompts"), Deploys);

	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Root, Writer);
	return Text;
}

bool UConstructLayer11CheckSubsystem::WatchDeploy(FConstructLayer11Result::FDeploy& Entry, const double Now, const bool bRunInFlight)
{
	if (Entry.bWatched)
	{
		return false;
	}
	const bool bNewRun = bRunInFlight && !Entry.bRunInFlight;
	const bool bOver = Now - Entry.At >= RunWatchSeconds;
	Entry.bRunInFlight |= bRunInFlight;
	Entry.bWatched = bOver;
	return bNewRun || bOver;
}

bool UConstructLayer11CheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	// Game worlds only: in PIE the forced exit would end the editor.
	const UWorld* World = Cast<UWorld>(Outer);
	if (!Super::ShouldCreateSubsystem(Outer) || !World || World->WorldType != EWorldType::Game)
	{
		return false;
	}
	FString GridText;
	return FParse::Value(FCommandLine::Get(), TEXT("Layer11CheckGrid="), GridText);
#endif
}

void UConstructLayer11CheckSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	UGameInstance* GameInstance = InWorld.GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	FString GridText;
	if (!FParse::Value(FCommandLine::Get(), TEXT("Layer11CheckGrid="), GridText))
	{
		return;
	}
	const FString LogDir = FPaths::GetPath(FPlatformOutputDevices::GetAbsoluteLogFilename());
	ResultPath = LogDir / TEXT("layer11-result.json");
	SignalPath = LogDir / TEXT("layer11-signal.json");
	StartedAt = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), 0.0f);

	// Started for the check but in another world, as when the build did not cook L_Layer11Check.
	if (!Cast<AConstructLayer11CheckGameMode>(InWorld.GetAuthGameMode()))
	{
		Fail(FString::Printf(TEXT("the build loaded %s without the layer 11 check GameMode"), *InWorld.GetMapName()));
		return;
	}
	double TimeoutMinutes = 0.0;
	if (FParse::Value(FCommandLine::Get(), TEXT("Layer11CheckTimeoutMinutes="), TimeoutMinutes) && TimeoutMinutes > 0.0)
	{
		TimeoutSeconds = TimeoutMinutes * 60.0;
	}
	if (!IsValidGridId(GridText, GridId))
	{
		Fail(TEXT("-Layer11CheckGrid= is not a grid id; start the check with Tools/Layer11Check"));
		return;
	}
	if (!FParse::Value(FCommandLine::Get(), TEXT("Layer11CheckProject="), ProjectId) || !IsValidProjectId(ProjectId))
	{
		Fail(TEXT("-Layer11CheckProject= is not a project id; start the check with Tools/Layer11Check"));
		return;
	}
	bMayConsent = FParse::Param(FCommandLine::Get(), TEXT("Layer11CheckConsent"));
	IFileManager::Get().Delete(*SignalPath, false, true, true);
	Host = NewObject<UConstructLayer11CheckHost>(this);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: run, consent %s"), bMayConsent ? TEXT("allowed") : TEXT("not allowed"));
	WriteResult();
}

void UConstructLayer11CheckSubsystem::Deinitialize()
{
	// A world that ends mid-check (a quit, a map travel) is a failure, never the graceful exit's 0.
	if (TickerHandle.IsValid() && IsLogging())
	{
		Fail(TEXT("the check's world ended before it finished"));
	}
	ReleaseDock();
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	Super::Deinitialize();
}

bool UConstructLayer11CheckSubsystem::Tick(float DeltaTime)
{
	if (!IsLogging())
	{
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds)
	{
		Fail(FString::Printf(TEXT("timed out after %.0f minutes in stage %d"), TimeoutSeconds / 60.0, static_cast<int32>(Stage)));
		return false;
	}
	switch (Stage)
	{
	case EStage::WaitEntry:
		TickEntry(Now);
		break;
	case EStage::WaitDock:
		TickDock(Now);
		break;
	case EStage::Ready:
		TickReady(Now);
		break;
	default:
		break;
	}
	return IsLogging();
}

void UConstructLayer11CheckSubsystem::TickEntry(const double Now)
{
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = GameInstance ? GameInstance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdySDKSubsystem* SDK = GameInstance ? GameInstance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	if (!Auth || !SDK)
	{
		Fail(TEXT("the Crowdy SDK subsystems are missing"));
		return;
	}
	if (!FindLocalPawn() || !Auth->IsSignedIn() || SDK->GetUDPConnectionState() != EUDPConnectionState::Connected)
	{
		return;
	}
	if (!MoveToCheckChunk())
	{
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: signed in, connected, standing in chunk S"));
	OpenDock(Now);
}

void UConstructLayer11CheckSubsystem::OpenDock(const double Now)
{
	Dock = NewObject<UConstructStudioDock>(this);
	Dock->SetHost(TScriptInterface<IConstructStudioDockHost>(Host.Get()));
	Dock->OnStateChanged.AddUniqueDynamic(this, &ThisClass::HandleStateChanged);
	Dock->OnDeployPrompt.AddUniqueDynamic(this, &ThisClass::HandleDeployPrompt);
	Dock->OnDeployPromptWithdrawn.AddUniqueDynamic(this, &ThisClass::HandleDeployPromptWithdrawn);
	UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get();
	if (!Viewport || !Viewport->AddWidget(Dock, FGameViewportWidgetSlot()))
	{
		Fail(TEXT("the dock could not be put on screen"));
		return;
	}
	OpenedAt = Now;
	Result.OpenedAt = Elapsed(Now);
	const EConstructStudioDockReason Refusal = Dock->Open(GridId, GridLabelFor(CheckChunkX, CheckChunkY, CheckChunkZ), ProjectId);
	if (Refusal != EConstructStudioDockReason::None)
	{
		Fail(FString::Printf(TEXT("Open was refused: %s"), *ReasonName(Refusal)));
		return;
	}
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: opened"));
	Stage = EStage::WaitDock;
	WriteResult();
}

void UConstructLayer11CheckSubsystem::TickDock(const double Now)
{
	const EConstructStudioDockState State = Dock->GetState();
	const EConstructStudioDockReason Reason = Dock->GetReason();
	switch (StepFor(State, Reason, bMayConsent, Result.bConsentGiven))
	{
	case EConstructLayer11Step::Wait:
		if (Now - OpenedAt > DockWaitSeconds)
		{
			Fail(FString::Printf(TEXT("the dock was still %s after %.0f s"), *StateName(State), DockWaitSeconds));
		}
		return;
	case EConstructLayer11Step::Consent:
		// Answered here, never inside the state change's own broadcast.
		Result.bConsentGiven = true;
		Dock->GiveProviderConsent();
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: consent given"));
		WriteResult();
		return;
	case EConstructLayer11Step::Ready:
		Result.ReadyAt = Elapsed(Now);
		UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: ready after %.1f s"), Now - OpenedAt);
		Stage = EStage::Ready;
		WriteResult();
		return;
	case EConstructLayer11Step::NotRun:
		NotRun(NotRunReason(State, Reason));
		return;
	default:
		Fail(FString::Printf(TEXT("the dock ended %s %s while booting"), *StateName(State), *ReasonName(Reason)));
	}
}

void UConstructLayer11CheckSubsystem::TickReady(const double Now)
{
	const EConstructStudioDockState State = Dock->GetState();
	if (State != EConstructStudioDockState::Ready)
	{
		Fail(FString::Printf(TEXT("the dock left Ready for %s %s"), *StateName(State), *ReasonName(Dock->GetReason())));
		return;
	}
	if (bHasPendingPrompt)
	{
		AnswerPendingPrompt(Now);
	}
	WatchDeploys(Now);
	if (Now - LastSignalRead < Layer11SignalSeconds)
	{
		return;
	}
	LastSignalRead = Now;
	ReadSignal(Now);
}

void UConstructLayer11CheckSubsystem::ReadSignal(const double Now)
{
	FString Json;
	FConstructLayer11Signal Signal;
	if (!FFileHelper::LoadFileToString(Json, *SignalPath) || !ParseSignal(Json, LastSeq, Signal))
	{
		return;
	}
	LastSeq = Signal.Seq;
	if (Signal.Kind == FConstructLayer11Signal::EKind::Close)
	{
		Finish();
		return;
	}
	bool bSent = false;
#if !UE_BUILD_SHIPPING
	bSent = Dock->SendPrompt(Signal.Text);
#endif
	FConstructLayer11Result::FPrompt& Entry = Result.Prompts.AddDefaulted_GetRef();
	Entry.Seq = Signal.Seq;
	Entry.Kind = SignalKindName(Signal.Kind);
	Entry.bSent = bSent;
	Entry.At = Elapsed(Now);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: prompt %lld %s %s"), Signal.Seq, SignalKindName(Signal.Kind), bSent ? TEXT("sent") : TEXT("refused"));
	WriteResult();
}

void UConstructLayer11CheckSubsystem::AnswerPendingPrompt(const double Now)
{
	bHasPendingPrompt = false;
	FConstructLayer11Result::FDeploy& Entry = Result.DeployPrompts.AddDefaulted_GetRef();
	Entry.Kind = DeployKindName(PendingPrompt.Kind);
	Entry.Answer = TEXT("NotNow");
	Entry.At = Elapsed(Now);
	Entry.ChangedFiles = PendingPrompt.ChangedFiles.Num();
	Entry.bReachesVisitors = PendingPrompt.bReachesVisitors;
	Entry.bGridMatches = PendingPrompt.GridId == GridId;
	Entry.bProjectMatches = PendingPrompt.ProjectId.Equals(ProjectId, ESearchCase::CaseSensitive);
	Dock->AnswerDeployPrompt(PendingPrompt.PromptId, false);
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: deploy prompt %s answered Not now"), *Entry.Kind);
	WriteResult();
}

void UConstructLayer11CheckSubsystem::WatchDeploys(const double Now)
{
	bool bRunInFlight = false;
#if !UE_BUILD_SHIPPING
	bRunInFlight = Dock->IsRunInFlightForChecks();
#endif
	bool bChanged = false;
	for (FConstructLayer11Result::FDeploy& Entry : Result.DeployPrompts)
	{
		bChanged |= WatchDeploy(Entry, Elapsed(Now), bRunInFlight);
	}
	if (bChanged)
	{
		UE_CLOG(bRunInFlight, LogConstructHarness, Warning, TEXT("ConstructLayer11Check: a run is in flight after a deploy prompt was answered Not now"));
		WriteResult();
	}
}

void UConstructLayer11CheckSubsystem::Finish()
{
	bClosing = true;
	ReleaseDock();
	Result.Outcome = TEXT("done");
	Result.Captures = Host ? Host->Captures : 0;
	Stage = EStage::Done;
	WriteResult();
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: done"));
	FPlatformMisc::RequestExitWithStatus(true, 0);
}

void UConstructLayer11CheckSubsystem::ReleaseDock()
{
	if (!Dock)
	{
		return;
	}
	bClosing = true;
	Dock->OnStateChanged.RemoveDynamic(this, &ThisClass::HandleStateChanged);
	Dock->OnDeployPrompt.RemoveDynamic(this, &ThisClass::HandleDeployPrompt);
	Dock->OnDeployPromptWithdrawn.RemoveDynamic(this, &ThisClass::HandleDeployPromptWithdrawn);
	Dock->Close();
	if (UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get())
	{
		Viewport->RemoveWidget(Dock);
	}
	Dock = nullptr;
}

bool UConstructLayer11CheckSubsystem::MoveToCheckChunk()
{
	AConstructLayer11CheckPawn* Pawn = FindLocalPawn();
	const UCrowdyGameSession* Session = GetWorld()->GetGameInstance()->GetSubsystem<UCrowdyGameSession>();
	const double ChunkSize = Session ? Session->GetChunkSize() : 0.0;
	if (!Pawn || ChunkSize <= 0.0)
	{
		Fail(TEXT("the pawn or the session's chunk size is missing"));
		return false;
	}
	FInt64Vector Chunk(0, 0, 0);
	const bool bPlaced = Pawn->SetActorLocation(SpotFor(ChunkSize)) && Session->TryGetLocalPlayerChunk(GetWorld(), Chunk);
	if (!bPlaced || Chunk != FInt64Vector(CheckChunkX, CheckChunkY, CheckChunkZ))
	{
		Fail(TEXT("the pawn could not be placed in chunk S"));
		return false;
	}
	return true;
}

AConstructLayer11CheckPawn* UConstructLayer11CheckSubsystem::FindLocalPawn() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AConstructLayer11CheckPawn* Pawn = Controller ? Cast<AConstructLayer11CheckPawn>(Controller->GetPawn()) : nullptr;
	const UCrowdyEntityComponent* Component = Pawn ? Pawn->FindComponentByClass<UCrowdyEntityComponent>() : nullptr;
	return Component && Component->GetRole() == ECrowdyRole::Owner ? Pawn : nullptr;
}

bool UConstructLayer11CheckSubsystem::IsLogging() const
{
	return Stage != EStage::Done && Stage != EStage::Failed;
}

double UConstructLayer11CheckSubsystem::Elapsed(const double Now) const
{
	return Now - StartedAt;
}

void UConstructLayer11CheckSubsystem::HandleStateChanged(const EConstructStudioDockState State, const EConstructStudioDockReason Reason)
{
	if (!IsLogging() || bClosing)
	{
		return;
	}
	FConstructLayer11Result::FState& Entry = Result.States.AddDefaulted_GetRef();
	Entry.State = StateName(State);
	Entry.Reason = ReasonName(Reason);
	Entry.At = Elapsed(FPlatformTime::Seconds());
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: state %s %s"), *Entry.State, *Entry.Reason);
	WriteResult();
}

void UConstructLayer11CheckSubsystem::HandleDeployPrompt(const FConstructStudioDeployPrompt& Prompt)
{
	if (!IsLogging() || bClosing)
	{
		return;
	}
	// Answered on the next tick, never inside the prompt's own broadcast.
	PendingPrompt = Prompt;
	bHasPendingPrompt = true;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: deploy prompt %s seen"), *DeployKindName(Prompt.Kind));
}

void UConstructLayer11CheckSubsystem::HandleDeployPromptWithdrawn(const int64 PromptId)
{
	if (!IsLogging() || !bHasPendingPrompt || PendingPrompt.PromptId != PromptId)
	{
		return;
	}
	bHasPendingPrompt = false;
	++Result.Withdrawn;
	UE_LOG(LogConstructHarness, Log, TEXT("ConstructLayer11Check: deploy prompt withdrawn before it was answered"));
	WriteResult();
}

void UConstructLayer11CheckSubsystem::WriteResult()
{
	if (ResultPath.IsEmpty())
	{
		return;
	}
	Result.Captures = Host ? Host->Captures : Result.Captures;
	// Written beside the file and moved over it, so a reader never sees half a file.
	const FString Partial = ResultPath + TEXT(".partial");
	if (FFileHelper::SaveStringToFile(ResultToJson(Result), *Partial, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Move(*ResultPath, *Partial, true, true);
	}
}

void UConstructLayer11CheckSubsystem::Fail(const FString& Why)
{
	End(TEXT("failed"), TEXT("FAILED"), Why, 1);
}

void UConstructLayer11CheckSubsystem::NotRun(const FString& Why)
{
	End(TEXT("not run"), TEXT("NOT RUN"), Why, 2);
}

void UConstructLayer11CheckSubsystem::End(const TCHAR* Outcome, const TCHAR* Line, const FString& Why, const int32 ExitCode)
{
	if (!IsLogging())
	{
		return;
	}
	Stage = EStage::Failed;
	Result.Outcome = Outcome;
	Result.Problem = Why;
	WriteResult();
	UE_LOG(LogConstructHarness, Warning, TEXT("ConstructLayer11Check: %s: %s"), Line, *Why);
	// Forced, because a graceful exit returns 0 whatever code it was given; the forced one flushes the log first.
	FPlatformMisc::RequestExitWithStatus(true, ExitCode);
}
