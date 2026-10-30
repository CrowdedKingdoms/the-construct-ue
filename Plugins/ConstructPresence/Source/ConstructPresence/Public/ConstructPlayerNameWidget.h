#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystem/CrowdyAccountTypes.h"
#include "ConstructPlayerNameWidget.generated.h"

class AConstructPresencePawn;
class UConstructUIInputSubsystem;
class UCrowdyAuthentication;
class UCrowdyGameSession;
class UCrowdyEntityComponent;
struct FConstructPlayerNameTestAccess;
class UConstructPlayerNameWidget;
class SConstructPlayerName;

/** A distinct weak receiver prevents abandoned or older requests completing a newer one. */
UCLASS(Transient)
class UConstructPlayerNameRequest : public UObject
{
	GENERATED_BODY()

private:
	friend class UConstructPlayerNameWidget;
	friend struct FConstructPlayerNameTestAccess;
	UFUNCTION()
	void Succeeded(FCrowdyPlayerProfile Profile);
	UFUNCTION()
	void Failed(FCrowdyAccountError Error, FString Message);

	TWeakObjectPtr<UConstructPlayerNameWidget> Widget;
	TWeakObjectPtr<AConstructPresencePawn> Pawn;
	int64 ExpectedUserID = 0;
	FGuid ExpectedNetID;
	FGuid ExpectedOwnerID;
	bool bSave = false;
};

/** Local profile presentation; Crowdy Players remains the profile/request owner. */
UCLASS(BlueprintType)
class CONSTRUCTPRESENCE_API UConstructPlayerNameWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeForPawn(AConstructPresencePawn* Pawn);
	void SetInitialProfile(const FCrowdyPlayerProfile& Profile, bool bReadSucceeded);
	void Shutdown();

	UFUNCTION(BlueprintCallable, Category = "Construct|Player Name")
	void OpenEditor();
	UFUNCTION(BlueprintCallable, Category = "Construct|Player Name")
	void CloseEditor();
	UFUNCTION(BlueprintCallable, Category = "Construct|Player Name")
	void SubmitName(const FString& Gamertag, const FString& Disambiguation);
	UFUNCTION(BlueprintCallable, Category = "Construct|Player Name")
	void RefreshProfile();
	UFUNCTION(BlueprintPure, Category = "Construct|Player Name")
	FString GetGamertag() const
	{
		return DraftGamertag;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Player Name")
	FString GetDisambiguation() const
	{
		return DraftDisambiguation;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Player Name")
	FText GetMessage() const
	{
		return Message;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Player Name")
	bool IsBusy() const
	{
		return ActiveRequest != nullptr;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Player Name")
	bool IsOpen() const
	{
		return bOpen;
	}

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	friend class UConstructPlayerNameRequest;
	friend class SConstructPlayerName;
	friend struct FConstructPlayerNameTestAccess;

	bool ResolveIdentity(int64& UserID, FGuid& NetID, FGuid& OwnerID) const;
	bool CanInteract() const;
	bool CanClose() const;
	bool BeginRequest(bool bSave);
	bool ConsumeRequest(UConstructPlayerNameRequest* Request);
	void RequestSucceeded(UConstructPlayerNameRequest* Request, const FCrowdyPlayerProfile& Profile);
	void RequestFailed(UConstructPlayerNameRequest* Request, const FCrowdyAccountError& Error, const FString& Detail);
	void SetProfileModel(const FCrowdyPlayerProfile& Profile, bool bReplaceDraft);
	void SetDraftGamertag(const FText& Value);
	void SetDraftDisambiguation(const FText& Value);
	void TryInitialPrompt();
	FText BadgeCaption() const;
	UFUNCTION()
	void InputPolicyChanged(bool bAllowed);

	UPROPERTY(Transient)
	TObjectPtr<UConstructPlayerNameRequest> ActiveRequest;
	TWeakObjectPtr<AConstructPresencePawn> OwnerPawn;
	TWeakObjectPtr<UConstructUIInputSubsystem> InputGate;
	TWeakObjectPtr<UCrowdyEntityComponent> Entity;
	TWeakObjectPtr<UCrowdyAuthentication> Authentication;
	TWeakObjectPtr<UCrowdyGameSession> Session;
	TSharedPtr<SConstructPlayerName> View;
	FString CurrentGamertag;
	FString CurrentDisambiguation;
	FString DraftGamertag;
	FString DraftDisambiguation = TEXT("0001");
	FText Message;
	bool bInitialized = false;
	bool bLive = false;
	bool bOpen = false;
	bool bDirty = false;
	bool bSaveConfirmed = false;
	bool bInitialPromptHandled = false;
	bool bInitialPromptPending = false;
	bool bError = false;
};
