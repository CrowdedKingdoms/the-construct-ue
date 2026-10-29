#include "ConstructPreviewPawn.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

AConstructPreviewPawn::AConstructPreviewPawn()
{
	bAddDefaultMovementBindings = false;
	OverrideInputComponentClass = UEnhancedInputComponent::StaticClass();
	SetReplicates(false);
	SetReplicateMovement(false);
}

void AConstructPreviewPawn::AddInputMappings()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input =
	    LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input || MappingContext)
	{
		return;
	}
	MappingSubsystem = Input;
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_ConstructPreview"));
	MoveAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPreviewMove"));
	MoveAction->ValueType = EInputActionValueType::Axis3D;
	MoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
	LookAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPreviewLook"));
	LookAction->ValueType = EInputActionValueType::Axis2D;
	UInputModifierScalar* MouseSensitivity = NewObject<UInputModifierScalar>(MappingContext);
	MouseSensitivity->Scalar = FVector(2.5, -2.5, 1.0);
	LookAction->Modifiers.Add(MouseSensitivity);
	LookRateAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPreviewLookRate"));
	LookRateAction->ValueType = EInputActionValueType::Axis2D;
	LookRateAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
	UInputModifierScalar* RateSensitivity = NewObject<UInputModifierScalar>(MappingContext);
	RateSensitivity->Scalar = FVector(2.5, 2.5, 1.0);
	LookRateAction->Modifiers.Add(RateSensitivity);
	auto MapMove = [this](FKey Key, int32 Axis, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(MoveAction, Key);
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		}
		if (Axis != 0)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Swizzle->Order = Axis == 1 ? EInputAxisSwizzle::YXZ : EInputAxisSwizzle::ZYX;
			Mapping.Modifiers.Add(Swizzle);
		}
	};
	MapMove(EKeys::W, 1, false);
	MapMove(EKeys::S, 1, true);
	MapMove(EKeys::A, 0, true);
	MapMove(EKeys::D, 0, false);
	MapMove(EKeys::Up, 1, false);
	MapMove(EKeys::Down, 1, true);
	MapMove(EKeys::SpaceBar, 2, false);
	MapMove(EKeys::LeftControl, 2, true);
	MapMove(EKeys::C, 2, true);
	MapMove(EKeys::Q, 2, true);
	MapMove(EKeys::Gamepad_LeftX, 0, false);
	MapMove(EKeys::Gamepad_LeftY, 1, false);
	MapMove(EKeys::Gamepad_LeftThumbstick, 2, false);
	MapMove(EKeys::Gamepad_RightThumbstick, 2, true);
	MapMove(EKeys::Gamepad_FaceButton_Bottom, 2, false);
	MappingContext->MapKey(LookAction, EKeys::Mouse2D);
	MappingContext->MapKey(LookRateAction, EKeys::Gamepad_RightX);
	FEnhancedActionKeyMapping& VerticalLook = MappingContext->MapKey(LookRateAction, EKeys::Gamepad_RightY);
	UInputModifierSwizzleAxis* LookSwizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
	LookSwizzle->Order = EInputAxisSwizzle::YXZ;
	VerticalLook.Modifiers.Add(LookSwizzle);
	MappingContext->MapKey(LookRateAction, EKeys::Right);
	MappingContext->MapKey(LookRateAction, EKeys::Left).Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Input->AddMappingContext(MappingContext, 0, Options);
}

void AConstructPreviewPawn::RemoveInputMappings()
{
	if (UEnhancedInputLocalPlayerSubsystem* Input = MappingSubsystem.Get())
	{
		if (MappingContext)
		{
			Input->RemoveMappingContext(MappingContext);
		}
	}
	MappingContext = nullptr;
	MoveAction = nullptr;
	LookAction = nullptr;
	LookRateAction = nullptr;
	MappingSubsystem.Reset();
}

void AConstructPreviewPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	AddInputMappings();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent); Input && MappingContext)
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);
		Input->BindAction(LookRateAction, ETriggerEvent::Triggered, this, &ThisClass::LookRate);
	}
}

void AConstructPreviewPawn::Move(const FInputActionValue& Value)
{
	if (!IsLocallyControlled() || !Controller || Controller->IsMoveInputIgnored())
	{
		return;
	}
	const FVector Axis = Value.Get<FVector>();
	const FRotationMatrix Rotation(Controller->GetControlRotation());
	AddMovementInput(Rotation.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(Rotation.GetUnitAxis(EAxis::Y), Axis.X);
	AddMovementInput(FVector::UpVector, Axis.Z);
}

void AConstructPreviewPawn::Look(const FInputActionValue& Value)
{
	if (!IsLocallyControlled() || !Controller || Controller->IsLookInputIgnored())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void AConstructPreviewPawn::UnPossessed()
{
	RemoveInputMappings();
	ConsumeMovementInputVector();
	if (UPawnMovementComponent* Movement = GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	Super::UnPossessed();
}

void AConstructPreviewPawn::LookRate(const FInputActionValue& Value)
{
	if (!IsLocallyControlled() || !Controller || Controller->IsLookInputIgnored())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaSeconds = GetWorld()->GetDeltaSeconds() * CustomTimeDilation;
	AddControllerYawInput(Axis.X * BaseTurnRate * DeltaSeconds);
	AddControllerPitchInput(-Axis.Y * BaseLookUpRate * DeltaSeconds);
}

void AConstructPreviewPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveInputMappings();
	Super::EndPlay(EndPlayReason);
}
