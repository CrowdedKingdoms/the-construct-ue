#include "ConstructStudioCaptureJob.h"
#include "ConstructCapturePrivacy.h"

#include "Async/Async.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "SceneView.h"
#include "TextureResource.h"
#include "UnrealClient.h"

struct FConstructStudioCaptureRenderState
{
    FIntPoint Dimensions = FIntPoint::ZeroValue;
    TUniquePtr<FRHIGPUTextureReadback> Readback;
    TArray<FColor> Pixels;
    FString Refusal;
    FString PngBase64;
    TAtomic<bool> bCopyStarted{false};
    TAtomic<bool> bPollPending{false};
    TAtomic<bool> bReadbackComplete{false};
    TAtomic<bool> bEncodingComplete{false};
    TAtomic<bool> bCancelled{false};
};

namespace
{
const FName StudioCaptureExcludedTag = ConstructCapturePrivacy::ExcludeTag;
constexpr int32 StudioCaptureMaximumDimension = 1280;

void PollStudioCaptureReadback(const TSharedRef<FConstructStudioCaptureRenderState, ESPMode::ThreadSafe>& State)
{
    if (State->bPollPending.Exchange(true))
    {
        return;
    }

    ENQUEUE_RENDER_COMMAND(ConstructStudioReadCapture)([State](FRHICommandListImmediate& RHICmdList)
    {
        if (State->Readback && State->Readback->IsReady())
        {
            if (State->bCancelled.Load())
            {
                State->Readback.Reset();
                State->bReadbackComplete.Store(true);
                State->bPollPending.Store(false);
                return;
            }
            int32 RowPitch = 0;
            int32 BufferHeight = 0;
            const FColor* Source = static_cast<const FColor*>(State->Readback->Lock(RowPitch, &BufferHeight));
            if (Source)
            {
                if (!State->bCancelled.Load() && RowPitch >= State->Dimensions.X && BufferHeight >= State->Dimensions.Y)
                {
                    State->Pixels.SetNumUninitialized(State->Dimensions.X * State->Dimensions.Y);
                    for (int32 Row = 0; Row < State->Dimensions.Y; ++Row)
                    {
                        FMemory::Memcpy(State->Pixels.GetData() + Row * State->Dimensions.X, Source + Row * RowPitch, State->Dimensions.X * sizeof(FColor));
                    }
                }
                else if (!State->bCancelled.Load())
                {
                    State->Refusal = TEXT("The rendered screenshot has an invalid size.");
                }
                State->Readback->Unlock();
            }
            else if (!State->bCancelled.Load())
            {
                State->Refusal = TEXT("The rendered screenshot could not be read.");
            }
            State->Readback.Reset();
            State->bReadbackComplete.Store(true);
        }
        State->bPollPending.Store(false);
    });
}
}

const AActor* UConstructStudioCaptureComponent::GetViewOwner() const
{
    return ViewOwner.Get();
}

UConstructStudioCaptureJob* UConstructStudioCaptureJob::CreateJob(UObject* WeakOwner, UWorld* World, const FString& Label, const FOnConstructStudioCaptured& OnDone)
{
    check(IsInGameThread());
    UConstructStudioCaptureJob* Job = NewObject<UConstructStudioCaptureJob>(GetTransientPackage());
    Job->Owner = WeakOwner;
    Job->CaptureWorld = World;
    Job->Completion = OnDone;
    Job->Result.Caption = Label;
    Job->AddToRoot();
    Job->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(Job, &UConstructStudioCaptureJob::Tick));
    Job->WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(Job, &UConstructStudioCaptureJob::WorldCleanup);
    return Job;
}

UConstructStudioCaptureJob* UConstructStudioCaptureJob::RefuseLater(UObject* WeakOwner, UWorld* World, const FString& Label, const FOnConstructStudioCaptured& OnDone, const FString& Reason)
{
    UConstructStudioCaptureJob* Job = CreateJob(WeakOwner, World, Label, OnDone);
    Job->Result.Refusal = Reason;
    return Job;
}

UConstructStudioCaptureJob* UConstructStudioCaptureJob::Begin(UObject* WeakOwner, APlayerController* Controller, const FString& Label, const FOnConstructStudioCaptured& OnDone)
{
    check(IsInGameThread());
    UWorld* World = IsValid(Controller) ? Controller->GetWorld() : nullptr;
    ULocalPlayer* Player = IsValid(Controller) ? Controller->GetLocalPlayer() : nullptr;
    UGameViewportClient* ViewportClient = Player ? Player->ViewportClient : nullptr;
    FViewport* Viewport = ViewportClient ? ViewportClient->Viewport : nullptr;
    if (!Viewport || !World || ViewportClient->bDisableWorldRendering || !FApp::CanEverRender() || GUsingNullRHI)
    {
        return RefuseLater(WeakOwner, World, Label, OnDone, TEXT("No game frame is being drawn."));
    }

    FSceneViewProjectionData Projection;
    if (!Player->GetProjectionData(Viewport, Projection) || !Projection.IsValidViewRectangle())
    {
        return RefuseLater(WeakOwner, World, Label, OnDone, TEXT("No game frame is being drawn."));
    }

    FMinimalViewInfo View;
    if (!Controller->PlayerCameraManager)
    {
        return RefuseLater(WeakOwner, World, Label, OnDone, TEXT("The game camera is unavailable."));
    }
    View = Controller->PlayerCameraManager->GetCameraCacheView();
    if (View.Location.ContainsNaN() || View.Rotation.ContainsNaN() || Projection.ProjectionMatrix.ContainsNaN())
    {
        return RefuseLater(WeakOwner, World, Label, OnDone, TEXT("The game camera is unavailable."));
    }
    UConstructStudioCaptureJob* Job = CreateJob(WeakOwner, World, Label, OnDone);
    Job->bRequiresWorld = true;
    Job->PendingController = Controller;
    Job->PendingViewport = ViewportClient;
    Job->PendingViewOwner = Controller->GetViewTarget();
    Job->PendingView = View;
    Job->PendingProjection = Projection.ProjectionMatrix;
    Job->PendingDimensions = Projection.GetConstrainedViewRect().Size();
    Job->bWaitingForFrame = true;
    Job->RequestedAt = FPlatformTime::Seconds();
    Job->DrawnHandle = ViewportClient->OnDrawn().AddUObject(Job, &UConstructStudioCaptureJob::FrameDrawn);
    return Job;
}

UConstructStudioCaptureJob* UConstructStudioCaptureJob::StartFromView(UObject* WeakOwner, UWorld* World, const FMinimalViewInfo& View, const FMatrix& Projection, FIntPoint Dimensions, AActor* ViewOwner, const FString& Label, const FOnConstructStudioCaptured& OnDone, APlayerController* Controller)
{
    check(IsInGameThread());
    UConstructStudioCaptureJob* Job = CreateJob(WeakOwner, World, Label, OnDone);
    if (!FApp::CanEverRender() || GUsingNullRHI || !IsValid(World) || !World->Scene || Dimensions.X <= 0 || Dimensions.Y <= 0 || View.Location.ContainsNaN() || View.Rotation.ContainsNaN() || Projection.ContainsNaN())
    {
        Job->Result.Refusal = TEXT("No game frame is being drawn.");
        return Job;
    }

    const double Scale = FMath::Min(1.0, static_cast<double>(StudioCaptureMaximumDimension) / FMath::Max(Dimensions.X, Dimensions.Y));
    Dimensions.X = FMath::Max(1, FMath::RoundToInt(Dimensions.X * Scale));
    Dimensions.Y = FMath::Max(1, FMath::RoundToInt(Dimensions.Y * Scale));
    Job->StartRendering(*World, View, Projection, Dimensions, ViewOwner, Controller);
    return Job;
}

void UConstructStudioCaptureJob::StartRendering(UWorld& World, const FMinimalViewInfo& View, const FMatrix& Projection, FIntPoint Dimensions, AActor* ViewOwner, APlayerController* Controller)
{
    bRequiresWorld = true;
    Result.Width = Dimensions.X;
    Result.Height = Dimensions.Y;
    Target = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
    Target->ClearColor = FLinearColor::Black;
    Target->InitCustomFormat(Dimensions.X, Dimensions.Y, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);

    Capture = NewObject<UConstructStudioCaptureComponent>(this, NAME_None, RF_Transient);
    Capture->ViewOwner = ViewOwner;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->TextureTarget = Target;
    Capture->SetCameraView(View);
    Capture->PostProcessSettings = View.PostProcessSettings;
    Capture->PostProcessBlendWeight = View.PostProcessBlendWeight;
    Capture->bUseCustomProjectionMatrix = true;
    Capture->CustomProjectionMatrix = Projection;
    Capture->ShowFlags.SetWidgetComponents(false);
    Capture->ShowFlags.SetTextRender(false);
    Capture->bCameraCutThisFrame = true;

    TSet<FPrimitiveComponentId> PlayerHidden;
    if (IsValid(Controller))
    {
        Controller->BuildHiddenComponentList(View.Location, PlayerHidden);
    }

    for (TActorIterator<AActor> Actor(&World); Actor; ++Actor)
    {
        const bool bHideActor = Actor->ActorHasTag(StudioCaptureExcludedTag);
        if (bHideActor)
        {
            Capture->HideActorComponents(*Actor, true);
        }
        TInlineComponentArray<UPrimitiveComponent*> Primitives;
        Actor->GetComponents(Primitives);
        for (UPrimitiveComponent* Primitive : Primitives)
        {
            if (IsValid(Primitive) && (bHideActor || Primitive->IsA<UWidgetComponent>() || Primitive->IsA<UTextRenderComponent>() || Primitive->ComponentHasTag(StudioCaptureExcludedTag) || PlayerHidden.Contains(Primitive->GetPrimitiveSceneId())))
            {
                Capture->HideComponent(Primitive);
            }
        }
    }

    Capture->RegisterComponentWithWorld(&World);
    Capture->CaptureScene();

    RenderState = MakeShared<FConstructStudioCaptureRenderState, ESPMode::ThreadSafe>();
    RenderState->Dimensions = Dimensions;
    const TSharedRef<FConstructStudioCaptureRenderState, ESPMode::ThreadSafe> State = RenderState.ToSharedRef();
    FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
    ENQUEUE_RENDER_COMMAND(ConstructStudioCopyCapture)([State, Resource](FRHICommandListImmediate& RHICmdList)
    {
        const FTextureRHIRef Texture = Resource->GetRenderTargetTexture();
        if (Texture.IsValid())
        {
            State->Readback = MakeUnique<FRHIGPUTextureReadback>(TEXT("ConstructStudioScreenshot"));
            State->Readback->EnqueueCopy(RHICmdList, Texture.GetReference(), FResolveRect(0, 0, State->Dimensions.X, State->Dimensions.Y));
        }
        else
        {
            State->Refusal = TEXT("The game frame has no render target.");
            State->bReadbackComplete.Store(true);
        }
        State->bCopyStarted.Store(true);
    });
}

void UConstructStudioCaptureJob::Cancel()
{
    check(IsInGameThread());
    bCancelled = true;
    Completion.Unbind();
    if (RenderState)
    {
        RenderState->bCancelled.Store(true);
    }
}

bool UConstructStudioCaptureJob::Tick(float DeltaSeconds)
{
    if (bFinished)
    {
        return false;
    }
    if (!Owner.IsValid() || (bRequiresWorld && !CaptureWorld.IsValid()))
    {
        Cancel();
    }
    if (bWaitingForFrame && !bCancelled)
    {
        UWorld* World = CaptureWorld.Get();
        UGameViewportClient* Viewport = PendingViewport.Get();
        if (!World || !World->Scene || !Viewport || Viewport->bDisableWorldRendering || (!bFrameDrawn && FPlatformTime::Seconds() - RequestedAt >= 1.0))
        {
            Result.Refusal = TEXT("No game frame is being drawn.");
            Finish(true);
            return false;
        }
        if (!bFrameDrawn)
        {
            return true;
        }
        bWaitingForFrame = false;
        Viewport->OnDrawn().Remove(DrawnHandle);
        DrawnHandle.Reset();
        const double Scale = FMath::Min(1.0, static_cast<double>(StudioCaptureMaximumDimension) / FMath::Max(PendingDimensions.X, PendingDimensions.Y));
        PendingDimensions.X = FMath::Max(1, FMath::RoundToInt(PendingDimensions.X * Scale));
        PendingDimensions.Y = FMath::Max(1, FMath::RoundToInt(PendingDimensions.Y * Scale));
        StartRendering(*World, PendingView, PendingProjection, PendingDimensions, PendingViewOwner.Get(), PendingController.Get());
    }
    if (!RenderState)
    {
        Finish(!bCancelled);
        return false;
    }

    const TSharedRef<FConstructStudioCaptureRenderState, ESPMode::ThreadSafe> State = RenderState.ToSharedRef();
    if (!State->bCopyStarted.Load())
    {
        return true;
    }
    if (!State->bReadbackComplete.Load())
    {
        PollStudioCaptureReadback(State);
        return true;
    }
    if (bCancelled)
    {
        Finish(false);
        return false;
    }
    if (!bEncodingStarted && !State->Refusal.IsEmpty())
    {
        Result.Refusal = State->Refusal;
        Finish(true);
        return false;
    }
    if (!bEncodingStarted)
    {
        bEncodingStarted = true;
        if (!FModuleManager::Get().LoadModule(TEXT("ImageWrapper")))
        {
            Result.Refusal = TEXT("Screenshot PNG encoding is unavailable.");
            Finish(true);
            return false;
        }
        Async(EAsyncExecution::ThreadPool, [State]()
        {
            if (!State->bCancelled.Load())
            {
                for (FColor& Pixel : State->Pixels)
                {
                    Pixel.A = 255;
                }
                TArray64<uint8> Png;
                const FImageView Image(State->Pixels.GetData(), State->Dimensions.X, State->Dimensions.Y);
                if (FImageUtils::CompressImage(Png, TEXT("png"), Image) && Png.Num() <= MAX_int32)
                {
                    State->PngBase64 = FBase64::Encode(Png.GetData(), static_cast<uint32>(Png.Num()));
                }
                else
                {
                    State->Refusal = TEXT("The screenshot could not be encoded as PNG.");
                }
            }
            State->Pixels.Empty();
            State->bEncodingComplete.Store(true);
        });
        return true;
    }
    if (!State->bEncodingComplete.Load())
    {
        return true;
    }

    Result.PngBase64 = MoveTemp(State->PngBase64);
    Result.Refusal = MoveTemp(State->Refusal);
    Result.bCaptured = !Result.PngBase64.IsEmpty() && Result.Refusal.IsEmpty();
    Finish(true);
    return false;
}

void UConstructStudioCaptureJob::WorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
    if (World == CaptureWorld.Get())
    {
        Cancel();
        if (Capture && Capture->IsRegistered())
        {
            Capture->UnregisterComponent();
        }
    }
}

void UConstructStudioCaptureJob::FrameDrawn()
{
    bFrameDrawn = true;
}

void UConstructStudioCaptureJob::ReleaseResources()
{
    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
    WorldCleanupHandle.Reset();
    if (UGameViewportClient* Viewport = PendingViewport.Get())
    {
        Viewport->OnDrawn().Remove(DrawnHandle);
    }
    DrawnHandle.Reset();
    if (Capture)
    {
        Capture->TextureTarget = nullptr;
        Capture->DestroyComponent();
        Capture = nullptr;
    }
    Target = nullptr;
    RenderState.Reset();
}

void UConstructStudioCaptureJob::Finish(bool bDeliver)
{
    check(IsInGameThread());
    if (bFinished)
    {
        return;
    }
    bFinished = true;
    const FOnConstructStudioCaptured Answer = Completion;
    const FConstructStudioCapture CaptureResult = MoveTemp(Result);
    Completion.Unbind();
    ReleaseResources();
    RemoveFromRoot();
    if (bDeliver && Owner.IsValid())
    {
        Answer.ExecuteIfBound(CaptureResult);
    }
}
