#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyAccountTypes.h"
#include "ConstructPresencePawn.generated.h"

class APlayerCameraManager;
class UConstructPlayerNameWidget;
class UCrowdyGameSession;
struct FConstructPlayerNameTestAccess;
class UCameraComponent;
class UConstructUIInputSubsystem;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
class UStaticMeshComponent;
class UTextRenderComponent;
class UConstructPresenceSubsystem;
struct FConstructPresenceTestAccess;
struct FConstructInputTestAccess;
struct FConstructPresenceSubsystemTestAccess;

/** One locally controlled capsule, or the SDK's pooled presentation of a remote player. */
UCLASS(Blueprintable)
class CONSTRUCTPRESENCE_API AConstructPresencePawn : public ACharacter
{
	GENERATED_BODY()

public:
	AConstructPresencePawn();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Restart() override;
	virtual void UnPossessed() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** Scripted subclasses may keep their transform writer separate from local locomotion. */
	virtual bool ShouldUseLocalCharacterMovement() const;

	UPROPERTY(BlueprintReadOnly,
	          Category = "Construct|Presence",
	          meta = (CrowdyState, CrowdyHeartbeat, CrowdyOnRep = "OnRep_PlayerName"))
	FString PlayerName;

	UFUNCTION()
	virtual void OnRep_PlayerName();

private:
	friend class UConstructPresenceSubsystem;
	friend class UConstructPlayerNameWidget;
	friend struct FConstructPlayerNameTestAccess;
	friend struct FConstructPresenceTestAccess;
	friend struct FConstructInputTestAccess;
	friend struct FConstructPresenceSubsystemTestAccess;

	UFUNCTION()
	void HandleOwnershipAssigned(const FGuid& NewOwnerID, ECrowdyRole NewRole, bool bIsLocallyOwned);
	UFUNCTION()
	void HandleNameRead(FCrowdyPlayerProfile Profile);
	UFUNCTION()
	void HandleNameReadFailed(FCrowdyAccountError Error, FString Message);

	void EnsurePlayerNameWidget();
	void ShutdownPlayerNameWidget();
	bool ApplyConfirmedPlayerProfile(const FCrowdyPlayerProfile& Profile);
	void CommitPlayerProfile(const FCrowdyPlayerProfile& Profile);
	void AddInputMappings();
	static void ConfigureLookMappings(UInputMappingContext* Context,
	                                  UInputAction* MouseAction,
	                                  UInputAction* GamepadAction);
	void RemoveInputMappings();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void LookRate(const FInputActionValue& Value);
	void JumpStarted(const FInputActionValue& Value);
	void TogglePlayerName(const FInputActionValue& Value);
	void JumpReleased(const FInputActionValue& Value);
	UFUNCTION()
	void HandleGameplayInputChanged(bool bAllowed);
	bool CanUseGameplayInput() const;
	void RefreshMovementState();
	void ApplyLocalMovementState(bool bEnabled);

	bool ConsumeNameReply();
	static float ControllerPitchDelta(float AxisY, float Scale);

	void ApplyProfileName(const FCrowdyPlayerProfile& Profile);
	void ApplyNameLookupFailure();
	void SetDisplayedPlayerName(const FString& Name);
	void ResetRemoteName();

	UPROPERTY(VisibleAnywhere, Category = "Construct|Presence")
	TObjectPtr<UCrowdyEntityComponent> Entity;
	UPROPERTY(VisibleAnywhere, Category = "Construct|Presence")
	TObjectPtr<UStaticMeshComponent> CapsuleBody;
	UPROPERTY(VisibleAnywhere, Category = "Construct|Presence")
	TObjectPtr<UStaticMeshComponent> CapsuleTop;
	UPROPERTY(VisibleAnywhere, Category = "Construct|Presence")
	TObjectPtr<UStaticMeshComponent> CapsuleBottom;
	UPROPERTY(VisibleAnywhere, Category = "Construct|Presence")
	TObjectPtr<UTextRenderComponent> NameTag;

	UPROPERTY(VisibleAnywhere, Category = "Construct|Presence")
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookRateAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PlayerNameAction;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSubsystem;
	TWeakObjectPtr<UConstructUIInputSubsystem> InputGate;
	bool bLocalMovementEnabled = false;

	UPROPERTY(Transient)
	TObjectPtr<UConstructPlayerNameWidget> PlayerNameWidget;
	UPROPERTY(Transient)
	FCrowdyPlayerProfile LocalProfile;
	bool bLocalProfileReadComplete = false;
	bool bLocalProfileReadSucceeded = false;
	TWeakObjectPtr<APlayerCameraManager> NameCamera;
	TWeakObjectPtr<UCrowdyGameSession> NameLookupSession;
	FGuid NameLookupEntity;
	FGuid NameLookupOwner;
	int64 NameLookupUser = 0;
	bool bLocalEntryStarted = false;
	bool bNameLookupPending = false;
};
