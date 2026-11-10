#include "ConstructUIInputSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Misc/CoreDelegates.h"

void UConstructUIInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DeactivatedHandle =
	    FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &ThisClass::ApplicationDeactivated);
	ActivatedHandle =
	    FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this, &ThisClass::ApplicationActivated);
}

void UConstructUIInputSubsystem::SetModal(FName Source, bool bVisible, UUserWidget* Focus)
{
	if (Source.IsNone())
	{
		return;
	}
	if (bVisible)
	{
		if (!Modals.Contains(Source))
		{
			ModalOrder.Add(Source);
		}
		Modals.Add(Source, Focus);
	}
	else
	{
		Modals.Remove(Source);
		ModalOrder.Remove(Source);
	}
	ApplyInput();
}

void UConstructUIInputSubsystem::SetSessionReady(bool bReady)
{
	bManaged = true;
	bSessionReady = bReady;
	ApplyInput();
}

void UConstructUIInputSubsystem::ReleaseManagement()
{
	ReleaseControllerInput();
	bPointerInteraction = false;
	if (APlayerController* Controller = AppliedController.Get())
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::PawnChanged);
		if (bOwnsMovementBlock)
		{
			Controller->SetIgnoreMoveInput(false);
			Controller->SetIgnoreLookInput(false);
		}
	}
	ObservePawn(nullptr);
	AppliedController = nullptr;
	AppliedFocus = nullptr;
	bOwnsMovementBlock = false;
	bManaged = false;
	bSessionReady = false;
	bApplied = false;
}

bool UConstructUIInputSubsystem::IsGameplayInputAllowed() const
{
	return bSessionReady && bApplicationActive && Modals.IsEmpty();
}

bool UConstructUIInputSubsystem::CanUseNavigationInput(FName AllowedModal, FName AlternateModal) const
{
	if (!bSessionReady || !bApplicationActive)
	{
		return false;
	}
	for (const auto& Modal : Modals)
	{
		if (Modal.Key != AllowedModal && Modal.Key != AlternateModal)
		{
			return false;
		}
	}
	return true;
}

void UConstructUIInputSubsystem::TogglePointerInteraction()
{
	if (IsGameplayInputAllowed())
	{
		bPointerInteraction = !bPointerInteraction;
		ApplyInput();
	}
}

void UConstructUIInputSubsystem::ReleaseControllerInput()
{
	if (APlayerController* Controller = AppliedController.Get())
	{
		Controller->bShowMouseCursor = bPreviousShowCursor;
		if (PointerInput)
		{
			Controller->PopInputComponent(PointerInput);
		}
	}
	if (PointerInput)
	{
		PointerInput->DestroyComponent();
		PointerInput = nullptr;
	}
	if (UGameViewportClient* Viewport = AppliedViewport.Get())
	{
		if (ULocalPlayer* Player = GetLocalPlayer(); Player && Player->ViewportClient == Viewport)
		{
			Player->GetSlateOperations().ReleaseMouseCapture().ReleaseMouseLock().CancelFocusRequest();
		}
		Viewport->SetMouseCaptureMode(PreviousMouseCapture);
		Viewport->SetMouseLockMode(PreviousMouseLock);
		Viewport->SetHideCursorDuringCapture(bPreviousHideCursorDuringCapture);
		Viewport->SetIgnoreInput(bPreviousIgnoreViewportInput);
	}
	AppliedViewport.Reset();
}

void UConstructUIInputSubsystem::ApplyInput()
{
	if (!bManaged)
	{
		return;
	}
	APlayerController* Controller = GetLocalPlayer() ? GetLocalPlayer()->GetPlayerController(GetWorld()) : nullptr;
	const bool bAllowed = IsGameplayInputAllowed();
	if (!bAllowed)
	{
		bPointerInteraction = false;
	}
	UUserWidget* Focus = nullptr;
	for (int32 Index = ModalOrder.Num() - 1; Index >= 0; --Index)
	{
		if (const auto* Candidate = Modals.Find(ModalOrder[Index]))
		{
			// A modal owned by the SDK may deliberately have no local focus widget.
			Focus = Candidate->Get();
			break;
		}
	}
	if (Modals.Contains(TEXT("CrowdyLegalConsent")))
	{
		Focus = nullptr;
	}
	if (AppliedController.Get() != Controller)
	{
		ReleaseControllerInput();
		bPointerInteraction = false;
		if (APlayerController* Previous = AppliedController.Get())
		{
			Previous->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::PawnChanged);
		}
		ObservePawn(nullptr);
		if (Controller)
		{
			Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::PawnChanged);
			ObservePawn(Controller->GetPawn());
			bPreviousShowCursor = Controller->bShowMouseCursor;
			PointerInput = NewObject<UInputComponent>(Controller);
			PointerInput->Priority = 70;
			PointerInput->RegisterComponent();
			PointerInput->BindKey(EKeys::F4, IE_Pressed, this, &ThisClass::TogglePointerInteraction);
			Controller->PushInputComponent(PointerInput);
		}
		if (APlayerController* Previous = AppliedController.Get(); Previous && bOwnsMovementBlock)
		{
			Previous->SetIgnoreMoveInput(false);
			Previous->SetIgnoreLookInput(false);
		}
		AppliedController = Controller;
		bOwnsMovementBlock = false;
		bApplied = false;
	}
	const bool bLostInputBlock = Controller && !bAllowed && bOwnsMovementBlock &&
	                             (!Controller->IsMoveInputIgnored() || !Controller->IsLookInputIgnored());
	if (bApplied && bLastAllowed == bAllowed && bLastPointerInteraction == bPointerInteraction &&
	    AppliedFocus.Get() == Focus && !bLostInputBlock)
	{
		return;
	}
	const bool bChanged = !bApplied || bLastAllowed != bAllowed;
	bApplied = true;
	bLastAllowed = bAllowed;
	bLastPointerInteraction = bPointerInteraction;
	AppliedFocus = Focus;
	if (Controller)
	{
		if (!bAllowed)
		{
			if (!bOwnsMovementBlock || !Controller->IsMoveInputIgnored())
			{
				Controller->SetIgnoreMoveInput(true);
			}
			if (!bOwnsMovementBlock || !Controller->IsLookInputIgnored())
			{
				Controller->SetIgnoreLookInput(true);
			}
			bOwnsMovementBlock = true;
			Controller->FlushPressedKeys();
			if (APawn* Pawn = Controller->GetPawn())
			{
				Pawn->ConsumeMovementInputVector();
				if (auto* Movement = Pawn->GetMovementComponent())
				{
					Movement->StopMovementImmediately();
				}
			}
		}
		else if (bAllowed && bOwnsMovementBlock)
		{
			Controller->SetIgnoreMoveInput(false);
			Controller->SetIgnoreLookInput(false);
			bOwnsMovementBlock = false;
		}
		if (UGameViewportClient* Viewport = Controller->GetWorld()->GetGameViewport(); Viewport && !AppliedViewport.IsValid())
		{
			AppliedViewport = Viewport;
			PreviousMouseCapture = Viewport->GetMouseCaptureMode();
			PreviousMouseLock = Viewport->GetMouseLockMode();
			bPreviousHideCursorDuringCapture = Viewport->HideCursorDuringCapture();
			bPreviousIgnoreViewportInput = Viewport->IgnoreInput();
		}
		Controller->bShowMouseCursor = !bAllowed || bPointerInteraction;
		if (Controller->bShowMouseCursor)
		{
			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			if (Focus)
			{
				Mode.SetWidgetToFocus(Focus->TakeWidget());
			}
			Controller->SetInputMode(Mode);
		}
		else
		{
			Controller->SetInputMode(FInputModeGameOnly());
		}
	}
	if (bChanged)
	{
		OnGameplayInputChanged.Broadcast(bAllowed);
	}
}

void UConstructUIInputSubsystem::ObservePawn(APawn* Pawn)
{
	if (APawn* Previous = ObservedPawn.Get())
	{
		Previous->ReceiveRestartedDelegate.RemoveDynamic(this, &ThisClass::PawnRestarted);
	}
	ObservedPawn = Pawn;
	if (Pawn)
	{
		Pawn->ReceiveRestartedDelegate.AddUniqueDynamic(this, &ThisClass::PawnRestarted);
	}
}

void UConstructUIInputSubsystem::PawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	ObservePawn(NewPawn);
	bApplied = false;
	ApplyInput();
}

void UConstructUIInputSubsystem::PawnRestarted(APawn* Pawn)
{
	bApplied = false;
	ApplyInput();
}

void UConstructUIInputSubsystem::ApplicationDeactivated()
{
	bApplicationActive = false;
	ApplyInput();
}
void UConstructUIInputSubsystem::ApplicationActivated()
{
	bApplicationActive = true;
	ApplyInput();
}

void UConstructUIInputSubsystem::Deinitialize()
{
	FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivatedHandle);
	FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ActivatedHandle);
	ReleaseManagement();
	Modals.Reset();
	ModalOrder.Reset();
	OnGameplayInputChanged.Clear();
	Super::Deinitialize();
}
