#include "ConstructModScriptInput.h"

#include "ConstructUIInputSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/DateTime.h"
#include "UnrealClient.h"

namespace
{
    bool IsFiniteVector(const FVector2D& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
    }

    int64 WallClockMs()
    {
        const FDateTime Now = FDateTime::UtcNow();
        return Now.ToUnixTimestamp() * 1000 + Now.GetMillisecond();
    }

    int64 HeldDuration(int64 NowMs, int64 PressedMs)
    {
        return NowMs > PressedMs ? NowMs - PressedMs : 0;
    }

    int32 ButtonForKey(FKey Key)
    {
        if (Key == EKeys::LeftMouseButton)
        {
            return 0;
        }
        if (Key == EKeys::MiddleMouseButton)
        {
            return 1;
        }
        if (Key == EKeys::RightMouseButton)
        {
            return 2;
        }
        return INDEX_NONE;
    }
}

void FConstructPointerQueue::Record(int32 Button, bool bDown, int64 AtMs, const FVector2D& NormalizedPosition)
{
    if (Button < 0 || Button >= 3 || Held[Button] == bDown)
    {
        return;
    }
    const bool bPositionValid = IsFiniteVector(NormalizedPosition) && NormalizedPosition.X >= -1.0 && NormalizedPosition.X <= 1.0 && NormalizedPosition.Y >= -1.0 && NormalizedPosition.Y <= 1.0;
    if (bDown && !bPositionValid)
    {
        return;
    }
    const int64 Duration = bDown ? 0 : HeldDuration(AtMs, PressedAtMs[Button]);
    Held[Button] = bDown;
    PressedAtMs[Button] = bDown ? AtMs : 0;
    if (!bPositionValid)
    {
        return;
    }
    if (Pending.Num() == MaxClicks)
    {
        Pending.RemoveAt(0);
    }
    FConstructPointerClick& Click = Pending.AddDefaulted_GetRef();
    Click.bDown = bDown;
    Click.Button = Button;
    Click.AtMs = AtMs;
    Click.HeldMs = Duration;
    Click.X = static_cast<float>(NormalizedPosition.X);
    Click.Y = static_cast<float>(NormalizedPosition.Y);
}

FConstructPointerSnapshot FConstructPointerQueue::Drain(int64 NowMs)
{
    FConstructPointerSnapshot Result;
    Result.Buttons = (Held[0] ? 1 : 0) | (Held[1] ? 4 : 0) | (Held[2] ? 2 : 0);
    Result.LeftHeldMs = Held[0] ? HeldDuration(NowMs, PressedAtMs[0]) : 0;
    Result.MiddleHeldMs = Held[1] ? HeldDuration(NowMs, PressedAtMs[1]) : 0;
    Result.RightHeldMs = Held[2] ? HeldDuration(NowMs, PressedAtMs[2]) : 0;
    Result.Clicks = MoveTemp(Pending);
    Pending.Reset();
    return Result;
}

void FConstructPointerQueue::Reset()
{
    for (int32 Button = 0; Button < 3; ++Button)
    {
        Held[Button] = false;
        PressedAtMs[Button] = 0;
    }
    Pending.Reset();
}

bool FConstructPointerQueue::Normalize(const FVector2D& Position, const FVector2D& ViewportSize, FVector2D& Out, const FVector2D& RegionOrigin, const FVector2D& RegionSize)
{
    Out = FVector2D::ZeroVector;
    if (!IsFiniteVector(Position) || !IsFiniteVector(ViewportSize) || !IsFiniteVector(RegionOrigin) || !IsFiniteVector(RegionSize) || ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0 || RegionOrigin.X < 0.0 || RegionOrigin.Y < 0.0 || RegionSize.X <= 0.0 || RegionSize.Y <= 0.0 || RegionOrigin.X + RegionSize.X > 1.0 || RegionOrigin.Y + RegionSize.Y > 1.0)
    {
        return false;
    }
    const FVector2D OriginPixels = RegionOrigin * ViewportSize;
    const FVector2D SizePixels = RegionSize * ViewportSize;
    const FVector2D LocalPosition = Position - OriginPixels;
    if (LocalPosition.X < 0.0 || LocalPosition.Y < 0.0 || LocalPosition.X > SizePixels.X || LocalPosition.Y > SizePixels.Y)
    {
        return false;
    }
    Out.X = LocalPosition.X / SizePixels.X * 2.0 - 1.0;
    Out.Y = 1.0 - LocalPosition.Y / SizePixels.Y * 2.0;
    return true;
}

UConstructModScriptInput::UConstructModScriptInput()
{
    Priority = 60;
    bBlockInput = false;
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UConstructModScriptInput::Attach(APlayerController* Controller)
{
    Detach();
    ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    UConstructUIInputSubsystem* Gate = Player ? Player->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
    if (!Controller || !Controller->IsLocalController() || !Gate)
    {
        return;
    }
    AttachedController = Controller;
    InputGate = Gate;
    Priority = 60;
    bBlockInput = false;
    BindKey(EKeys::F7, IE_Pressed, this, &ThisClass::ToggleList).bConsumeInput = false;
    for (const FKey Key : {EKeys::LeftMouseButton, EKeys::MiddleMouseButton, EKeys::RightMouseButton})
    {
        BindKey(Key, IE_Pressed, this, &ThisClass::PointerPressed).bConsumeInput = false;
        BindKey(Key, IE_Released, this, &ThisClass::PointerReleased).bConsumeInput = false;
    }
    Gate->OnGameplayInputChanged.AddUniqueDynamic(this, &ThisClass::GameplayInputChanged);
    if (FSlateApplication::IsInitialized())
    {
        ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &ThisClass::ApplicationActivationChanged);
    }
    if (!IsRegistered())
    {
        RegisterComponentWithWorld(Controller->GetWorld());
    }
    Controller->PushInputComponent(this);
    SetComponentTickEnabled(true);
}

void UConstructModScriptInput::Detach()
{
    if (APlayerController* Controller = AttachedController.Get())
    {
        Controller->PopInputComponent(this);
    }
    if (UConstructUIInputSubsystem* Gate = InputGate.Get())
    {
        Gate->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::GameplayInputChanged);
    }
    if (ActivationHandle.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
    }
    ActivationHandle.Reset();
    AttachedController.Reset();
    InputGate.Reset();
    KeyBindings.Reset();
    Queue.Reset();
    SetComponentTickEnabled(false);
}

bool UConstructModScriptInput::HasFocusedLocalViewport() const
{
    const APlayerController* Controller = AttachedController.Get();
    const ULocalPlayer* Player = Controller ? Controller->GetLocalPlayer() : nullptr;
    const UGameViewportClient* Client = Player ? Player->ViewportClient : nullptr;
    return FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive() && Controller && Controller->IsLocalController() && Client && Client->Viewport && Client->Viewport->HasFocus();
}

bool UConstructModScriptInput::CanRecordPointer() const
{
    const UConstructUIInputSubsystem* Gate = InputGate.Get();
    return Gate && Gate->IsGameplayInputAllowed() && HasFocusedLocalViewport();
}

void UConstructModScriptInput::RecordPointer(FKey Key, bool bDown)
{
    if (!CanRecordPointer())
    {
        Queue.Reset();
        return;
    }
    APlayerController* Controller = AttachedController.Get();
    ULocalPlayer* Player = Controller->GetLocalPlayer();
    FVector2D Position;
    FVector2D NormalizedPosition(2.0, 2.0);
    int32 Width = 0;
    int32 Height = 0;
    Controller->GetViewportSize(Width, Height);
    if (Controller->GetMousePosition(Position.X, Position.Y))
    {
        FVector2D Candidate;
        if (FConstructPointerQueue::Normalize(Position, FVector2D(Width, Height), Candidate, Player->Origin, Player->Size))
        {
            NormalizedPosition = Candidate;
        }
    }
    Queue.Record(ButtonForKey(Key), bDown, WallClockMs(), NormalizedPosition);
}

void UConstructModScriptInput::PointerPressed(FKey Key)
{
    RecordPointer(Key, true);
}

void UConstructModScriptInput::PointerReleased(FKey Key)
{
    RecordPointer(Key, false);
}

void UConstructModScriptInput::ToggleList()
{
    const UConstructUIInputSubsystem* Gate = InputGate.Get();
    const FName ListModal(TEXT("ConstructModScriptList"));
    const FName ConsentModal(TEXT("ConstructModScriptConsent"));
    if (Gate && Gate->CanUseNavigationInput(ListModal, ConsentModal) && Gate->CanUseNavigationInput(ListModal))
    {
        OnToggleList.ExecuteIfBound();
    }
}

FConstructPointerSnapshot UConstructModScriptInput::DrainClicks()
{
    if (!CanRecordPointer())
    {
        Queue.Reset();
    }
    return Queue.Drain(WallClockMs());
}

void UConstructModScriptInput::GameplayInputChanged(bool bAllowed)
{
    if (!bAllowed)
    {
        Queue.Reset();
    }
}

void UConstructModScriptInput::ApplicationActivationChanged(bool bActive)
{
    if (!bActive)
    {
        Queue.Reset();
    }
}

void UConstructModScriptInput::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!CanRecordPointer())
    {
        Queue.Reset();
    }
}

void UConstructModScriptInput::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    Detach();
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}
