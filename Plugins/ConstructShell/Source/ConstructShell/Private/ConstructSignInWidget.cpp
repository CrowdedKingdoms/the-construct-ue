#include "ConstructSignInWidget.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Utils/CrowdySDKDeveloperSettings.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructShell"

namespace ConstructLogin
{
FSlateFontInfo Font(FName Typeface, float PixelSize)
{
	// Slate font sizes are points; the approved layout is specified in design pixels.
	return FCoreStyle::GetDefaultFontStyle(Typeface, PixelSize * 0.75f);
}
FLinearColor Color(uint32 Hex)
{
	return FLinearColor::FromSRGBColor(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255));
}
const FLinearColor Ink = Color(0xFAFAFA);
const FLinearColor Muted = Color(0xA1A1A4);
const FLinearColor Gold = Color(0xE0AE2A);
FString ProviderName(const FString& Key)
{
	const FString Lower = Key.ToLower();
	if (Lower.Contains(TEXT("google")))
	{
		return TEXT("Google");
	}
	if (Lower.Contains(TEXT("discord")))
	{
		return TEXT("Discord");
	}
	if (Lower.Contains(TEXT("github")))
	{
		return TEXT("GitHub");
	}
	if (Lower.Contains(TEXT("apple")))
	{
		return TEXT("Apple");
	}
	if (Lower.Contains(TEXT("microsoft")) || Lower.Contains(TEXT("azure")))
	{
		return TEXT("Microsoft");
	}
	if (Lower.Contains(TEXT("mock")))
	{
		return TEXT("Mock (dev test)");
	}
	return Key.IsEmpty() ? Key : Key.Left(1).ToUpper() + Key.Mid(1);
}
FString ProviderIcon(const FString& Key)
{
	for (const TCHAR* Name : {TEXT("google"), TEXT("discord"), TEXT("github"), TEXT("apple"), TEXT("microsoft")})
	{
		if (Key.Contains(Name, ESearchCase::IgnoreCase))
		{
			return Name;
		}
	}
	return Key.Contains(TEXT("mock"), ESearchCase::IgnoreCase) ? TEXT("wand") : TEXT("key");
}
}

class SConstructSignIn : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SConstructSignIn)
	{
	}
	SLATE_ARGUMENT(TWeakObjectPtr<UConstructSignInWidget>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Owner = Args._Owner;
		using namespace ConstructLogin;
		Background = FSlateColorBrush(Color(0x09090A));
		Card = FSlateRoundedBoxBrush(Color(0x101111), 10.0f, Color(0x2B2B2D), 1.0f);
		Line = FSlateColorBrush(Color(0x2B2B2D));
		Primary.SetNormal(FSlateRoundedBoxBrush(Gold, 7.0f))
		    .SetHovered(FSlateRoundedBoxBrush(Color(0xE8BE40), 7.0f))
		    .SetPressed(FSlateRoundedBoxBrush(Color(0xB8901F), 7.0f))
		    .SetDisabled(FSlateRoundedBoxBrush(Color(0x63521F), 7.0f))
		    .SetNormalForeground(Color(0x0A0A0A))
		    .SetHoveredForeground(Color(0x0A0A0A))
		    .SetPressedForeground(Color(0x0A0A0A))
		    .SetDisabledForeground(Muted);
		Secondary.SetNormal(FSlateRoundedBoxBrush(Color(0x19191B), 7.0f, Color(0x444448), 1.0f))
		    .SetHovered(FSlateRoundedBoxBrush(Color(0x28282A), 7.0f, Color(0x66666A), 1.0f))
		    .SetPressed(FSlateRoundedBoxBrush(Color(0x101011), 7.0f))
		    .SetDisabled(FSlateRoundedBoxBrush(Color(0x141415), 7.0f))
		    .SetNormalForeground(Ink)
		    .SetHoveredForeground(Ink)
		    .SetPressedForeground(Ink)
		    .SetDisabledForeground(Muted);
		Ghost = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
		Ghost.SetNormalForeground(Ink).SetHoveredForeground(Gold).SetPressedForeground(Gold);
		Input = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
		Input.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Color(0x101011), 7.0f, Color(0x444448), 1.0f))
		    .SetBackgroundImageHovered(FSlateRoundedBoxBrush(Color(0x101011), 7.0f, Color(0x66666A), 1.0f))
		    .SetBackgroundImageFocused(FSlateRoundedBoxBrush(Color(0x101011), 7.0f, Gold, 1.0f))
		    .SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Color(0x101011), 7.0f))
		    .SetForegroundColor(Ink)
		    .SetPadding(FMargin(16, 12));
		LoadIcons();
		TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(LOCTEXT("Crowded", "CROWDED"), 58, true, Ink)];
		Column->AddSlot()
		    .AutoHeight()
		    .HAlign(HAlign_Center)
		    .Padding(0, -9, 0, 0)[Text(LOCTEXT("Kingdoms", "KINGDOMS"), 58, true, Gold)];
		Column->AddSlot()
		    .AutoHeight()
		    .HAlign(HAlign_Center)
		    .Padding(0, 12, 0, 24)[Text(LOCTEXT("Construct", "T H E   C O N S T R U C T"), 16, true, Muted)];

		TSharedRef<SVerticalBox> Form = SNew(SVerticalBox);
		Form->AddSlot().AutoHeight()[Text(LOCTEXT("SignInTitle", "Sign in"), 38, true, Ink)];
		Form->AddSlot().AutoHeight().Padding(
		    0, 4, 0, 24)[Text(LOCTEXT("Subtitle", "Use your Crowdy account to continue."), 18, false, Muted)];
		Form->AddSlot().AutoHeight()[SAssignNew(ProvidersContainer, SVerticalBox)];
		Form->AddSlot().AutoHeight()[Text(LOCTEXT("Email", "Email"), 17, false, Ink)];
		Form->AddSlot().AutoHeight().Padding(
		    0, 8, 0, 20)[SNew(SBox).HeightOverride(52)[SAssignNew(Email, SEditableTextBox)
		                                                   .Style(&Input)
		                                                   .Font(ConstructLogin::Font("Regular", 18))
		                                                   .HintText(LOCTEXT("EmailHint", "you@example.com"))
		                                                   .IsEnabled_Lambda(
		                                                       [this]()
		                                                       {
			                                                       return CanSubmit();
		                                                       })]];
		Form->AddSlot().AutoHeight()[Text(LOCTEXT("Password", "Password"), 17, false, Ink)];
		Form->AddSlot().AutoHeight().Padding(
		    0, 8, 0, 20)[SNew(SBox).HeightOverride(52)[SAssignNew(Password, SEditableTextBox)
		                                                   .Style(&Input)
		                                                   .Font(ConstructLogin::Font("Regular", 18))
		                                                   .IsPassword(true)
		                                                   .HintText(LOCTEXT("PasswordHint", "Password"))
		                                                   .IsEnabled_Lambda(
		                                                       [this]()
		                                                       {
			                                                       return CanSubmit();
		                                                       })
		                                                   .OnTextCommitted_Lambda(
		                                                       [this](const FText&, ETextCommit::Type Type)
		                                                       {
			                                                       if (Type == ETextCommit::OnEnter)
			                                                       {
				                                                       Submit(false);
			                                                       }
		                                                       })]];
		Form->AddSlot().AutoHeight()[Button(LOCTEXT("SignIn", "Sign In"),
		                                    TEXT(""),
		                                    true,
		                                    [this]()
		                                    {
			                                    Submit(false);
		                                    })];
		Form->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[Button(LOCTEXT("EmailLink", "Email me a sign-in link"),
		                                                         TEXT("mail"),
		                                                         false,
		                                                         [this]()
		                                                         {
			                                                         if (Owner.IsValid())
			                                                         {
				                                                         Owner->SubmitEmailLink(
				                                                             Email->GetText().ToString());
				                                                         Password->SetText(FText::GetEmpty());
			                                                         }
		                                                         })];
		Form->AddSlot()
		    .AutoHeight()
		    .HAlign(HAlign_Center)
		    .Padding(0,
		             16,
		             0,
		             18)[SNew(SHorizontalBox) +
		                 SHorizontalBox::Slot().AutoWidth().VAlign(
		                     VAlign_Center)[Text(LOCTEXT("NewHere", "New here? "), 17, false, Muted)] +
		                 SHorizontalBox::Slot()
		                     .AutoWidth()[SNew(SButton)
		                                      .ButtonStyle(&Ghost)
		                                      .ContentPadding(FMargin(0, 4))
		                                      .IsEnabled_Lambda(
		                                          [this]()
		                                          {
			                                          return CanSubmit();
		                                          })
		                                      .OnClicked_Lambda(
		                                          [this]()
		                                          {
			                                          Submit(true);
			                                          return FReply::Handled();
		                                          })[Text(LOCTEXT("CreateAccount", "Create Account"), 17, true, Ink)]]];
		Form->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(1)[SNew(SImage).Image(&Line)]];
		Form->AddSlot().AutoHeight().Padding(
		    0, 14, 0, 0)[SNew(STextBlock)
		                     .Text_Lambda(
		                         [this]()
		                         {
			                         return Owner.IsValid() ? Owner->GetMessage() : FText::GetEmpty();
		                         })
		                     .Font(ConstructLogin::Font("Regular", 14))
		                     .Justification(ETextJustify::Center)
		                     .AutoWrapText(true)
		                     .ColorAndOpacity_Lambda(
		                         [this]()
		                         {
			                         return Owner.IsValid() && Owner->HasError() ? Color(0xE05260) : Muted;
		                         })];
		Form->AddSlot()
		    .AutoHeight()
		    .HAlign(HAlign_Center)
		    .Padding(0, 6, 0, 0)[SNew(SButton)
		                             .ButtonStyle(&Ghost)
		                             .Visibility_Lambda(
		                                 [this]()
		                                 {
			                                 return Owner.IsValid() && Owner->IsBusy() ? EVisibility::Visible
			                                                                           : EVisibility::Collapsed;
		                                 })
		                             .OnClicked_Lambda(
		                                 [this]()
		                                 {
			                                 if (Owner.IsValid())
			                                 {
				                                 Owner->CancelSignIn();
			                                 }
			                                 return FReply::Handled();
		                                 })[Text(LOCTEXT("Cancel", "Cancel sign-in"), 14, false, Muted)]];
		Column->AddSlot().AutoHeight()[SNew(SBorder).BorderImage(&Card).Padding(FMargin(36, 24, 36, 24))[Form]];
		Column->AddSlot()
		    .AutoHeight()
		    .HAlign(HAlign_Center)
		    .Padding(
		        0, 16, 0, 0)[SNew(SButton)
		                         .ButtonStyle(&Ghost)
		                         .ContentPadding(FMargin(8, 5))
		                         .OnClicked_Lambda(
		                             [this]()
		                             {
			                             bConnectionExpanded = !bConnectionExpanded;
			                             return FReply::Handled();
		                             })[SNew(SHorizontalBox) +
		                                SHorizontalBox::Slot()
		                                    .AutoWidth()
		                                    .VAlign(VAlign_Center)
		                                    .Padding(0, 0, 10, 0)[Icon(TEXT("chevron-down"), 16)] +
		                                SHorizontalBox::Slot().AutoWidth()[Text(
		                                    LOCTEXT("ConnectionSettings", "Connection settings"), 17, false, Muted)]]];
		Column->AddSlot().AutoHeight().Padding(
		    12, 4)[SNew(STextBlock)
		               .Text_Lambda(
		                   [this]()
		                   {
			                   return Owner.IsValid() ? Owner->GetConnectionDescription() : FText::GetEmpty();
		                   })
		               .Visibility_Lambda(
		                   [this]()
		                   {
			                   return bConnectionExpanded ? EVisibility::Visible : EVisibility::Collapsed;
		                   })
		               .Font(ConstructLogin::Font("Regular", 14))
		               .ColorAndOpacity(Muted)
		               .AutoWrapText(true)
		               .Justification(ETextJustify::Center)];
		ChildSlot[SNew(SBorder)
		              .BorderImage(&Background)
		              .Padding(0)
		                  [SNew(SScaleBox)
		                       .Stretch(EStretch::ScaleToFit)
		                       .HAlign(HAlign_Center)
		                       .VAlign(VAlign_Center)[SNew(SBox).WidthOverride(1920).HeightOverride(
		                           1080)[SNew(SOverlay) +
		                                 SOverlay::Slot()
		                                     .HAlign(HAlign_Center)
		                                     .VAlign(VAlign_Top)
		                                     .Padding(0, 50, 0, 24)[SNew(SBox).WidthOverride(
		                                         552)[SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed) +
		                                              SScrollBox::Slot()[Column]]] +
		                                 SOverlay::Slot()
		                                     .HAlign(HAlign_Right)
		                                     .VAlign(VAlign_Top)
		                                     .Padding(0, 20, 28, 0)[SNew(SButton).ButtonStyle(&Ghost).OnClicked_Lambda(
		                                         [this]()
		                                         {
			                                         if (Owner.IsValid())
			                                         {
				                                         Owner->ExitApplication();
			                                         }
			                                         return FReply::Handled();
		                                         })[Text(LOCTEXT("Exit", "Exit"), 16, false, Muted)]]]]]];
		RebuildProviders();
	}

	void RebuildProviders()
	{
		ProvidersContainer->ClearChildren();
		if (!Owner.IsValid())
		{
			return;
		}
		if (Owner->HasProviderDiscoveryError())
		{
			ProvidersContainer->AddSlot().AutoHeight().Padding(
			    0, 0, 0, 14)[SNew(SButton)
			                     .ButtonStyle(&Secondary)
			                     .ContentPadding(FMargin(12, 8))
			                     .IsEnabled_Lambda(
			                         [this]()
			                         {
				                         return Owner.IsValid() && !Owner->IsProviderQueryPending();
			                         })
			                     .OnClicked_Lambda(
			                         [this]()
			                         {
				                         if (Owner.IsValid())
				                         {
					                         Owner->RefreshProviders();
				                         }
				                         return FReply::Handled();
			                         })[Text(LOCTEXT("RetryProviders", "Social options unavailable. Click to retry."),
			                                 14,
			                                 false,
			                                 ConstructLogin::Muted)]];
		}
		if (Owner->GetProviders().IsEmpty())
		{
			return;
		}
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		for (const FString& Provider : Owner->GetProviders())
		{
			Rows->AddSlot().AutoHeight().Padding(
			    0, 0, 0, 14)[Button(FText::Format(LOCTEXT("ContinueWith", "Continue with {0}"),
			                                      FText::FromString(ConstructLogin::ProviderName(Provider))),
			                        ConstructLogin::ProviderIcon(Provider),
			                        false,
			                        [this, Provider]()
			                        {
				                        if (Owner.IsValid())
				                        {
					                        Owner->SubmitProvider(Provider);
					                        Password->SetText(FText::GetEmpty());
				                        }
			                        })];
		}
		ProvidersContainer->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(
		    168)[SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed) + SScrollBox::Slot()[Rows]]];
		ProvidersContainer->AddSlot().AutoHeight().Padding(
		    0,
		    20,
		    0,
		    28)[SNew(SHorizontalBox) +
		        SHorizontalBox::Slot().FillWidth(1).VAlign(
		            VAlign_Center)[SNew(SBox).HeightOverride(1)[SNew(SImage).Image(&Line)]] +
		        SHorizontalBox::Slot().AutoWidth().Padding(
		            12, 0)[Text(LOCTEXT("Divider", "or continue with email"), 16, false, ConstructLogin::Muted)] +
		        SHorizontalBox::Slot().FillWidth(1).VAlign(
		            VAlign_Center)[SNew(SBox).HeightOverride(1)[SNew(SImage).Image(&Line)]]];
	}
	void ClearPassword()
	{
		if (Password)
		{
			Password->SetText(FText::GetEmpty());
		}
	}

private:
	TSharedRef<SWidget> Text(const FText& Value, int32 Size, bool bBold, FLinearColor Tint)
	{
		return SNew(STextBlock)
		    .Text(Value)
		    .Font(ConstructLogin::Font(bBold ? "Bold" : "Regular", Size))
		    .ColorAndOpacity(Tint);
	}
	TSharedRef<SWidget> Icon(const FString& Name, float Size)
	{
		const TSharedPtr<FSlateVectorImageBrush>* Brush = Icons.Find(Name);
		return SNew(SBox).WidthOverride(Size).HeightOverride(
		    Size)[SNew(SImage).Image(Brush ? Brush->Get() : nullptr).ColorAndOpacity(ConstructLogin::Ink)];
	}
	TSharedRef<SWidget> Button(const FText& Caption, const FString& IconName, bool bPrimary, TFunction<void()> Action)
	{
		return SNew(SBox).HeightOverride(
		    58)[SNew(SButton)
		            .ButtonStyle(bPrimary ? &Primary : &Secondary)
		            .ContentPadding(FMargin(20, 8))
		            .HAlign(HAlign_Fill)
		            .IsEnabled_Lambda(
		                [this]()
		                {
			                return CanSubmit();
		                })
		            .OnClicked_Lambda(
		                [Action = MoveTemp(Action)]()
		                {
			                Action();
			                return FReply::Handled();
		                })[SNew(SOverlay) +
		                   SOverlay::Slot()
		                       .HAlign(HAlign_Left)
		                       .VAlign(VAlign_Center)[SNew(SBox).Visibility(
		                           IconName.IsEmpty() ? EVisibility::Collapsed
		                                              : EVisibility::HitTestInvisible)[Icon(IconName, 26)]] +
		                   SOverlay::Slot()
		                       .HAlign(HAlign_Center)
		                       .VAlign(VAlign_Center)[Text(Caption,
		                                                   bPrimary ? 21 : 19,
		                                                   true,
		                                                   bPrimary ? ConstructLogin::Color(0x0A0A0A)
		                                                            : ConstructLogin::Ink)]]];
	}
	bool CanSubmit() const
	{
		return Owner.IsValid() && !Owner->IsBusy();
	}
	void Submit(bool bRegister)
	{
		if (CanSubmit())
		{
			Owner->SubmitPassword(Email->GetText().ToString(), Password->GetText().ToString(), bRegister);
			ClearPassword();
		}
	}
	void LoadIcons()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ConstructShell"));
		if (!Plugin)
		{
			return;
		}
		for (const TCHAR* Name : {TEXT("google"),
		                          TEXT("discord"),
		                          TEXT("github"),
		                          TEXT("apple"),
		                          TEXT("microsoft"),
		                          TEXT("wand"),
		                          TEXT("key"),
		                          TEXT("mail"),
		                          TEXT("chevron-down")})
		{
			const FString File =
			    FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Icons"), FString(Name) + TEXT(".svg"));
			Icons.Add(Name, MakeShared<FSlateVectorImageBrush>(File, FVector2D(26, 26)));
		}
	}
	TWeakObjectPtr<UConstructSignInWidget> Owner;
	TSharedPtr<SEditableTextBox> Email;
	TSharedPtr<SEditableTextBox> Password;
	TSharedPtr<SVerticalBox> ProvidersContainer;
	TMap<FString, TSharedPtr<FSlateVectorImageBrush>> Icons;
	FSlateColorBrush Background{FLinearColor::Black};
	FSlateColorBrush Line{FLinearColor::Transparent};
	FSlateRoundedBoxBrush Card{FLinearColor::Transparent};
	FButtonStyle Primary;
	FButtonStyle Secondary;
	FButtonStyle Ghost;
	FEditableTextBoxStyle Input;
	bool bConnectionExpanded = false;
};

void UConstructSignInWidget::SetDesignPreview(bool bEnabled)
{
	bDesignPreview = bEnabled;
	Providers = bEnabled ? TArray<FString>{TEXT("google"), TEXT("discord")} : TArray<FString>{};
	if (SignInView)
	{
		SignInView->RebuildProviders();
	}
}

TSharedRef<SWidget> UConstructSignInWidget::RebuildWidget()
{
	if (IsDesignTime())
	{
		SetDesignPreview(true);
	}
	return SAssignNew(SignInView, SConstructSignIn).Owner(this);
}

UCrowdyAuthentication* UConstructSignInWidget::GetAuthentication() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrowdyAuthentication>() : nullptr;
}

void UConstructSignInWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (bDesignPreview || IsDesignTime())
	{
		return;
	}
	if (UCrowdyAuthentication* Auth = GetAuthentication())
	{
		Auth->OnLegalAcceptanceRequired.AddUniqueDynamic(this, &ThisClass::LegalRequired);
		Auth->OnLegalAcceptanceEnded.AddUniqueDynamic(this, &ThisClass::LegalEnded);
		FetchProviders();
	}
}

void UConstructSignInWidget::FetchProviders()
{
	if (bDesignPreview || IsDesignTime() || bProviderQueryPending)
	{
		return;
	}
	bProviderDiscoveryError = false;
	if (UCrowdyAuthentication* Auth = GetAuthentication())
	{
		bProviderQueryPending = true;
		FOnLoginProvidersReceived Success;
		Success.BindDynamic(this, &ThisClass::ProvidersReceived);
		FOnAuthError Error;
		Error.BindDynamic(this, &ThisClass::ProvidersFailed);
		Auth->GetAvailableLoginProviders(Success, Error);
	}
}

bool UConstructSignInWidget::BeginRequest(const FText& PendingMessage)
{
	if (bPending || bDesignPreview)
	{
		return false;
	}
	if (!GetAuthentication())
	{
		SetMessage(LOCTEXT("NoSDK", "Crowdy authentication is unavailable in this world."), true);
		return false;
	}
	bPending = true;
	ActiveRequest = NewObject<UConstructSignInRequest>(this);
	ActiveRequest->Initialize(this);
	SetMessage(PendingMessage);
	return true;
}

void UConstructSignInWidget::SubmitPassword(const FString& Email, const FString& Password, bool bRegister)
{
	if (bPending)
	{
		return;
	}
	if (Email.TrimStartAndEnd().IsEmpty() || Password.IsEmpty())
	{
		SetMessage(LOCTEXT("CredentialsRequired", "Enter your email and password to continue."), true);
		return;
	}
	if (!BeginRequest(bRegister ? LOCTEXT("Registering", "Creating your account...")
	                            : LOCTEXT("SigningIn", "Signing in...")))
	{
		return;
	}
	FOnAuthSuccess Success;
	Success.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Succeeded);
	FOnAuthError Error;
	Error.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Failed);
	if (bRegister)
	{
		GetAuthentication()->Register(Email.TrimStartAndEnd(), Password, Success, Error);
	}
	else
	{
		GetAuthentication()->Login(Email.TrimStartAndEnd(), Password, Success, Error);
	}
}

void UConstructSignInWidget::SubmitEmailLink(const FString& Email)
{
	if (bPending)
	{
		return;
	}
	if (Email.TrimStartAndEnd().IsEmpty())
	{
		SetMessage(LOCTEXT("EmailRequired", "Enter your email above to request a sign-in link."), true);
		return;
	}
	if (!BeginRequest(
	        LOCTEXT("WaitingForLink", "Requesting a sign-in link. Follow the email link to finish signing in.")))
	{
		return;
	}
	FOnAuthSuccess Success;
	Success.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Succeeded);
	FOnAuthError Error;
	Error.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Failed);
	GetAuthentication()->BeginMagicLinkSignIn(Email.TrimStartAndEnd(), Success, Error);
}

void UConstructSignInWidget::SubmitProvider(const FString& Provider)
{
	if (!Providers.Contains(Provider))
	{
		return;
	}
	if (!BeginRequest(
	        LOCTEXT("WaitingForBrowser", "Continue in your browser. This screen will update when sign-in finishes.")))
	{
		return;
	}
	FOnAuthSuccess Success;
	Success.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Succeeded);
	FOnAuthError Error;
	Error.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Failed);
	GetAuthentication()->BeginSocialSignIn(Provider, Success, Error);
}

void UConstructSignInWidget::RestoreSavedSession()
{
	if (!BeginRequest(LOCTEXT("Restoring", "Restoring your saved session...")))
	{
		return;
	}
	bRestoring = true;
	FOnAuthSuccess Success;
	Success.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Succeeded);
	FOnAuthError Error;
	Error.BindDynamic(ActiveRequest.Get(), &UConstructSignInRequest::Failed);
	GetAuthentication()->RestoreSession(Success, Error);
}

void UConstructSignInWidget::AuthSucceeded(FCrowdyAuthResult Result)
{
	AccessNotice = FText::GetEmpty();
	ActiveRequest = nullptr;
	bPending = false;
	bRestoring = false;
	if (SignInView)
	{
		SignInView->ClearPassword();
	}
	SetMessage(LOCTEXT("SignedIn", "Signed in. Connecting to the app..."));
}

void UConstructSignInWidget::AuthFailed(FString Error)
{
	ActiveRequest = nullptr;
	const bool bNoSavedSession = bRestoring && Error.Contains(TEXT("No saved session"), ESearchCase::IgnoreCase);
	bPending = false;
	bRestoring = false;
	if (bNoSavedSession)
	{
		SetMessage(LOCTEXT("Ready", "Choose a sign-in method to continue."));
	}
	else
	{
		SetMessage(FText::FromString(Error), true);
	}
}

void UConstructSignInWidget::ProvidersReceived(const TArray<FString>& EnabledProviders)
{
	bProviderQueryPending = false;
	bProviderDiscoveryError = false;
	Providers = EnabledProviders;
	if (SignInView)
	{
		SignInView->RebuildProviders();
	}
}

void UConstructSignInWidget::ProvidersFailed(FString Error)
{
	bProviderQueryPending = false;
	bProviderDiscoveryError = true;
	Providers.Reset();
	if (SignInView)
	{
		SignInView->RebuildProviders();
	}
	// Discovery failure must not disable the independent email/password path.
}

void UConstructSignInWidget::SetMessage(const FText& Text, bool bError)
{
	Message = Text;
	bMessageIsError = bError;
}

void UConstructSignInWidget::ShowAccessEnded(const FString& Reason, const FString& BlockedUntil)
{
	AccessNotice = FText::FromString(BlockedUntil.IsEmpty() ? Reason : Reason + TEXT(" Until: ") + BlockedUntil);
}

void UConstructSignInWidget::LegalRequired()
{
	SetMessage(LOCTEXT("LegalPending", "Review the Crowdy terms prompt to finish signing in."));
}

void UConstructSignInWidget::LegalEnded()
{
	if (bPending)
	{
		SetMessage(LOCTEXT("Finishing", "Finishing sign-in..."));
	}
}

void UConstructSignInWidget::CancelSignIn()
{
	if (bDesignPreview || !bPending)
	{
		return;
	}
	// Drop the request first so the SDK's cancellation reply cannot complete it again.
	ActiveRequest = nullptr;
	bPending = false;
	bRestoring = false;
	if (UCrowdyAuthentication* Auth = GetAuthentication())
	{
		Auth->CancelSignIn();
	}
	SetMessage(LOCTEXT("Cancelled", "Sign-in cancelled. Choose a sign-in method to continue."));
}

void UConstructSignInWidget::ExitApplication()
{
	if (bDesignPreview)
	{
		return;
	}
	CancelSignIn();
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

FText UConstructSignInWidget::GetConnectionDescription() const
{
	const UCrowdySDKDeveloperSettings* Settings = GetDefault<UCrowdySDKDeveloperSettings>();
	const UEnum* EnvironmentEnum = StaticEnum<ECrowdyEnvironment>();
	const FText Environment = EnvironmentEnum->GetDisplayNameTextByValue(static_cast<int64>(Settings->Environment));
	return FText::Format(
	    LOCTEXT(
	        "ConnectionInfo",
	        "Crowdy backend: {0}\nUses this project's SDK configuration. Connection settings are managed in Crowdy Studio."),
	    Environment);
}

void UConstructSignInWidget::NativeDestruct()
{
	if (UCrowdyAuthentication* Auth = GetAuthentication())
	{
		Auth->OnLegalAcceptanceRequired.RemoveDynamic(this, &ThisClass::LegalRequired);
		Auth->OnLegalAcceptanceEnded.RemoveDynamic(this, &ThisClass::LegalEnded);
	}
	if (SignInView)
	{
		SignInView->ClearPassword();
	}
	Super::NativeDestruct();
}

void UConstructSignInWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SignInView.Reset();
}

void UConstructSignInRequest::Initialize(UConstructSignInWidget* InOwner)
{
	Owner = InOwner;
}

void UConstructSignInRequest::Succeeded(FCrowdyAuthResult Result)
{
	if (Owner.IsValid() && Owner->ActiveRequest == this)
	{
		Owner->AuthSucceeded(Result);
	}
}

void UConstructSignInRequest::Failed(FString Error)
{
	if (Owner.IsValid() && Owner->ActiveRequest == this)
	{
		Owner->AuthFailed(Error);
	}
}

#undef LOCTEXT_NAMESPACE
