#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Subsystem/CrowdyChannels.h"
#include "Subsystem/CrowdyProximityChat.h"
#include "ConstructChatComponent.generated.h"

class APlayerController;
class UGameInstance;
struct FCrowdySendRefusal;
class UConstructChatComponent;
class UConstructChatPanel;
class UConstructUIInputSubsystem;
class UInputComponent;
class SEditableTextBox;

/** One membership request, isolated from later requests and account changes. */
UCLASS(Transient)
class UConstructChatLookup : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UConstructChatComponent* InOwner);
	UFUNCTION()
	void Succeeded(const TArray<FCrowdyChannelMembership>& Values);
	UFUNCTION()
	void Failed(FCrowdyChannelError Error, FString Message);

private:
	bool IsCurrent() const;
	TWeakObjectPtr<UConstructChatComponent> Owner;
	uint64 Generation = 0;
	int64 UserID = 0;
	int64 AppID = 0;
};

/** Runtime local-player chat presentation. The SDK owns membership, delivery and permissions. */
UCLASS(ClassGroup = (Construct), BlueprintType, meta = (BlueprintSpawnableComponent))
class CONSTRUCTCHAT_API UConstructChatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UConstructChatComponent();
	virtual void BeginPlay() override;
	/** Completes local setup if the controller was assigned after this component began play. */
	void InitializeLocalRuntime();
	/** Releases this controller's presentation without destroying an authored component. Safe to call repeatedly. */
	void ShutdownLocalRuntime();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	UPROPERTY(EditAnywhere, Category = "Construct|Chat")
	FString DedicatedChannelName = TEXT("construct_chat");
	UFUNCTION(BlueprintCallable, Category = "Construct|Chat")
	void EnableChannelChat();
	UFUNCTION(BlueprintCallable, Category = "Construct|Chat")
	void UseNearbyChat();
	UFUNCTION(BlueprintCallable, Category = "Construct|Chat")
	void RefreshChannel();
	UFUNCTION(BlueprintCallable, Category = "Construct|Chat")
	void SendText(const FString& Text);
	UFUNCTION(BlueprintCallable, Category = "Construct|Chat")
	void OpenComposer();
	UFUNCTION(BlueprintCallable, Category = "Construct|Chat")
	void CloseComposer();
	UFUNCTION(BlueprintCallable, Category = "Construct|Chat")
	void SubmitComposer(const FString& Text);
	UFUNCTION(BlueprintPure, Category = "Construct|Chat")
	FText GetStatus() const
	{
		return FText::FromString(Status);
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Chat")
	FText GetHistory() const
	{
		return HistoryText;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Chat")
	bool CanSend() const;
	UFUNCTION(BlueprintPure, Category = "Construct|Chat")
	bool IsComposerOpen() const
	{
		return bComposerOpen;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Chat")
	bool IsChannelMode() const
	{
		return bChannelMode;
	}
	UFUNCTION(BlueprintPure, Category = "Construct|Chat")
	bool IsSignedIn() const;
	static FString SanitizeOutgoing(const FString& Text);
	static FString SanitizeIncoming(const FString& Text, int32 Maximum = 240);
	static bool Encode(const FString& Text, TArray<uint8>& Bytes);
	static bool Decode(const TArray<uint8>& Bytes, FString& Text);

private:
	friend class UConstructChatPanel;
	friend class UConstructChatLookup;
	friend class FConstructChatSessionIsolationTest;
	friend class FConstructChatHistoryTest;
	friend class FConstructChatLifecycleTest;
	friend class FConstructChatControllerReplacementTest;
	friend class FConstructChatProximityPresentationTest;
	friend class FConstructChatProximitySessionTest;
	APlayerController* LocalController() const;
	UCrowdyChannels* Channels() const;
	UCrowdyProximityChat* ProximityChat() const;
	FString ResolveSenderName(const FGuid& SenderEntityId) const;
	void AppendProximityMessage(const FCrowdyProximityText& Message);
	void ApplyProximitySendResult(ECrowdyProximityChatResult Result, const FString& Text);
	void GetCurrentIdentity(int64& OutUserID, int64& OutAppID) const;
	bool HasCurrentIdentity() const;
	bool RefreshLocalPresentation();
	void ReleaseInput();
	void RequestGameplayFocusRestore();
	void CancelGameplayFocusRestore();
	void ResetSession();
	void RefreshVisibility();
	void TogglePanel();
	void Append(const FString& Line);
	void Memberships(const TArray<FCrowdyChannelMembership>& Values);
	void Failed(FCrowdyChannelError Error, FString Message);
	UFUNCTION()
	void SignedIn(bool bSuccess, FString Message);
	UFUNCTION()
	void SignedOut(bool bSuccess);
	UFUNCTION()
	void AccessEnded(const FString& Reason, const FString& Until);
	UFUNCTION()
	void Connected();
	UFUNCTION()
	void Received(int64 Id, FString Sender, const TArray<uint8>& Bytes);
	UFUNCTION()
	void SendRefused(const FCrowdySendRefusal& Refusal);
	UFUNCTION()
	void ProximityReceived(const FCrowdyProximityText& Message);
	UFUNCTION()
	void ProximityRefused(const FCrowdySendRefusal& Refusal);
	UFUNCTION()
	void GameplayInputChanged(bool bAllowed);
	UPROPERTY(Transient)
	TObjectPtr<UConstructChatLookup> ActiveLookup;
	UPROPERTY(Transient)
	TObjectPtr<UConstructChatPanel> Panel;
	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> ChatInput;
	TWeakObjectPtr<UGameInstance> RuntimeInstance;
	TWeakObjectPtr<APlayerController> InputController;
	TWeakObjectPtr<UConstructUIInputSubsystem> InputGate;
	FTimerHandle GameplayFocusTimer;
	uint64 SessionGeneration = 0;
	int64 SessionUserID = 0;
	int64 SessionAppID = 0;
	int32 SetupAttemptsRemaining = 0;
	bool bRuntimeActive = false;
	bool bComposerOpen = false;
	bool bShowHistory = true;
	bool bChannelMode = false;
	bool bLoading = false;
	bool bCanPublish = false;
	int64 ChannelId = 0;
	FString Status = TEXT("Nearby chat reaches players within 4 chunks. Enter to type.");
	TArray<FString> History;
	FText HistoryText;
};

UCLASS(Blueprintable)
class CONSTRUCTCHAT_API UConstructChatPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	void ClearComposer();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual FReply NativeOnFocusReceived(const FGeometry& Geometry, const FFocusEvent& FocusEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	UConstructChatComponent* Chat() const;
	TSharedPtr<SEditableTextBox> Composer;
};
