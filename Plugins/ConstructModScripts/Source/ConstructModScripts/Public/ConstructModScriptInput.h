#pragma once

#include "CoreMinimal.h"
#include "Components/InputComponent.h"
#include "ConstructModScriptInput.generated.h"

class APlayerController;
class UConstructUIInputSubsystem;

/** Construct's local input data. Conversion into the SDK's host response belongs to the host adapter. */
struct CONSTRUCTMODSCRIPTS_API FConstructPointerClick
{
    bool bDown = false;
    int32 Button = 0;
    int64 AtMs = 0;
    int64 HeldMs = 0;
    float X = 0.0f;
    float Y = 0.0f;
};

struct CONSTRUCTMODSCRIPTS_API FConstructPointerSnapshot
{
    int32 Buttons = 0;
    int64 LeftHeldMs = 0;
    int64 MiddleHeldMs = 0;
    int64 RightHeldMs = 0;
    TArray<FConstructPointerClick> Clicks;
};

/** Game-thread queue. Button numbers are left 0, middle 1, right 2; held bits are left 1, right 2, middle 4. */
class CONSTRUCTMODSCRIPTS_API FConstructPointerQueue
{
public:
    static constexpr int32 MaxClicks = 64;

    /** Coordinates must be finite and within [-1, 1]. An invalid release still clears a held button. */
    void Record(int32 Button, bool bDown, int64 AtMs, const FVector2D& NormalizedPosition);
    FConstructPointerSnapshot Drain(int64 NowMs);
    void Reset();

    /** Region origin and size are fractions of the full viewport, as on ULocalPlayer. Y points up. */
    static bool Normalize(const FVector2D& Position, const FVector2D& ViewportSize, FVector2D& Out, const FVector2D& RegionOrigin = FVector2D::ZeroVector, const FVector2D& RegionSize = FVector2D(1.0, 1.0));

private:
    bool Held[3] = {false, false, false};
    int64 PressedAtMs[3] = {0, 0, 0};
    TArray<FConstructPointerClick> Pending;
};

/** Observes only normal player-controller input. Slate widgets keep their ordinary opportunity to consume clicks. */
UCLASS()
class CONSTRUCTMODSCRIPTS_API UConstructModScriptInput : public UInputComponent
{
    GENERATED_BODY()

public:
    UConstructModScriptInput();
    void Attach(APlayerController* Controller);
    void Detach();
    FConstructPointerSnapshot DrainClicks();
    FSimpleDelegate OnToggleList;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
    bool HasFocusedLocalViewport() const;
    bool CanRecordPointer() const;
    void PointerPressed(FKey Key);
    void PointerReleased(FKey Key);
    void RecordPointer(FKey Key, bool bDown);
    void ToggleList();
    void ApplicationActivationChanged(bool bActive);

    UFUNCTION()
    void GameplayInputChanged(bool bAllowed);

    TWeakObjectPtr<APlayerController> AttachedController;
    TWeakObjectPtr<UConstructUIInputSubsystem> InputGate;
    FDelegateHandle ActivationHandle;
    FConstructPointerQueue Queue;
};
