#include "ConstructChatComponent.h"

#include "Components/InputComponent.h"
#include "ConstructUIInputSubsystem.h"
#include "ConstructPresencePawn.h"
#include "Containers/StringConv.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Network/CrowdyCpp/CrowdyCppReplicationSubsystem.h"
#include "Replication/Subsystems/CrowdyEntitySubsystem.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "TimerManager.h"
#include "Widgets/SViewport.h"

namespace
{
const FName ChatModal(TEXT("ConstructChat"));
const FString NearbyReadyStatus(TEXT("Nearby chat reaches players within 4 chunks. Enter to type."));

FString LimitText(const FString& Text, int32 Maximum)
{
	FString Limited = Text.Left(Maximum);
	if (!Limited.IsEmpty())
	{
		const uint32 Last = static_cast<uint32>(Limited[Limited.Len() - 1]);
		if (Last >= 0xD800 && Last <= 0xDBFF)
		{
			Limited.LeftChopInline(1);
		}
	}
	return Limited;
}
}

UConstructChatComponent::UConstructChatComponent()
{
	bAutoActivate = false;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.25f;
}

APlayerController* UConstructChatComponent::LocalController() const
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	return IsValid(Controller) && Controller->IsLocalController() && Controller->GetLocalPlayer() ? Controller
	                                                                                              : nullptr;
}

UCrowdyChannels* UConstructChatComponent::Channels() const
{
	UGameInstance* Game = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return Game ? Game->GetSubsystem<UCrowdyChannels>() : nullptr;
}

UCrowdyProximityChat* UConstructChatComponent::ProximityChat() const
{
	UGameInstance* Game = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return Game ? Game->GetSubsystem<UCrowdyProximityChat>() : nullptr;
}

bool UConstructChatComponent::IsSignedIn() const
{
	const UGameInstance* Game = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyAuthentication* Auth = Game ? Game->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	return LocalController() && Auth && Auth->IsSignedIn() && Session && !Session->HasAccessEnded() &&
	       Session->GetUserID() > 0 && Session->GetAppID() > 0;
}

void UConstructChatComponent::GetCurrentIdentity(int64& OutUserID, int64& OutAppID) const
{
	OutUserID = 0;
	OutAppID = 0;
	const UGameInstance* Game = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	if (Session && IsSignedIn())
	{
		OutUserID = Session->GetUserID();
		OutAppID = Session->GetAppID();
	}
}

bool UConstructChatComponent::HasCurrentIdentity() const
{
	int64 UserID = 0;
	int64 AppID = 0;
	GetCurrentIdentity(UserID, AppID);
	return UserID > 0 && UserID == SessionUserID && AppID == SessionAppID;
}

void UConstructChatComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeLocalRuntime();
}

void UConstructChatComponent::InitializeLocalRuntime()
{
	if (bRuntimeActive || !HasBegunPlay())
	{
		return;
	}
	APlayerController* Controller = LocalController();
	const UWorld* World = GetWorld();
	if (!Controller || !World || !World->IsGameWorld() ||
	    Controller->FindComponentByClass<UConstructChatComponent>() != this)
	{
		return;
	}
	bRuntimeActive = true;
	GetCurrentIdentity(SessionUserID, SessionAppID);
	UGameInstance* Game = World->GetGameInstance();
	RuntimeInstance = Game;
	if (UCrowdyProximityChat* Proximity = Game ? Game->GetSubsystem<UCrowdyProximityChat>() : nullptr)
	{
		Proximity->OnProximityTextReceived.AddUniqueDynamic(this, &ThisClass::ProximityReceived);
		Proximity->OnProximityTextRefused.AddUniqueDynamic(this, &ThisClass::ProximityRefused);
	}
	if (UCrowdyChannels* Service = Channels())
	{
		Service->OnChannelMessageReceived.AddUniqueDynamic(this, &ThisClass::Received);
	}
	if (UCrowdySDKSubsystem* SDK = Game ? Game->GetSubsystem<UCrowdySDKSubsystem>() : nullptr)
	{
		SDK->OnLogin.AddUniqueDynamic(this, &ThisClass::SignedIn);
		SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::SignedOut);
		SDK->OnUDPConnectionSuccess.AddUniqueDynamic(this, &ThisClass::Connected);
	}
	if (UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr)
	{
		Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
	}
	if (UCrowdyCppReplicationSubsystem* Realtime =
	        Game ? Game->GetSubsystem<UCrowdyCppReplicationSubsystem>() : nullptr)
	{
		Realtime->OnSendRefused.AddUniqueDynamic(this, &ThisClass::SendRefused);
	}
	if (!RefreshLocalPresentation())
	{
		SetupAttemptsRemaining = 40;
		SetComponentTickEnabled(true);
	}
}

bool UConstructChatComponent::RefreshLocalPresentation()
{
	APlayerController* Controller = LocalController();
	ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
	if (!bRuntimeActive || !Controller || !Player)
	{
		return false;
	}
	UConstructUIInputSubsystem* Gate = Player->GetSubsystem<UConstructUIInputSubsystem>();
	if (InputGate.Get() != Gate)
	{
		if (UConstructUIInputSubsystem* Previous = InputGate.Get())
		{
			Previous->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::GameplayInputChanged);
		}
		InputGate = Gate;
		if (Gate)
		{
			Gate->OnGameplayInputChanged.AddUniqueDynamic(this, &ThisClass::GameplayInputChanged);
		}
	}
	if (!ChatInput)
	{
		InputController = Controller;
		ChatInput = NewObject<UInputComponent>(Controller);
		ChatInput->Priority = 60;
		ChatInput->RegisterComponent();
		ChatInput->BindKey(EKeys::Enter, IE_Pressed, this, &ThisClass::OpenComposer);
		ChatInput->BindKey(EKeys::F3, IE_Pressed, this, &ThisClass::TogglePanel);
		Controller->PushInputComponent(ChatInput);
	}
	if (!Panel && GetWorld()->GetGameViewport())
	{
		Panel = CreateWidget<UConstructChatPanel>(Controller, UConstructChatPanel::StaticClass());
		if (Panel)
		{
			Panel->SetIsFocusable(true);
		}
	}
	if (Panel && !Panel->IsInViewport())
	{
		Panel->AddToPlayerScreen(20);
	}
	RefreshVisibility();
	return ChatInput && Panel && Panel->IsInViewport() && Gate;
}

void UConstructChatComponent::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(Delta, TickType, TickFunction);
	if (!bRuntimeActive || RefreshLocalPresentation() || --SetupAttemptsRemaining <= 0)
	{
		SetComponentTickEnabled(false);
	}
}

void UConstructChatComponent::RefreshVisibility()
{
	if (Panel)
	{
		const bool bShowPanel = bRuntimeActive && IsSignedIn() && (bShowHistory || bComposerOpen);
		Panel->SetVisibility(bShowPanel ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UConstructChatComponent::TogglePanel()
{
	const UConstructUIInputSubsystem* Gate = InputGate.Get();
	if (!bRuntimeActive || !IsSignedIn() || !Gate || !Gate->CanUseNavigationInput(ChatModal))
	{
		return;
	}
	if (bComposerOpen)
	{
		bShowHistory = false;
		CloseComposer();
	}
	else
	{
		bShowHistory = !bShowHistory;
		RefreshVisibility();
	}
}

void UConstructChatComponent::OpenComposer()
{
	UConstructUIInputSubsystem* Gate = InputGate.Get();
	if (!bRuntimeActive || bComposerOpen || !Panel || !IsSignedIn() || !HasCurrentIdentity() || !Gate ||
	    !Gate->IsGameplayInputAllowed())
	{
		return;
	}
	CancelGameplayFocusRestore();
	bComposerOpen = true;
	RefreshVisibility();
	Gate->SetModal(ChatModal, true, Panel);
}

void UConstructChatComponent::CloseComposer()
{
	const bool bWasOpen = bComposerOpen;
	bComposerOpen = false;
	if (Panel)
	{
		Panel->ClearComposer();
		RefreshVisibility();
	}
	if (UConstructUIInputSubsystem* Gate = InputGate.Get(); bWasOpen && Gate)
	{
		Gate->SetModal(ChatModal, false);
		RequestGameplayFocusRestore();
	}
}

void UConstructChatComponent::CancelGameplayFocusRestore()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameplayFocusTimer);
	}
	GameplayFocusTimer.Invalidate();
}

void UConstructChatComponent::RequestGameplayFocusRestore()
{
	CancelGameplayFocusRestore();
	APlayerController* Controller = LocalController();
	ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
	UWorld* World = GetWorld();
	UConstructUIInputSubsystem* Gate = InputGate.Get();
	if (!bRuntimeActive || !Player || !World || !Panel || !Panel->IsInViewport() || !Gate ||
	    !Gate->IsGameplayInputAllowed() || !HasCurrentIdentity() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	const TSharedPtr<FSlateUser> User = Player->GetSlateUser();
	if (!User)
	{
		return;
	}
	const TWeakObjectPtr<APlayerController> ExpectedController(Controller);
	const TWeakObjectPtr<ULocalPlayer> ExpectedPlayer(Player);
	const TWeakObjectPtr<UConstructChatPanel> ExpectedPanel(Panel.Get());
	const uint64 ExpectedGeneration = SessionGeneration;
	const TWeakPtr<SWidget> PreviousFocus = FSlateApplication::Get().GetUserFocusedWidget(User->GetUserIndex());
	// Editable text and button replies finish changing focus after their callbacks return.
	GameplayFocusTimer = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
	    this,
	    [this, ExpectedController, ExpectedPlayer, ExpectedPanel, ExpectedGeneration, PreviousFocus]()
	    {
		    GameplayFocusTimer.Invalidate();
		    APlayerController* CurrentController = LocalController();
		    ULocalPlayer* CurrentPlayer = CurrentController ? CurrentController->GetLocalPlayer() : nullptr;
		    UWorld* CurrentWorld = GetWorld();
		    UConstructUIInputSubsystem* CurrentGate = InputGate.Get();
		    if (!bRuntimeActive || bComposerOpen || SessionGeneration != ExpectedGeneration ||
		        CurrentController != ExpectedController.Get() || !CurrentPlayer ||
		        CurrentPlayer != ExpectedPlayer.Get() || !CurrentWorld ||
		        CurrentPlayer->GetPlayerController(CurrentWorld) != CurrentController ||
		        Panel.Get() != ExpectedPanel.Get() || !Panel || !Panel->IsInViewport() || !CurrentGate ||
		        !CurrentGate->IsGameplayInputAllowed() || !HasCurrentIdentity() || !FSlateApplication::IsInitialized())
		    {
			    return;
		    }
		    UGameViewportClient* Viewport = CurrentWorld->GetGameViewport();
		    const TSharedPtr<FSlateUser> CurrentUser = CurrentPlayer->GetSlateUser();
		    const TSharedPtr<SViewport> ViewportWidget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		    if (!CurrentUser || !ViewportWidget || CurrentPlayer->ViewportClient != Viewport)
		    {
			    return;
		    }
		    FSlateApplication& Slate = FSlateApplication::Get();
		    const TSharedPtr<SWidget> Focus = Slate.GetUserFocusedWidget(CurrentUser->GetUserIndex());
		    if (Focus && Focus != PreviousFocus.Pin() && Focus != ViewportWidget)
		    {
			    return;
		    }
		    Slate.SetUserFocus(CurrentUser->GetUserIndex(), ViewportWidget, EFocusCause::SetDirectly);
	    }));
}

void UConstructChatComponent::SubmitComposer(const FString& Text)
{
	if (!bComposerOpen)
	{
		return;
	}
	if (InputGate.IsValid() && InputGate->CanUseNavigationInput(ChatModal))
	{
		SendText(Text);
	}
	CloseComposer();
}

void UConstructChatComponent::GameplayInputChanged(bool bAllowed)
{
	if (!bAllowed && bComposerOpen && InputGate.IsValid() && !InputGate->CanUseNavigationInput(ChatModal))
	{
		CloseComposer();
	}
}

void UConstructChatComponent::EnableChannelChat()
{
	if (!bRuntimeActive || !IsSignedIn() || !HasCurrentIdentity())
	{
		Status = TEXT("Sign in to use channel chat.");
		return;
	}
	if (!bChannelMode)
	{
		CloseComposer();
		History.Reset();
		HistoryText = FText::GetEmpty();
	}
	bChannelMode = true;
	RefreshChannel();
}

void UConstructChatComponent::UseNearbyChat()
{
	if (!bRuntimeActive || !IsSignedIn() || !HasCurrentIdentity())
	{
		return;
	}
	if (bChannelMode)
	{
		CloseComposer();
		ActiveLookup = nullptr;
		bLoading = false;
		ChannelId = 0;
		bCanPublish = false;
		History.Reset();
		HistoryText = FText::GetEmpty();
	}
	bChannelMode = false;
	Status = NearbyReadyStatus;
}

void UConstructChatComponent::RefreshChannel()
{
	if (!bChannelMode || bLoading)
	{
		return;
	}
	UCrowdyChannels* Service = Channels();
	if (!bRuntimeActive || !IsSignedIn() || !HasCurrentIdentity() || !Service)
	{
		Status = TEXT("Sign in to use channel chat.");
		return;
	}
	bLoading = true;
	ChannelId = 0;
	bCanPublish = false;
	ActiveLookup = NewObject<UConstructChatLookup>(this);
	ActiveLookup->Initialize(this);
	Status = TEXT("Loading your chat channel...");
	FOnMyChannelsSuccess Success;
	Success.BindDynamic(ActiveLookup.Get(), &UConstructChatLookup::Succeeded);
	FOnChannelError Error;
	Error.BindDynamic(ActiveLookup.Get(), &UConstructChatLookup::Failed);
	Service->GetMyChannels(Success, Error);
}

void UConstructChatComponent::Memberships(const TArray<FCrowdyChannelMembership>& Values)
{
	ActiveLookup = nullptr;
	bLoading = false;
	ChannelId = 0;
	bCanPublish = false;
	if (!bRuntimeActive || !bChannelMode || !IsSignedIn() || !HasCurrentIdentity())
	{
		return;
	}
	for (const FCrowdyChannelMembership& Value : Values)
	{
		if (Value.Channel.Name == DedicatedChannelName && Value.Channel.ChannelId > 0)
		{
			ChannelId = Value.Channel.ChannelId;
			bCanPublish = Value.Permissions.bSendMessages;
			Status = bCanPublish ? TEXT("Channel chat connected. Messages reach this channel regardless of distance.")
			                     : TEXT("Channel is read-only for your account.");
			return;
		}
	}
	Status = TEXT("No dedicated chat channel is assigned to your account.");
}

void UConstructChatComponent::Failed(FCrowdyChannelError Error, FString Message)
{
	ActiveLookup = nullptr;
	bLoading = false;
	Status = TEXT("Could not load your chat channel. Try Refresh.");
}

FString UConstructChatComponent::SanitizeOutgoing(const FString& Text)
{
	FString Clean = Text.Replace(TEXT("\r\n"), TEXT(" "));
	Clean.ReplaceInline(TEXT("\r"), TEXT(" "));
	Clean.ReplaceInline(TEXT("\n"), TEXT(" "));
	Clean.ReplaceInline(TEXT("<br>"), TEXT(" "), ESearchCase::IgnoreCase);
	Clean.ReplaceInline(TEXT("\t"), TEXT(" "));
	for (TCHAR& Character : Clean)
	{
		if (Character == TEXT('\0'))
		{
			Character = TEXT(' ');
		}
	}
	Clean = Clean.TrimStartAndEnd();
	return LimitText(Clean, 240);
}

bool UConstructChatComponent::Encode(const FString& Text, TArray<uint8>& Bytes)
{
	Bytes.Reset();
	const FString Clean = SanitizeOutgoing(Text);
	if (Clean.IsEmpty())
	{
		return false;
	}
	FTCHARToUTF8 Encoded(*(FString(TEXT("construct.chat.v1\n")) + Clean));
	Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
	return Bytes.Num() <= 2048;
}

bool UConstructChatComponent::Decode(const TArray<uint8>& Bytes, FString& Text)
{
	Text.Empty();
	if (Bytes.IsEmpty() || Bytes.Num() > 2048)
	{
		return false;
	}
	const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	const FString Value(Decoded.Length(), Decoded.Get());
	const FString Prefix(TEXT("construct.chat.v1\n"));
	if (!Value.StartsWith(Prefix))
	{
		return false;
	}
	Text = SanitizeIncoming(Value.Mid(Prefix.Len()));
	return !Text.IsEmpty();
}

FString UConstructChatComponent::SanitizeIncoming(const FString& Text, int32 Maximum)
{
	int32 End = Text.Len();
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		if (Text[Index] == TEXT('\r') || Text[Index] == TEXT('\n') || Text[Index] == TEXT('\0'))
		{
			End = Index;
			break;
		}
	}
	const int32 Break = Text.Find(TEXT("<br>"), ESearchCase::IgnoreCase);
	if (Break != INDEX_NONE)
	{
		End = FMath::Min(End, Break);
	}
	return LimitText(Text.Left(End).TrimStartAndEnd(), FMath::Max(0, Maximum));
}

bool UConstructChatComponent::CanSend() const
{
	if (!bRuntimeActive || !IsSignedIn() || !HasCurrentIdentity())
	{
		return false;
	}
	if (!bChannelMode)
	{
		return ProximityChat() != nullptr;
	}
	UCrowdyChannels* Service = Channels();
	return ChannelId > 0 && bCanPublish && Service && Service->CanPublishChannelMessage();
}

void UConstructChatComponent::Append(const FString& Line)
{
	History.Add(LimitText(Line, 320));
	if (History.Num() > 100)
	{
		History.RemoveAt(0, History.Num() - 100);
	}
	HistoryText = FText::FromString(FString::Join(History, TEXT("\n")));
}

void UConstructChatComponent::SendText(const FString& Text)
{
	const FString Clean = SanitizeOutgoing(Text);
	if (Clean.IsEmpty())
	{
		return;
	}
	if (!CanSend())
	{
		Status = bChannelMode ? TEXT("Channel chat is not ready to send.") : TEXT("Chat is not connected");
		return;
	}
	if (!bChannelMode)
	{
		if (UCrowdyProximityChat* Proximity = ProximityChat())
		{
			ApplyProximitySendResult(Proximity->SendProximityText(Clean, 4), Clean);
		}
		return;
	}
	TArray<uint8> Bytes;
	if (!Encode(Clean, Bytes))
	{
		return;
	}
	Channels()->PublishChannelMessage(ChannelId, Bytes);
	Append(FString(TEXT("You (submitted): ")) + Clean);
	Status = TEXT("Submitted. Channel delivery is not acknowledged.");
}

void UConstructChatComponent::ApplyProximitySendResult(ECrowdyProximityChatResult Result, const FString& Text)
{
	switch (Result)
	{
	case ECrowdyProximityChatResult::Sent:
		Append(FString(TEXT("You: ")) + Text);
		Status = TEXT("Sent. Delivery is not acknowledged.");
		break;
	case ECrowdyProximityChatResult::EmptyText:
		break;
	case ECrowdyProximityChatResult::TooLong:
		Status = TEXT("Chat message is too long.");
		break;
	case ECrowdyProximityChatResult::NotConnected:
	case ECrowdyProximityChatResult::NoPosition:
		Status = TEXT("Chat is not connected");
		break;
	default:
		Status = TEXT("Chat is not connected");
		break;
	}
}

FString UConstructChatComponent::ResolveSenderName(const FGuid& SenderEntityId) const
{
	UCrowdyEntitySubsystem* Entities = GetWorld() ? GetWorld()->GetSubsystem<UCrowdyEntitySubsystem>() : nullptr;
	const AConstructPresencePawn* Sender = Entities && SenderEntityId.IsValid()
	                                           ? Cast<AConstructPresencePawn>(Entities->FindEntity(SenderEntityId))
	                                           : nullptr;
	const FString Name =
	    IsValid(Sender) && !Sender->IsActorBeingDestroyed() ? SanitizeIncoming(Sender->GetPlayerName(), 64) : FString();
	return Name.IsEmpty() ? TEXT("Player") : Name;
}

void UConstructChatComponent::AppendProximityMessage(const FCrowdyProximityText& Message)
{
	const FString Text = SanitizeIncoming(Message.Text);
	if (!Text.IsEmpty())
	{
		// The actor id and presence name are client assertions used only for presentation.
		Append(ResolveSenderName(Message.SenderEntityId) + TEXT(": ") + Text);
	}
}

void UConstructChatComponent::ProximityReceived(const FCrowdyProximityText& Message)
{
	if (!bRuntimeActive || bChannelMode || !IsSignedIn() || !HasCurrentIdentity())
	{
		return;
	}
	AppendProximityMessage(Message);
}

void UConstructChatComponent::ProximityRefused(const FCrowdySendRefusal& Refusal)
{
	if (bRuntimeActive && !bChannelMode && IsSignedIn() && HasCurrentIdentity())
	{
		// This is a late status notice, never a delivery receipt or an instruction to resend.
		Status = TEXT("Chat is not enabled for this app");
	}
}

void UConstructChatComponent::Received(int64 Id, FString Sender, const TArray<uint8>& Bytes)
{
	if (!bRuntimeActive || !bChannelMode || ChannelId <= 0 || Id != ChannelId || !IsSignedIn() || !HasCurrentIdentity())
	{
		return;
	}
	FString Text;
	if (Decode(Bytes, Text))
	{
		// Channel transport has only an actor uuid, not an authenticated player-name lookup id.
		Append(FString(TEXT("Player: ")) + Text);
	}
}

void UConstructChatComponent::SendRefused(const FCrowdySendRefusal& Refusal)
{
	if (!bRuntimeActive || !bChannelMode || ChannelId <= 0 || !Refusal.bChannelMessage ||
	    Refusal.ChannelId != ChannelId || !HasCurrentIdentity())
	{
		return;
	}
	// A late refusal is a status notice, not a reliable receipt for one history entry.
	Status = TEXT("Chat is not enabled for this app or channel.");
}

void UConstructChatComponent::ResetSession()
{
	CloseComposer();
	bShowHistory = false;
	CancelGameplayFocusRestore();
	++SessionGeneration;
	ActiveLookup = nullptr;
	bLoading = false;
	ChannelId = 0;
	bCanPublish = false;
	History.Reset();
	HistoryText = FText::GetEmpty();
	GetCurrentIdentity(SessionUserID, SessionAppID);
}

void UConstructChatComponent::SignedIn(bool bSuccess, FString Message)
{
	if (!bSuccess)
	{
		return;
	}
	ResetSession();
	Status = NearbyReadyStatus;
	RefreshVisibility();
	if (bChannelMode)
	{
		RefreshChannel();
	}
}

void UConstructChatComponent::SignedOut(bool bSuccess)
{
	ResetSession();
	Status = TEXT("Signed out. Chat history cleared.");
	RefreshVisibility();
}

void UConstructChatComponent::AccessEnded(const FString& Reason, const FString& Until)
{
	ResetSession();
	Status = TEXT("App access ended. Chat history cleared.");
	RefreshVisibility();
}

void UConstructChatComponent::Connected()
{
	if (!HasCurrentIdentity())
	{
		ResetSession();
	}
	RefreshVisibility();
	if (bChannelMode)
	{
		RefreshChannel();
	}
}

void UConstructChatComponent::ReleaseInput()
{

	if (APlayerController* Controller = InputController.Get(); Controller && ChatInput)
	{
		Controller->PopInputComponent(ChatInput);
	}
	if (ChatInput)
	{
		ChatInput->DestroyComponent();
	}
	ChatInput = nullptr;

	InputController.Reset();
}

void UConstructChatComponent::ShutdownLocalRuntime()
{
	bRuntimeActive = false;
	SetComponentTickEnabled(false);
	SetupAttemptsRemaining = 0;
	ResetSession();
	ReleaseInput();
	if (UConstructUIInputSubsystem* Gate = InputGate.Get())
	{
		Gate->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::GameplayInputChanged);
	}
	InputGate.Reset();
	if (Panel)
	{
		Panel->RemoveFromParent();
		Panel = nullptr;
	}
	UGameInstance* Game = RuntimeInstance.Get();
	if (UCrowdyProximityChat* Proximity = Game ? Game->GetSubsystem<UCrowdyProximityChat>() : nullptr)
	{
		Proximity->OnProximityTextReceived.RemoveDynamic(this, &ThisClass::ProximityReceived);
		Proximity->OnProximityTextRefused.RemoveDynamic(this, &ThisClass::ProximityRefused);
	}
	if (UCrowdyChannels* Service = Game ? Game->GetSubsystem<UCrowdyChannels>() : nullptr)
	{
		Service->OnChannelMessageReceived.RemoveDynamic(this, &ThisClass::Received);
	}
	if (UCrowdySDKSubsystem* SDK = Game ? Game->GetSubsystem<UCrowdySDKSubsystem>() : nullptr)
	{
		SDK->OnLogin.RemoveDynamic(this, &ThisClass::SignedIn);
		SDK->OnLogout.RemoveDynamic(this, &ThisClass::SignedOut);
		SDK->OnUDPConnectionSuccess.RemoveDynamic(this, &ThisClass::Connected);
	}
	if (UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr)
	{
		Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
	}
	if (UCrowdyCppReplicationSubsystem* Realtime =
	        Game ? Game->GetSubsystem<UCrowdyCppReplicationSubsystem>() : nullptr)
	{
		Realtime->OnSendRefused.RemoveDynamic(this, &ThisClass::SendRefused);
	}
	RuntimeInstance.Reset();
	SessionUserID = 0;
	SessionAppID = 0;
	bChannelMode = false;
	Status = NearbyReadyStatus;
}

void UConstructChatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ShutdownLocalRuntime();
	Super::EndPlay(Reason);
}

void UConstructChatLookup::Initialize(UConstructChatComponent* InOwner)
{
	Owner = InOwner;
	if (InOwner)
	{
		Generation = InOwner->SessionGeneration;
		InOwner->GetCurrentIdentity(UserID, AppID);
	}
}

bool UConstructChatLookup::IsCurrent() const
{
	if (!Owner.IsValid() || Owner->ActiveLookup != this || Owner->SessionGeneration != Generation)
	{
		return false;
	}
	int64 CurrentUser = 0;
	int64 CurrentApp = 0;
	Owner->GetCurrentIdentity(CurrentUser, CurrentApp);
	return CurrentUser == UserID && CurrentApp == AppID;
}

void UConstructChatLookup::Succeeded(const TArray<FCrowdyChannelMembership>& Values)
{
	if (IsCurrent())
	{
		Owner->Memberships(Values);
	}
}

void UConstructChatLookup::Failed(FCrowdyChannelError Error, FString Message)
{
	if (IsCurrent())
	{
		Owner->Failed(Error, Message);
	}
}
