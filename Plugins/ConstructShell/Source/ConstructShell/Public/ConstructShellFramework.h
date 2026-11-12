#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ConstructShellFramework.generated.h"

class UConstructSignInWidget;

UCLASS(BlueprintType, Blueprintable)
class CONSTRUCTSHELL_API UConstructConnectionStatus : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Construct|Shell")
	FText GetStatusCaption() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** Installs runtime login and connection views for maps using the Construct shell. */
UCLASS(Blueprintable)
class CONSTRUCTSHELL_API AConstructShellHUD : public AHUD
{
	GENERATED_BODY()

public:
	AConstructShellHUD();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Construct|Shell")
	TSubclassOf<UConstructSignInWidget> SignInWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Construct|Shell")
	TSubclassOf<UConstructConnectionStatus> StatusWidgetClass;

public:
	void SetLocalPreviewEnabled(bool bEnabled);
	bool IsLocalPreviewEnabled() const
	{
		return bLocalPreview;
	}
	bool IsSignInVisible() const
	{
		return bShowingSignIn;
	}
	void ShowSignIn();

private:
	UFUNCTION()
	void RefreshVisibility();
	UFUNCTION()
	void AddressChanged(bool bSuccess, bool bGateKeep);
	UFUNCTION()
	void PauseChanged(bool bPaused, const FString& Reason);
	bool bLocalPreview = false;
	UFUNCTION()
	void LoggedOut(bool bSuccess);
	UFUNCTION()
	void AccessEnded(const FString& Reason, const FString& BlockedUntil);
	UPROPERTY(Transient)
	TObjectPtr<UConstructSignInWidget> SignIn;
	UPROPERTY(Transient)
	TObjectPtr<UConstructConnectionStatus> ConnectionStatus;
	bool bShowingSignIn = false;
	void ResumeAfterIdle();
	// The player's last input time when the session was seen released for idleness; negative while it is not.
	double AwaySince = -1.0;
};

UCLASS(Blueprintable)
class CONSTRUCTSHELL_API AConstructShellGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AConstructShellGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UFUNCTION()
	virtual void Connected();
	UFUNCTION()
	void PauseChanged(bool bPaused, const FString& Reason);
	UFUNCTION()
	void LegalEnded();
	UFUNCTION()
	void LoggedOut(bool bSuccess);
	UFUNCTION()
	void AccessEnded(const FString& Reason, const FString& BlockedUntil);
	void ReleasePlayerPawn();
};
