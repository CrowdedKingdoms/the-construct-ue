#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ConstructStudioDockTypes.h"
#include "Containers/Ticker.h"
#include "UObject/Object.h"
#include "ConstructStudioCaptureJob.generated.h"

class APlayerController;
class UGameViewportClient;
class UTextureRenderTarget2D;
struct FConstructStudioCaptureRenderState;

/** Uses the same ownership visibility rules as the player's normal scene view. */
UCLASS(Transient)
class UConstructStudioCaptureComponent final : public USceneCaptureComponent2D
{
    GENERATED_BODY()

public:
    TWeakObjectPtr<AActor> ViewOwner;

    virtual const AActor* GetViewOwner() const override;
};

/** One screenshot's resources outlive its widget only long enough to drain rendering safely. */
UCLASS(Transient)
class UConstructStudioCaptureJob final : public UObject
{
    GENERATED_BODY()

public:
    static UConstructStudioCaptureJob* Begin(UObject* WeakOwner, APlayerController* Controller, const FString& Label, const FOnConstructStudioCaptured& OnDone);
    static UConstructStudioCaptureJob* StartFromView(UObject* WeakOwner, UWorld* World, const FMinimalViewInfo& View, const FMatrix& Projection, FIntPoint Dimensions, AActor* ViewOwner, const FString& Label, const FOnConstructStudioCaptured& OnDone, APlayerController* Controller = nullptr);

    static UConstructStudioCaptureJob* RefuseLater(UObject* WeakOwner, UWorld* World, const FString& Label, const FOnConstructStudioCaptured& OnDone, const FString& Reason);

    void Cancel();
    bool IsFinished() const { return bFinished; }

private:
    static UConstructStudioCaptureJob* CreateJob(UObject* WeakOwner, UWorld* World, const FString& Label, const FOnConstructStudioCaptured& OnDone);
    void StartRendering(UWorld& World, const FMinimalViewInfo& View, const FMatrix& Projection, FIntPoint Dimensions, AActor* ViewOwner, APlayerController* Controller);
    bool Tick(float DeltaSeconds);
    void WorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    void FrameDrawn();
    void ReleaseResources();
    void Finish(bool bDeliver);

    UPROPERTY(Transient)
    TObjectPtr<UConstructStudioCaptureComponent> Capture;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> Target;

    UPROPERTY(Transient)
    FOnConstructStudioCaptured Completion;

    TWeakObjectPtr<UObject> Owner;
    TWeakObjectPtr<UWorld> CaptureWorld;
    TWeakObjectPtr<APlayerController> PendingController;
    TWeakObjectPtr<UGameViewportClient> PendingViewport;
    TWeakObjectPtr<AActor> PendingViewOwner;
    FMinimalViewInfo PendingView;
    FMatrix PendingProjection = FMatrix::Identity;
    FIntPoint PendingDimensions = FIntPoint::ZeroValue;
    TSharedPtr<FConstructStudioCaptureRenderState, ESPMode::ThreadSafe> RenderState;
    FTSTicker::FDelegateHandle TickHandle;
    FDelegateHandle WorldCleanupHandle;
    FDelegateHandle DrawnHandle;
    FConstructStudioCapture Result;
    bool bCancelled = false;
    bool bFinished = false;
    bool bRequiresWorld = false;
    bool bEncodingStarted = false;
    bool bWaitingForFrame = false;
    bool bFrameDrawn = false;
    double RequestedAt = 0.0;
};
