#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "ConstructSignInWidget.generated.h"

class SConstructSignIn;
class UConstructSignInWidget;

/** A weak, per-attempt callback receiver keeps canceled replies out of subsequent UI requests. */
UCLASS(Transient)
class UConstructSignInRequest : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UConstructSignInWidget* InOwner);
	UFUNCTION()
	void Succeeded(FCrowdyAuthResult Result);
	UFUNCTION()
	void Failed(FString Error);

private:
	TWeakObjectPtr<UConstructSignInWidget> Owner;
};

/** Player-facing view. Authentication, persistence and browser callbacks remain owned by the SDK. */
UCLASS(BlueprintType, Blueprintable)
class CONSTRUCTSHELL_API UConstructSignInWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetDesignPreview(bool bEnabled);
	bool GetDesignPreview() const
	{
		return bDesignPreview;
	}
	UFUNCTION(BlueprintCallable, Category = "Construct|SignIn")
	void RestoreSavedSession();
	UFUNCTION(BlueprintCallable, Category = "Construct|SignIn")
	void SubmitPassword(const FString& Email, const FString& Password, bool bRegister);
	UFUNCTION(BlueprintCallable, Category = "Construct|SignIn")
	void SubmitEmailLink(const FString& Email);
	UFUNCTION(BlueprintCallable, Category = "Construct|SignIn")
	void SubmitProvider(const FString& Provider);
	UFUNCTION(BlueprintCallable, Category = "Construct|SignIn")
	void CancelSignIn();
	void ExitApplication();
	void ShowAccessEnded(const FString& Reason, const FString& BlockedUntil);
	void SetMessage(const FText& Text, bool bError = false);
	bool IsBusy() const
	{
		return bPending;
	}
	bool HasError() const
	{
		return bMessageIsError || !AccessNotice.IsEmpty();
	}
	FText GetMessage() const
	{
		return AccessNotice.IsEmpty()
		           ? Message
		           : FText::Format(NSLOCTEXT("ConstructShell", "AccessAndMessage", "{0}\n{1}"), AccessNotice, Message);
	}
	const TArray<FString>& GetProviders() const
	{
		return Providers;
	}
	FText GetConnectionDescription() const;
	bool HasProviderDiscoveryError() const
	{
		return bProviderDiscoveryError;
	}
	bool IsProviderQueryPending() const
	{
		return bProviderQueryPending;
	}
	void RefreshProviders()
	{
		FetchProviders();
	}

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	friend class UConstructSignInRequest;
	friend class FConstructSignInRequestIsolationTest;
	friend class FConstructSignInCancellationTest;
	UPROPERTY(Transient)
	TObjectPtr<UConstructSignInRequest> ActiveRequest;
	bool bProviderDiscoveryError = false;
	bool bProviderQueryPending = false;
	UCrowdyAuthentication* GetAuthentication() const;
	bool BeginRequest(const FText& PendingMessage);
	void FetchProviders();
	UFUNCTION()
	void AuthSucceeded(FCrowdyAuthResult Result);
	UFUNCTION()
	void AuthFailed(FString Error);
	UFUNCTION()
	void ProvidersReceived(const TArray<FString>& EnabledProviders);
	UFUNCTION()
	void ProvidersFailed(FString Error);
	UFUNCTION()
	void LegalRequired();
	UFUNCTION()
	void LegalEnded();

	TSharedPtr<SConstructSignIn> SignInView;
	TArray<FString> Providers;
	FText Message = NSLOCTEXT("ConstructShell", "Ready", "Choose a sign-in method to continue.");
	FText AccessNotice;
	bool bPending = false;
	bool bRestoring = false;
	bool bMessageIsError = false;
	bool bDesignPreview = false;
};
