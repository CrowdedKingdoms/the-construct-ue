#include "ConstructPaintCanvas.h"

#include "Components/SceneComponent.h"
#include "ConstructChunkDrawing.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Subsystem/CrowdyVoxelLibrary.h"
#include "Subsystem/CrowdyVoxelWorld.h"
#include "TimerManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructPaintCanvas"

DEFINE_LOG_CATEGORY_STATIC(LogConstructPaint, Log, All);

AConstructPaintCanvas::AConstructPaintCanvas()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PaintCanvas")));
}

void AConstructPaintCanvas::BeginPlay()
{
    Super::BeginPlay();
    UCrowdyVoxelWorld* Voxels = GetWorld()->GetSubsystem<UCrowdyVoxelWorld>();
    if (Voxels)
    {
        VoxelWorld = Voxels;
        Voxels->OnChunkLoaded.AddUniqueDynamic(this, &ThisClass::ChunkLoaded);
        Voxels->OnEditDone.AddUniqueDynamic(this, &ThisClass::EditDone);
        Voxels->Follow(this, 1);
        const FInt64Vector3 Chunk = GetCanvasChunk();
        ChunkLoaded(Chunk.X, Chunk.Y, Chunk.Z);
    }
}

void AConstructPaintCanvas::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UCrowdyVoxelWorld* Voxels = VoxelWorld.Get())
    {
        Voxels->OnChunkLoaded.RemoveDynamic(this, &ThisClass::ChunkLoaded);
        Voxels->OnEditDone.RemoveDynamic(this, &ThisClass::EditDone);
        Voxels->StopFollowing(this);
    }
    VoxelWorld.Reset();
    PendingEdits.Reset();
    GetWorldTimerManager().ClearTimer(FeedbackTimer);
    HideFeedback();
    Super::EndPlay(Reason);
}

FInt64Vector3 AConstructPaintCanvas::GetCanvasChunk() const
{
    FInt64Vector3 Chunk;
    FIntVector Cell;
    UCrowdyVoxelLibrary::WorldToVoxel(this, GetActorLocation(), Chunk.X, Chunk.Y, Chunk.Z, Cell);
    return Chunk;
}

bool AConstructPaintCanvas::MakeWrite(FIntVector Cell, int32 Type, FCrowdyVoxelWrite& Write) const
{
    if (!ConstructChunkDrawing::IsInsideChunk(Cell) || Cell.Z != 0 || Type < 0 || Type > ConstructChunkDrawing::BlockMaterialCount)
    {
        return false;
    }

    const FInt64Vector3 Chunk = GetCanvasChunk();
    Write.ChunkX = Chunk.X;
    Write.ChunkY = Chunk.Y;
    Write.ChunkZ = Chunk.Z;
    Write.Voxel.Position = Cell;
    Write.Voxel.Type = Type;
    return true;
}

void AConstructPaintCanvas::PaintCell(FIntVector Cell, int32 Type)
{
    UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
    FCrowdyVoxelWrite Write;
    if (Voxels && MakeWrite(Cell, Type, Write))
    {
        PendingEdits.Add(Voxels->SetVoxel(Write));
    }
}

void AConstructPaintCanvas::ChunkLoaded(int64 ChunkX, int64 ChunkY, int64 ChunkZ)
{
    const UCrowdyVoxelWorld* Voxels = VoxelWorld.Get();
    if (!Voxels || bWarnedAboutStorage || GetCanvasChunk() != FInt64Vector3(ChunkX, ChunkY, ChunkZ))
    {
        return;
    }

    FCrowdyVoxelChunk Chunk;
    if (Voxels->FindChunk(ChunkX, ChunkY, ChunkZ, Chunk) && !Chunk.bStored)
    {
        bWarnedAboutStorage = true;
        UE_LOG(LogConstructPaint, Warning, TEXT("Paint strokes will not be kept because the canvas chunk is not stored."));
    }
}

void AConstructPaintCanvas::EditDone(int64 EditId, const FCrowdyVoxelWrite& Write, ECrowdyVoxelResult Result, const FString& Message)
{
    if (PendingEdits.Remove(EditId) == 0)
    {
        return;
    }

    if (Result != ECrowdyVoxelResult::Ok)
    {
        ShowFeedback(Message);
    }
    OnPaintDone.Broadcast(Write.Voxel.Position, Write.Voxel.Type, Result, Message);
}

void AConstructPaintCanvas::ShowFeedback(const FString& Message)
{
    HideFeedback();
    UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
    if (!Viewport)
    {
        return;
    }

    const FText Reason = Message.IsEmpty() ? LOCTEXT("Failed", "Could not paint this cell.") : FText::FromString(Message);
    FeedbackWidget = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(20.0f, 20.0f, 20.0f, 90.0f)
        [
            SNew(SBorder).Padding(FMargin(14.0f, 8.0f)).BorderBackgroundColor(FLinearColor(0.10f, 0.015f, 0.01f))
            [
                SNew(STextBlock).Text(Reason).ColorAndOpacity(FLinearColor::White)
            ]
        ];
    FeedbackViewport = Viewport;
    Viewport->AddViewportWidgetContent(FeedbackWidget.ToSharedRef(), 25);
    GetWorldTimerManager().SetTimer(FeedbackTimer, this, &ThisClass::HideFeedback, 3.0f);
}

void AConstructPaintCanvas::HideFeedback()
{
    if (UGameViewportClient* Viewport = FeedbackViewport.Get(); Viewport && FeedbackWidget.IsValid())
    {
        Viewport->RemoveViewportWidgetContent(FeedbackWidget.ToSharedRef());
    }
    FeedbackWidget.Reset();
    FeedbackViewport.Reset();
}

#undef LOCTEXT_NAMESPACE
