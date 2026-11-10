#include "ConstructPlayerNameWidget.h"

#include "ConstructPresencePawn.h"
#include "ConstructShellStyle.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdyPlayers.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructPlayerName"

namespace
{
const FName NameModalKey(TEXT("ConstructPlayerName"));
constexpr int32 MaxGamertagLength = 64;
constexpr int32 MaxDisambiguationLength = 128;
}

class SConstructPlayerName : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConstructPlayerName)
	{
	}
	SLATE_ARGUMENT(TWeakObjectPtr<UConstructPlayerNameWidget>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		const TWeakObjectPtr<UConstructPlayerNameWidget> Owner = Args._Owner;
		const ISlateStyle& Style = FConstructShellStyle::Get();
		const FButtonStyle* Primary = &Style.GetWidgetStyle<FButtonStyle>(TEXT("Construct.Button.Primary"));
		const FButtonStyle* Secondary = &Style.GetWidgetStyle<FButtonStyle>(TEXT("Construct.Button.Secondary"));
		const FEditableTextBoxStyle* Input = &Style.GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Construct.Input"));
		const FTextBlockStyle* Body = &Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Construct.Text.Body"));
		const FTextBlockStyle* Muted = &Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Construct.Text.Muted"));
		auto Interact = [Owner]()
		{
			return Owner.IsValid() && Owner->CanInteract();
		};
		auto Edit = [Owner]()
		{
			return Owner.IsValid() && Owner->CanInteract() && !Owner->IsBusy();
		};
		auto Close = [Owner]()
		{
			return Owner.IsValid() && Owner->CanClose();
		};

		TSharedRef<SVerticalBox> Form = SNew(SVerticalBox);
		Form->AddSlot()
		    .AutoHeight()[SNew(STextBlock)
		                      .TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Construct.Text.Heading")))
		                      .Text(LOCTEXT("Title", "Player name"))];
		Form->AddSlot().AutoHeight().Padding(
		    0, 12, 0, 5)[SNew(STextBlock).TextStyle(Body).Text(LOCTEXT("Gamertag", "Gamertag"))];
		Form->AddSlot().AutoHeight()
		    [SNew(SHorizontalBox) +
		     SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SEditableTextBox)
		                                                .Style(Input)
		                                                .IsEnabled_Lambda(Edit)
		                                                .Text_Lambda(
		                                                    [Owner]()
		                                                    {
			                                                    return Owner.IsValid()
			                                                               ? FText::FromString(Owner->GetGamertag())
			                                                               : FText::GetEmpty();
		                                                    })
		                                                .OnVerifyTextChanged_Lambda(
		                                                    [](const FText& Value, FText& Error)
		                                                    {
			                                                    if (Value.ToString().Len() <= MaxGamertagLength)
			                                                    {
				                                                    return true;
			                                                    }
			                                                    Error = LOCTEXT("GamertagLimit",
			                                                                    "Use at most 64 characters.");
			                                                    return false;
		                                                    })
		                                                .OnTextChanged_Lambda(
		                                                    [Owner](const FText& Value)
		                                                    {
			                                                    if (Owner.IsValid())
			                                                    {
				                                                    Owner->SetDraftGamertag(Value);
			                                                    }
		                                                    })] +
		     SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[SNew(SBox).WidthOverride(110).ToolTipText(
		         LOCTEXT("IdentifierTooltip",
		                 "Identifier"))[SNew(SHorizontalBox) +
		                                SHorizontalBox::Slot()
		                                    .AutoWidth()
		                                    .VAlign(VAlign_Center)
		                                    .Padding(0, 0, 5, 0)[SNew(STextBlock).TextStyle(Body).Text(INVTEXT("#"))] +
		                                SHorizontalBox::Slot().FillWidth(
		                                    1.0f)[SNew(SEditableTextBox)
		                                              .Style(Input)
		                                              .IsEnabled_Lambda(Edit)
		                                              .Text_Lambda(
		                                                  [Owner]()
		                                                  {
			                                                  return Owner.IsValid()
			                                                             ? FText::FromString(Owner->GetDisambiguation())
			                                                             : FText::GetEmpty();
		                                                  })
		                                              .OnVerifyTextChanged_Lambda(
		                                                  [](const FText& Value, FText& Error)
		                                                  {
			                                                  if (Value.ToString().Len() <= MaxDisambiguationLength)
			                                                  {
				                                                  return true;
			                                                  }
			                                                  Error = LOCTEXT("IdentifierLimit",
			                                                                  "Use at most 128 characters.");
			                                                  return false;
		                                                  })
		                                              .OnTextChanged_Lambda(
		                                                  [Owner](const FText& Value)
		                                                  {
			                                                  if (Owner.IsValid())
			                                                  {
				                                                  Owner->SetDraftDisambiguation(Value);
			                                                  }
		                                                  })]]]];
		Form->AddSlot().AutoHeight().Padding(
		    0, 5, 0, 12)[SNew(STextBlock)
		                     .TextStyle(Muted)
		                     .Text(LOCTEXT("IdentifierHint",
		                                   "The public suffix that distinguishes your name from another player's."))
		                     .AutoWrapText(true)];
		Form->AddSlot().AutoHeight().Padding(
		    0, 0, 0, 12)[SNew(STextBlock)
		                     .Text_Lambda(
		                         [Owner]()
		                         {
			                         return Owner.IsValid() ? Owner->GetMessage() : FText::GetEmpty();
		                         })
		                     .TextStyle(Body)
		                     .AutoWrapText(true)
		                     .ColorAndOpacity_Lambda(
		                         [Owner]()
		                         {
			                         return Owner.IsValid() && Owner->bError
			                                    ? FConstructShellStyle::Get()
			                                          .GetWidgetStyle<FTextBlockStyle>(TEXT("Construct.Text.Error"))
			                                          .ColorAndOpacity
			                                    : FSlateColor(FConstructShellStyle::Muted());
		                         })];
		Form->AddSlot().AutoHeight()
		    [SNew(SHorizontalBox) +
		     SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SButton)
		                                                .ButtonStyle(Primary)
		                                                .ContentPadding(FMargin(12, 8))
		                                                .IsEnabled_Lambda(Edit)
		                                                .OnClicked_Lambda(
		                                                    [Owner]()
		                                                    {
			                                                    if (Owner.IsValid())
			                                                    {
				                                                    Owner->SubmitName(Owner->GetGamertag(),
				                                                                      Owner->GetDisambiguation());
			                                                    }
			                                                    return FReply::Handled();
		                                                    })[SNew(STextBlock)
		                                                           .Text_Lambda(
		                                                               [Owner]()
		                                                               {
			                                                               return Owner.IsValid() && Owner->IsBusy()
			                                                                          ? LOCTEXT("Saving", "Working...")
			                                                                          : LOCTEXT("Save", "Save");
		                                                               })]] +
		     SHorizontalBox::Slot().AutoWidth().Padding(
		         8, 0)[SNew(SButton)
		                   .ButtonStyle(Secondary)
		                   .ContentPadding(FMargin(12, 8))
		                   .IsEnabled_Lambda(Edit)
		                   .OnClicked_Lambda(
		                       [Owner]()
		                       {
			                       if (Owner.IsValid())
			                       {
				                       Owner->RefreshProfile();
			                       }
			                       return FReply::Handled();
		                       })[SNew(STextBlock).Text(LOCTEXT("Refresh", "Refresh"))]] +
		     SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
		                                            .ButtonStyle(Secondary)
		                                            .ContentPadding(FMargin(12, 8))
		                                            .IsEnabled_Lambda(Close)
		                                            .OnClicked_Lambda(
		                                                [Owner]()
		                                                {
			                                                if (Owner.IsValid())
			                                                {
				                                                Owner->CloseEditor();
			                                                }
			                                                return FReply::Handled();
		                                                })[SNew(STextBlock).Text(LOCTEXT("Close", "Close"))]]];

		ChildSlot[SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible) +
		          SOverlay::Slot()
		              .HAlign(HAlign_Right)
		              .VAlign(VAlign_Top)
		              .Padding(20, 20, 20, 0)[SNew(SButton)
		                                          .ButtonStyle(Secondary)
		                                          .ContentPadding(FMargin(12, 8))
		                                          .IsEnabled_Lambda(Interact)
		                                          .OnClicked_Lambda(
		                                              [Owner]()
		                                              {
			                                              if (Owner.IsValid())
			                                              {
				                                              Owner->OpenEditor();
			                                              }
			                                              return FReply::Handled();
		                                              })[SNew(STextBlock)
		                                                     .TextStyle(Body)
		                                                     .Text_Lambda(
		                                                         [Owner]()
		                                                         {
			                                                         return Owner.IsValid() ? Owner->BadgeCaption()
			                                                                                : FText::GetEmpty();
		                                                         })]] +
		          SOverlay::Slot()[SNew(SBorder)
		                               .BorderImage(Style.GetBrush(TEXT("Construct.Background")))
		                               .Visibility_Lambda(
		                                   [Owner]()
		                                   {
			                                   return Owner.IsValid() && Owner->IsOpen() ? EVisibility::Visible
			                                                                             : EVisibility::Collapsed;
		                                   })
		                               .HAlign(HAlign_Center)
		                               .VAlign(VAlign_Center)[SNew(SBox).WidthOverride(
		                                   420.0f)[SNew(SBorder)
		                                               .BorderImage(Style.GetBrush(TEXT("Construct.Panel")))
		                                               .Padding(24)[Form]]]]];
	}
};

void UConstructPlayerNameWidget::InitializeForPawn(AConstructPresencePawn* Pawn)
{
	if (bInitialized || !IsValid(Pawn))
	{
		return;
	}
	bInitialized = true;
	bLive = true;
	OwnerPawn = Pawn;
	Entity = Pawn->FindComponentByClass<UCrowdyEntityComponent>();
	UGameInstance* Instance = GetGameInstance();
	Authentication = Instance ? Instance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	APlayerController* Controller = GetOwningPlayer();
	ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
	InputGate = Local ? Local->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
	if (UConstructUIInputSubsystem* Gate = InputGate.Get())
	{
		Gate->OnGameplayInputChanged.AddUniqueDynamic(this, &ThisClass::InputPolicyChanged);
	}
}

bool UConstructPlayerNameWidget::ResolveIdentity(int64& UserID, FGuid& NetID, FGuid& OwnerID) const
{
	AConstructPresencePawn* Pawn = OwnerPawn.Get();
	APlayerController* Controller = GetOwningPlayer();
	if (!bLive || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(Controller) ||
	    !Controller->IsLocalController() || Controller->GetPawn() != Pawn || Pawn->GetController() != Controller ||
	    !Pawn->IsLocallyControlled() || Pawn->GetGameInstance() != GetGameInstance())
	{
		return false;
	}
	UCrowdyEntityComponent* PlayerEntity = Entity.Get();
	const UCrowdyAuthentication* Auth = Authentication.Get();
	const UCrowdyGameSession* PlayerSession = Session.Get();
	if (!PlayerEntity || !PlayerEntity->IsLocallyOwned() || !Auth || !Auth->IsSignedIn() || !PlayerSession ||
	    PlayerSession->GetUserID() <= 0)
	{
		return false;
	}
	UserID = PlayerSession->GetUserID();
	NetID = PlayerEntity->GetNetID();
	OwnerID = PlayerEntity->GetOwnerID();
	return NetID.IsValid();
}

bool UConstructPlayerNameWidget::CanInteract() const
{
	int64 UserID = 0;
	FGuid NetID, OwnerID;
	if (!ResolveIdentity(UserID, NetID, OwnerID))
	{
		return false;
	}
	const UConstructUIInputSubsystem* Gate = InputGate.Get();
	const UCrowdyAuthentication* Auth = Authentication.Get();
	return Gate && Gate->CanUseNavigationInput(NameModalKey) && Auth && !Auth->IsLegalAcceptancePending();
}

bool UConstructPlayerNameWidget::CanClose() const
{
	return bOpen;
}

void UConstructPlayerNameWidget::SetInitialProfile(const FCrowdyPlayerProfile& Profile, bool bReadSucceeded)
{
	int64 UserID = 0;
	FGuid NetID, OwnerID;
	if (!ResolveIdentity(UserID, NetID, OwnerID) || bSaveConfirmed)
	{
		return;
	}
	if (!bReadSucceeded || Profile.UserID != UserID)
	{
		if (!IsBusy())
		{
			Message = LOCTEXT("InitialReadFailed", "Could not load your player name. Use Refresh to try again.");
			bError = true;
		}
		return;
	}
	SetProfileModel(Profile, !bDirty && !IsBusy());
	if (Profile.Gamertag.IsEmpty() && !bInitialPromptHandled)
	{
		bInitialPromptPending = true;
		TryInitialPrompt();
	}
}

void UConstructPlayerNameWidget::SetProfileModel(const FCrowdyPlayerProfile& Profile, bool bReplaceDraft)
{
	CurrentGamertag = Profile.Gamertag;
	CurrentDisambiguation = Profile.Disambiguation;
	if (bReplaceDraft)
	{
		DraftGamertag = CurrentGamertag;
		DraftDisambiguation =
		    CurrentGamertag.IsEmpty() && CurrentDisambiguation.IsEmpty() ? TEXT("0001") : CurrentDisambiguation;
		bDirty = false;
	}
}

void UConstructPlayerNameWidget::TryInitialPrompt()
{
	if (bInitialPromptPending && !bInitialPromptHandled && CanInteract())
	{
		OpenEditor();
	}
}

void UConstructPlayerNameWidget::InputPolicyChanged(bool bAllowed)
{
	TryInitialPrompt();
}

void UConstructPlayerNameWidget::OpenEditor()
{
	if (!CanInteract())
	{
		return;
	}
	bInitialPromptHandled = true;
	bInitialPromptPending = false;
	bOpen = true;
	if (UConstructUIInputSubsystem* Gate = InputGate.Get())
	{
		Gate->SetModal(NameModalKey, true, this);
	}
}

void UConstructPlayerNameWidget::CloseEditor()
{
	if (!bOpen)
	{
		return;
	}
	bInitialPromptHandled = true;
	bInitialPromptPending = false;
	bOpen = false;
	if (UConstructUIInputSubsystem* Gate = InputGate.Get())
	{
		Gate->SetModal(NameModalKey, false);
	}
}

void UConstructPlayerNameWidget::SetDraftGamertag(const FText& Value)
{
	if (!CanInteract() || IsBusy())
	{
		return;
	}
	const FString Text = Value.ToString();
	if (Text.Len() <= MaxGamertagLength && Text != DraftGamertag)
	{
		DraftGamertag = Text;
		bDirty = true;
	}
}

void UConstructPlayerNameWidget::SetDraftDisambiguation(const FText& Value)
{
	if (!CanInteract() || IsBusy())
	{
		return;
	}
	const FString Text = Value.ToString();
	if (Text.Len() <= MaxDisambiguationLength && Text != DraftDisambiguation)
	{
		DraftDisambiguation = Text;
		bDirty = true;
	}
}

bool UConstructPlayerNameWidget::BeginRequest(bool bSave)
{
	if (!CanInteract() || IsBusy())
	{
		return false;
	}
	int64 UserID = 0;
	FGuid NetID, OwnerID;
	if (!ResolveIdentity(UserID, NetID, OwnerID))
	{
		return false;
	}
	ActiveRequest = NewObject<UConstructPlayerNameRequest>(this);
	ActiveRequest->Widget = this;
	ActiveRequest->Pawn = OwnerPawn;
	ActiveRequest->ExpectedUserID = UserID;
	ActiveRequest->ExpectedNetID = NetID;
	ActiveRequest->ExpectedOwnerID = OwnerID;
	ActiveRequest->bSave = bSave;
	bError = false;
	Message = bSave ? LOCTEXT("SavingMessage", "Saving player name...")
	                : LOCTEXT("ReadingMessage", "Refreshing player name...");
	return true;
}

void UConstructPlayerNameWidget::SubmitName(const FString& Gamertag, const FString& Disambiguation)
{
	if (!CanInteract() || IsBusy())
	{
		return;
	}
	if (Gamertag.IsEmpty() || Gamertag.Len() > MaxGamertagLength || Disambiguation.Len() > MaxDisambiguationLength)
	{
		bError = true;
		Message = LOCTEXT("InvalidDraft",
		                  "Enter a gamertag of up to 64 characters and an identifier of up to 128 characters.");
		return;
	}
	UCrowdyPlayers* Players = GetGameInstance()->GetSubsystem<UCrowdyPlayers>();
	if (!Players)
	{
		bError = true;
		Message = LOCTEXT("NoPlayers", "Player names are unavailable. Try again later.");
		return;
	}
	DraftGamertag = Gamertag;
	DraftDisambiguation = Disambiguation;
	bDirty = true;
	if (!BeginRequest(true))
	{
		return;
	}
	FOnPlayerProfileRead Result;
	Result.BindDynamic(ActiveRequest.Get(), &UConstructPlayerNameRequest::Succeeded);
	FOnPlayersError Error;
	Error.BindDynamic(ActiveRequest.Get(), &UConstructPlayerNameRequest::Failed);
	Players->SetPlayerName(DraftGamertag, DraftDisambiguation, Result, Error);
}

void UConstructPlayerNameWidget::RefreshProfile()
{
	if (!CanInteract() || IsBusy())
	{
		return;
	}
	UCrowdyPlayers* Players = GetGameInstance()->GetSubsystem<UCrowdyPlayers>();
	if (!Players)
	{
		bError = true;
		Message = LOCTEXT("NoPlayers", "Player names are unavailable. Try again later.");
		return;
	}
	if (!BeginRequest(false))
	{
		return;
	}
	FOnPlayerProfileRead Result;
	Result.BindDynamic(ActiveRequest.Get(), &UConstructPlayerNameRequest::Succeeded);
	FOnPlayersError Error;
	Error.BindDynamic(ActiveRequest.Get(), &UConstructPlayerNameRequest::Failed);
	Players->GetPlayerName(ActiveRequest->ExpectedUserID, Result, Error);
}

bool UConstructPlayerNameWidget::ConsumeRequest(UConstructPlayerNameRequest* Request)
{
	if (!bLive || !Request || ActiveRequest != Request)
	{
		return false;
	}
	int64 UserID = 0;
	FGuid NetID, OwnerID;
	const bool bAccept = Request->Widget.Get() == this && Request->Pawn == OwnerPawn &&
	                     ResolveIdentity(UserID, NetID, OwnerID) && UserID == Request->ExpectedUserID &&
	                     NetID == Request->ExpectedNetID && OwnerID == Request->ExpectedOwnerID;
	ActiveRequest = nullptr;
	return bAccept;
}

void UConstructPlayerNameWidget::RequestSucceeded(UConstructPlayerNameRequest* Request,
                                                  const FCrowdyPlayerProfile& Profile)
{
	const TStrongObjectPtr<UConstructPlayerNameWidget> KeepWidget(this);
	const TStrongObjectPtr<UConstructPlayerNameRequest> KeepRequest(Request);
	if (!ConsumeRequest(Request))
	{
		return;
	}
	if (Profile.UserID != Request->ExpectedUserID || (Request->bSave && Profile.Gamertag.IsEmpty()))
	{
		bError = true;
		Message = LOCTEXT("IncompleteResult",
		                  "The service returned an unexpected profile. Refresh to confirm your saved name.");
		return;
	}
	AConstructPresencePawn* Pawn = OwnerPawn.Get();
	if (!Pawn || !Pawn->ApplyConfirmedPlayerProfile(Profile))
	{
		bError = true;
		Message =
		    LOCTEXT("OwnerChanged", "Your player changed before the name could be displayed. Refresh to continue.");
		return;
	}
	if (!bLive)
	{
		return;
	}
	if (Request->bSave)
	{
		bSaveConfirmed = true;
		bInitialPromptHandled = true;
		bInitialPromptPending = false;
	}
	SetProfileModel(Profile, true);
	bError = false;
	Message = Request->bSave ? LOCTEXT("Saved", "Player name saved.") : LOCTEXT("Refreshed", "Player name refreshed.");
	// A closed editor stays closed. Completion never adds a modal or takes focus.
}

void UConstructPlayerNameWidget::RequestFailed(UConstructPlayerNameRequest* Request,
                                               const FCrowdyAccountError& Error,
                                               const FString& Detail)
{
	const TStrongObjectPtr<UConstructPlayerNameWidget> KeepWidget(this);
	const TStrongObjectPtr<UConstructPlayerNameRequest> KeepRequest(Request);
	if (!ConsumeRequest(Request))
	{
		return;
	}
	bError = true;
	if (Error.Code == ECrowdyAccountErrorCode::NameTaken)
	{
		Message = LOCTEXT("NameTaken", "That name and identifier are already in use. Try another identifier.");
	}
	else if (!Error.Message.IsEmpty())
	{
		Message = FText::FromString(Error.Message);
	}
	else if (!Detail.IsEmpty())
	{
		Message = FText::FromString(Detail);
	}
	else
	{
		Message = LOCTEXT("RequestFailed", "Could not update player names. Try again.");
	}
}

FText UConstructPlayerNameWidget::BadgeCaption() const
{
	return CurrentGamertag.IsEmpty()
	           ? LOCTEXT("SetName", "Set player name [F2]")
	           : FText::Format(LOCTEXT("NamedBadge", "{0}  [F2]"), FText::FromString(CurrentGamertag));
}

TSharedRef<SWidget> UConstructPlayerNameWidget::RebuildWidget()
{
	// The view fills the screen; only its badge and open form take clicks, so panels beneath it stay usable.
	SAssignNew(View, SConstructPlayerName).Owner(this).Visibility(EVisibility::SelfHitTestInvisible);
	return View.ToSharedRef();
}

void UConstructPlayerNameWidget::Shutdown()
{
	if (!bLive)
	{
		return;
	}
	bLive = false;
	bOpen = false;
	bInitialPromptPending = false;
	ActiveRequest = nullptr;
	if (UConstructUIInputSubsystem* Gate = InputGate.Get())
	{
		Gate->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::InputPolicyChanged);
		Gate->SetModal(NameModalKey, false);
	}
	InputGate.Reset();
	OwnerPawn.Reset();
	Entity.Reset();
	Authentication.Reset();
	Session.Reset();
	RemoveFromParent();
}

void UConstructPlayerNameWidget::NativeDestruct()
{
	Shutdown();
	Super::NativeDestruct();
}

void UConstructPlayerNameWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	View.Reset();
}

void UConstructPlayerNameRequest::Succeeded(FCrowdyPlayerProfile Profile)
{
	if (UConstructPlayerNameWidget* Owner = Widget.Get())
	{
		Owner->RequestSucceeded(this, Profile);
	}
}

void UConstructPlayerNameRequest::Failed(FCrowdyAccountError Error, FString Message)
{
	if (UConstructPlayerNameWidget* Owner = Widget.Get())
	{
		Owner->RequestFailed(this, Error, Message);
	}
}

#undef LOCTEXT_NAMESPACE
