#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "ConstructUIInputSubsystem.generated.h"

class UUserWidget;
class APlayerController;
class APawn;
class UInputComponent;
class UGameViewportClient;

DECLARE_MULTICAST_DELEGATE(FConstructModsOpenRequested);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FConstructGameplayInputChanged, bool, bAllowed);

/** Coordinates local shell/UI input only. It never pauses the world or manages SDK authentication. */
UCLASS()
class CONSTRUCTSHELL_API UConstructUIInputSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintCallable, Category = "Construct|UI")
	void SetModal(FName Source, bool bVisible, UUserWidget* Focus = nullptr);
	UFUNCTION(BlueprintCallable, Category = "Construct|UI")
	void SetSessionReady(bool bReady);
	/** Stop controlling input for the departing shell while retaining other screens' modal registrations. */
	UFUNCTION(BlueprintCallable, Category = "Construct|UI")
	void ReleaseManagement();
	UFUNCTION(BlueprintPure, Category = "Construct|UI")
	bool IsGameplayInputAllowed() const;
	UFUNCTION(BlueprintPure, Category = "Construct|UI")
	bool CanUseNavigationInput(FName AllowedModal, FName AlternateModal = NAME_None) const;
	UPROPERTY(BlueprintAssignable, Category = "Construct|UI")
	FConstructGameplayInputChanged OnGameplayInputChanged;

    /** Thin UI request only. ConstructWorld owns the menu and domain behavior. */
    FConstructModsOpenRequested OnModsOpenRequested;
    void RequestOpenMods();

private:
	friend class FConstructHUDReleasesInputManagementTest;
	void ApplyInput();
	void TogglePointerInteraction();
	void ReleaseControllerInput();
	void ObservePawn(APawn* Pawn);
	UFUNCTION()
	void PawnChanged(APawn* OldPawn, APawn* NewPawn);
	UFUNCTION()
	void PawnRestarted(APawn* Pawn);
	void ApplicationDeactivated();
	void ApplicationActivated();
	TMap<FName, TWeakObjectPtr<UUserWidget>> Modals;
	TArray<FName> ModalOrder;
	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> PointerInput;
	TWeakObjectPtr<APlayerController> AppliedController;
	TWeakObjectPtr<UGameViewportClient> AppliedViewport;
	EMouseCaptureMode PreviousMouseCapture = EMouseCaptureMode::NoCapture;
	EMouseLockMode PreviousMouseLock = EMouseLockMode::DoNotLock;
	bool bPreviousHideCursorDuringCapture = false;
	bool bPreviousIgnoreViewportInput = false;
	bool bPreviousShowCursor = false;
	TWeakObjectPtr<APawn> ObservedPawn;
	TWeakObjectPtr<UUserWidget> AppliedFocus;
	FDelegateHandle DeactivatedHandle;
	FDelegateHandle ActivatedHandle;
	bool bManaged = false;
	bool bSessionReady = false;
	bool bApplicationActive = true;
	bool bApplied = false;
	bool bLastAllowed = false;
	bool bPointerInteraction = false;
	bool bLastPointerInteraction = false;
	bool bOwnsMovementBlock = false;
};
