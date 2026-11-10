#include "ConstructShellFramework.h"

#include "ConstructSignInWidget.h"
#include "ConstructShellPresentation.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "ConstructPreviewPawn.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdySDKSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructShell"

FText UConstructConnectionStatus::GetStatusCaption() const
{
	UGameInstance* Instance = GetGameInstance();
	UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	return ConstructShellPresentation::ConnectionCaption(SDK ? SDK->GetUDPConnectionState()
	                                                         : EUDPConnectionState::Disconnected,
	                                                     Session && Session->IsAppPaused(),
	                                                     Session ? Session->GetAppPausedReason() : FString(),
	                                                     SDK != nullptr);
}

TSharedRef<SWidget> UConstructConnectionStatus::RebuildWidget()
{
	const TWeakObjectPtr<UConstructConnectionStatus> WeakThis(this);
	return SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible) +
	       SHorizontalBox::Slot()
	           .FillWidth(1)
	           .VAlign(VAlign_Bottom)
	           .Padding(20)[SNew(STextBlock)
	                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
	                            .Visibility(EVisibility::HitTestInvisible)
	                            .Text_Lambda(
	                                [WeakThis]()
	                                {
		                                return WeakThis.IsValid()
		                                           ? FText::Format(LOCTEXT("StatusAndCursor", "{0}   |   F4: Toggle cursor"),
		                                                           WeakThis->GetStatusCaption())
		                                           : FText::GetEmpty();
	                                })] +
	       SHorizontalBox::Slot()
	           .AutoWidth()
	           .VAlign(VAlign_Bottom)
	           .Padding(20)[SNew(SButton)
	                            .ContentPadding(FMargin(16, 8))
	                            .OnClicked_Lambda(
	                                [WeakThis]()
	                                {
		                                UGameInstance* Instance =
		                                    WeakThis.IsValid() ? WeakThis->GetGameInstance() : nullptr;
		                                if (UCrowdySDKSubsystem* SDK =
		                                        Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr)
		                                {
			                                SDK->Logout();
		                                }
		                                return FReply::Handled();
	                                })[SNew(STextBlock).Text(LOCTEXT("SignOut", "Sign Out"))]];
}

AConstructShellHUD::AConstructShellHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.15f;
	SignInWidgetClass = UConstructSignInWidget::StaticClass();
	StatusWidgetClass = UConstructConnectionStatus::StaticClass();
}

void AConstructShellHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}
	SignIn = CreateWidget<UConstructSignInWidget>(Controller, SignInWidgetClass);
	ConnectionStatus = CreateWidget<UConstructConnectionStatus>(Controller, StatusWidgetClass);
	if (!SignIn || !ConnectionStatus)
	{
		return;
	}
	SignIn->SetIsFocusable(true);
	SignIn->AddToViewport(10);
	ConnectionStatus->AddToViewport(5);
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
		{
			SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::LoggedOut);
			SDK->OnUDPConnectionSuccess.AddUniqueDynamic(this, &ThisClass::RefreshVisibility);
			SDK->OnUDPTimedOut.AddUniqueDynamic(this, &ThisClass::RefreshVisibility);
			SDK->OnUDPAddressNotify.AddUniqueDynamic(this, &ThisClass::AddressChanged);
		}
		UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>();
		if (Session)
		{
			Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
			Session->OnAppPausedChanged.AddUniqueDynamic(this, &ThisClass::PauseChanged);
			if (Session->HasAccessEnded())
			{
				AccessEnded(Session->GetAccessEndedReason(), Session->GetAccessBlockedUntil());
			}
		}
		UCrowdyAuthentication* Auth = Instance->GetSubsystem<UCrowdyAuthentication>();
		if (!bLocalPreview && Auth && !Auth->IsSignedIn() && !(Session && Session->HasAccessEnded()))
		{
			SignIn->RestoreSavedSession();
		}
	}
	RefreshVisibility();
}

void AConstructShellHUD::RefreshVisibility()
{
	if (!SignIn || !ConnectionStatus)
	{
		return;
	}
	UGameInstance* Instance = GetGameInstance();
	UCrowdyAuthentication* Auth = Instance ? Instance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	const bool bShouldShow = ConstructShellPresentation::ShouldShowSignIn(
	    bLocalPreview, Auth && Auth->IsSignedIn(), Session && Session->HasAccessEnded());
	SignIn->SetVisibility(bShouldShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const bool bShowConnectionStatus = !bShouldShow && Auth && Auth->IsSignedIn();
	ConnectionStatus->SetVisibility(bShowConnectionStatus ? ESlateVisibility::SelfHitTestInvisible
	                                                      : ESlateVisibility::Collapsed);

	bShowingSignIn = bShouldShow;
	if (APlayerController* Controller = GetOwningPlayerController())
	{
		if (ULocalPlayer* Local = Controller->GetLocalPlayer())
		{
			if (UConstructUIInputSubsystem* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>())
			{
				UCrowdySDKSubsystem* SDK = Instance ? Instance->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
				const bool bConnected = SDK && SDK->GetUDPConnectionState() == EUDPConnectionState::Connected;
				const bool bReady = ConstructShellPresentation::IsSessionReady(bLocalPreview,
				                                                               Auth && Auth->IsSignedIn(),
				                                                               Session && Session->HasAccessEnded(),
				                                                               bConnected,
				                                                               Auth && Auth->IsLegalAcceptancePending(),
				                                                               Session && Session->IsAppPaused());
				Gate->SetModal(TEXT("ConstructSignIn"), bShouldShow, SignIn);
				Gate->SetSessionReady(bReady);
			}
		}
	}
}

void AConstructShellHUD::AddressChanged(bool bSuccess, bool bGateKeep)
{
	RefreshVisibility();
}

void AConstructShellHUD::PauseChanged(bool bPaused, const FString& Reason)
{
	RefreshVisibility();
}

void AConstructShellHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RefreshVisibility();
}

void AConstructShellHUD::LoggedOut(bool bSuccess)
{
	if (SignIn)
	{
		SignIn->SetMessage(LOCTEXT("Ready", "Choose a sign-in method to continue."));
	}
	RefreshVisibility();
}

void AConstructShellHUD::AccessEnded(const FString& Reason, const FString& BlockedUntil)
{
	if (SignIn)
	{
		SignIn->ShowAccessEnded(Reason, BlockedUntil);
	}
	RefreshVisibility();
}

void AConstructShellHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SignIn && SignIn->IsBusy())
	{
		SignIn->CancelSignIn();
	}
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
		{
			SDK->OnLogout.RemoveDynamic(this, &ThisClass::LoggedOut);
			SDK->OnUDPConnectionSuccess.RemoveDynamic(this, &ThisClass::RefreshVisibility);
			SDK->OnUDPTimedOut.RemoveDynamic(this, &ThisClass::RefreshVisibility);
			SDK->OnUDPAddressNotify.RemoveDynamic(this, &ThisClass::AddressChanged);
		}
		if (UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>())
		{
			Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
			Session->OnAppPausedChanged.RemoveDynamic(this, &ThisClass::PauseChanged);
		}
	}
	if (APlayerController* Controller = GetOwningPlayerController(); Controller && Controller->GetLocalPlayer())
	{
		if (auto* Gate = Controller->GetLocalPlayer()->GetSubsystem<UConstructUIInputSubsystem>())
		{
			Gate->SetModal(TEXT("ConstructSignIn"), false);
			Gate->ReleaseManagement();
		}
	}
	if (SignIn)
	{
		SignIn->RemoveFromParent();
	}
	if (ConnectionStatus)
	{
		ConnectionStatus->RemoveFromParent();
	}
	Super::EndPlay(EndPlayReason);
}

AConstructShellGameMode::AConstructShellGameMode()
{
	HUDClass = AConstructShellHUD::StaticClass();
	DefaultPawnClass = AConstructPreviewPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}

void AConstructShellGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
		{
			SDK->OnUDPConnectionSuccess.AddUniqueDynamic(this, &ThisClass::Connected);
			SDK->OnLogout.AddUniqueDynamic(this, &ThisClass::LoggedOut);
			if (UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>())
			{
				Session->OnAppPausedChanged.AddUniqueDynamic(this, &ThisClass::PauseChanged);
				Session->OnAccessEnded.AddUniqueDynamic(this, &ThisClass::AccessEnded);
			}
			if (UCrowdyAuthentication* Auth = Instance->GetSubsystem<UCrowdyAuthentication>())
			{
				Auth->OnLegalAcceptanceEnded.AddUniqueDynamic(this, &ThisClass::LegalEnded);
			}
			if (SDK->GetUDPConnectionState() == EUDPConnectionState::Connected)
			{
				Connected();
			}
		}
	}
}

void AConstructShellGameMode::Connected()
{
	UGameInstance* Instance = GetGameInstance();
	UCrowdyAuthentication* Auth = Instance ? Instance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	if (!Auth || !Auth->IsSignedIn() || Auth->IsLegalAcceptancePending() ||
	    (Session && (Session->HasAccessEnded() || Session->IsAppPaused())))
	{
		return;
	}
	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (!Controller->GetPawn() || Controller->GetPawn() == Controller->GetSpectatorPawn())
		{
			RestartPlayer(Controller);
		}
	}
}

void AConstructShellGameMode::PauseChanged(bool bPaused, const FString& Reason)
{
	if (!bPaused)
	{
		LegalEnded();
	}
}

void AConstructShellGameMode::LegalEnded()
{
	UCrowdySDKSubsystem* SDK = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdySDKSubsystem>() : nullptr;
	if (SDK && SDK->GetUDPConnectionState() == EUDPConnectionState::Connected)
	{
		Connected();
	}
}

void AConstructShellGameMode::ReleasePlayerPawn()
{
	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		APawn* Pawn = Controller->GetPawn();
		if (Pawn && Pawn != Controller->GetSpectatorPawn() && !Pawn->ActorHasTag(TEXT("ConstructLocalPreview")))
		{
			Controller->UnPossess();
			Pawn->Destroy();
		}
	}
}

void AConstructShellGameMode::LoggedOut(bool bSuccess)
{
	if (bSuccess)
	{
		ReleasePlayerPawn();
	}
}

void AConstructShellGameMode::AccessEnded(const FString& Reason, const FString& BlockedUntil)
{
	ReleasePlayerPawn();
}

void AConstructShellGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UCrowdySDKSubsystem* SDK = Instance->GetSubsystem<UCrowdySDKSubsystem>())
		{
			SDK->OnUDPConnectionSuccess.RemoveDynamic(this, &ThisClass::Connected);
			SDK->OnLogout.RemoveDynamic(this, &ThisClass::LoggedOut);
			if (UCrowdyGameSession* Session = Instance->GetSubsystem<UCrowdyGameSession>())
			{
				Session->OnAppPausedChanged.RemoveDynamic(this, &ThisClass::PauseChanged);
				Session->OnAccessEnded.RemoveDynamic(this, &ThisClass::AccessEnded);
			}
			if (UCrowdyAuthentication* Auth = Instance->GetSubsystem<UCrowdyAuthentication>())
			{
				Auth->OnLegalAcceptanceEnded.RemoveDynamic(this, &ThisClass::LegalEnded);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE

void AConstructShellHUD::SetLocalPreviewEnabled(bool bEnabled)
{
	if (bEnabled && SignIn && SignIn->IsBusy())
	{
		SignIn->CancelSignIn();
	}
	bLocalPreview = bEnabled;
	RefreshVisibility();
}

void AConstructShellHUD::ShowSignIn()
{
	bLocalPreview = false;
	RefreshVisibility();
	UCrowdyAuthentication* Auth =
	    GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	if (SignIn && Auth && !Auth->IsSignedIn() && !SignIn->IsBusy())
	{
		SignIn->RestoreSavedSession();
	}
}
