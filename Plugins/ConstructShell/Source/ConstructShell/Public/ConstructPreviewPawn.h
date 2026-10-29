#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "ConstructPreviewPawn.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;

/** Local free camera using Enhanced Input; no Engine default movement bindings or SDK identity. */
UCLASS(Blueprintable)
class CONSTRUCTSHELL_API AConstructPreviewPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	AConstructPreviewPawn();
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void AddInputMappings();
	void RemoveInputMappings();
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void LookRate(const FInputActionValue& Value);
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookRateAction;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSubsystem;
};
