#include "ConstructPaintControls.h"

#include "CollisionQueryParams.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "ConstructChunkDrawing.h"
#include "ConstructPaintCanvas.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Styling/CoreStyle.h"

#include "Subsystem/CrowdyVoxelLibrary.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConstructPaintControls"

namespace
{
    constexpr int32 BlockCount = ConstructChunkDrawing::BlockMaterialCount;
    constexpr int32 Columns = 7;

    FLinearColor SwatchColour(int32 Type)
    {
        const FString Path = ConstructChunkDrawing::GetBlockMaterialPath(Type);
        const UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
        FLinearColor Colour = FLinearColor::White;
        if (Material)
        {
            Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BlockColor")), Colour);
        }
        return Colour;
    }
}

AConstructPaintControls::AConstructPaintControls()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PaintControls")));
}

void AConstructPaintControls::BeginPlay()
{
    Super::BeginPlay();
    UGameInstance* Instance = GetGameInstance();
    ULocalPlayer* Player = Instance ? Instance->GetFirstGamePlayer() : nullptr;
    if (!IsValid(Canvas) || !IsValid(Floor) || !Player)
    {
        return;
    }

    LocalPlayer = Player;
    ControllerChangedHandle = Player->OnPlayerControllerChanged().AddUObject(this, &ThisClass::AttachToController);
    AttachToController(Player->GetPlayerController(GetWorld()));
}

void AConstructPaintControls::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (ULocalPlayer* Player = LocalPlayer.Get())
    {
        Player->OnPlayerControllerChanged().Remove(ControllerChangedHandle);
    }
    ReleasePresentation();
    LocalPlayer.Reset();
    Super::EndPlay(EndPlayReason);
}

void AConstructPaintControls::AttachToController(APlayerController* Controller)
{
    ReleasePresentation();
    ULocalPlayer* Player = LocalPlayer.Get();
    if (!Player || !IsValid(Controller) || Controller->GetWorld() != GetWorld() || Controller->GetLocalPlayer() != Player
        || !Controller->IsLocalController())
    {
        return;
    }

    UGameViewportClient* GameViewport = GetWorld()->GetGameViewport();
    if (!GameViewport)
    {
        return;
    }

    InputController = Controller;
    Viewport = GameViewport;
    PaintInput = NewObject<UInputComponent>(Controller);
    PaintInput->Priority = 40;
    PaintInput->RegisterComponent();
    PaintInput->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ThisClass::PaintUnderCursor).bConsumeInput = false;
    Controller->PushInputComponent(PaintInput);

    Palette = BuildPalette();
    GameViewport->AddViewportWidgetForPlayer(Player, Palette.ToSharedRef(), 1);
}

void AConstructPaintControls::ReleasePresentation()
{
    if (APlayerController* Controller = InputController.Get(); Controller && PaintInput)
    {
        Controller->PopInputComponent(PaintInput);
    }
    if (PaintInput)
    {
        PaintInput->DestroyComponent();
    }
    if (UGameViewportClient* GameViewport = Viewport.Get(); GameViewport && Palette.IsValid())
    {
        if (ULocalPlayer* Player = LocalPlayer.Get())
        {
            GameViewport->RemoveViewportWidgetForPlayer(Player, Palette.ToSharedRef());
        }
    }
    Palette.Reset();
    PaintInput = nullptr;
    Viewport.Reset();
    InputController.Reset();
}

TSharedRef<SWidget> AConstructPaintControls::BuildPalette()
{
    const TWeakObjectPtr<AConstructPaintControls> WeakThis(this);
    const TSharedRef<SVerticalBox> Contents = SNew(SVerticalBox);
    Contents->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
    [
        SNew(STextBlock).Text(LOCTEXT("CursorHint", "F4: Toggle cursor"))
    ];
    Contents->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
    [
        SNew(STextBlock).Text_Lambda([WeakThis]()
        {
            if (const AConstructPaintControls* Controls = WeakThis.Get(); Controls && Controls->SelectedType > 0)
            {
                return FText::Format(LOCTEXT("SelectedBlock", "Paint block {0}"), FText::AsNumber(Controls->SelectedType));
            }
            return LOCTEXT("SelectedErase", "Erase");
        })
    ];

    for (int32 Start = 1; Start <= BlockCount + 1; Start += Columns)
    {
        const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        Contents->AddSlot().AutoHeight()[Row];
        for (int32 Entry = Start; Entry < Start + Columns && Entry <= BlockCount + 1; ++Entry)
        {
            const int32 Type = Entry <= BlockCount ? Entry : 0;
            const FText Label = Type ? FText::AsNumber(Type) : LOCTEXT("EraseButton", "Erase");
            const FText Tooltip = Type ? FText::Format(LOCTEXT("ChooseBlock", "Choose block {0}"), Label) : LOCTEXT("ChooseErase", "Erase a block");
            const FLinearColor Colour = Type ? SwatchColour(Type) : FLinearColor(0.18f, 0.18f, 0.18f);
            const FLinearColor Ink = Colour.GetLuminance() > 0.25f ? FLinearColor::Black : FLinearColor::White;
            Row->AddSlot().AutoWidth().Padding(2.0f)
            [
                SNew(SBorder)
                .Padding(2.0f)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([WeakThis, Type]()
                {
                    const AConstructPaintControls* Controls = WeakThis.Get();
                    return FSlateColor(Controls && Controls->SelectedType == Type ? FLinearColor::White : FLinearColor::Transparent);
                })
                [
                    SNew(SButton)
                    .IsFocusable(false)
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    .ContentPadding(0.0f)
                    .ButtonColorAndOpacity(Colour)
                    .ToolTipText(Tooltip)
                    .OnClicked_Lambda([WeakThis, Type]()
                    {
                        if (AConstructPaintControls* Controls = WeakThis.Get(); Controls && Controls->CanPaint())
                        {
                            Controls->SelectedType = Type;
                        }
                        return FReply::Handled();
                    })
                    [
                        SNew(SBox).WidthOverride(Type ? 30.0f : 46.0f).HeightOverride(28.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock).Text(Label).ColorAndOpacity(Ink)
                        ]
                    ]
                ]
            ];
        }
    }

    return SNew(SOverlay)
        .Visibility_Lambda([WeakThis]()
        {
            const AConstructPaintControls* Controls = WeakThis.Get();
            return Controls && Controls->CanPaint() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        })
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(20.0f, 0.0f, 0.0f, 62.0f)
        [
            SNew(SBorder)
            .Padding(8.0f)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.04f, 0.04f, 0.04f, 0.9f))
            .OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&)
            {
                return FReply::Handled();
            })
            [
                Contents
            ]
        ];
}

bool AConstructPaintControls::CanPaint() const
{
    const APlayerController* Controller = InputController.Get();
    const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    const ULocalPlayer* Player = LocalPlayer.Get();
    const UConstructUIInputSubsystem* Gate = Player ? Player->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
    return IsValid(Canvas) && IsValid(Floor) && Pawn && Pawn->IsLocallyControlled() && Pawn != Controller->GetSpectatorPawn()
        && Gate && Gate->IsGameplayInputAllowed();
}

void AConstructPaintControls::PaintUnderCursor()
{
    APlayerController* Controller = InputController.Get();
    float X = 0.0f;
    float Y = 0.0f;
    if (!CanPaint() || !Controller || !Controller->bShowMouseCursor || !Controller->GetMousePosition(X, Y))
    {
        return;
    }

    FCollisionQueryParams Params(SCENE_QUERY_STAT(ConstructPaintClick));
    Params.AddIgnoredActor(Controller->GetPawn());
    FHitResult Hit;
    if (!Controller->GetHitResultAtScreenPosition(FVector2D(X, Y), ECC_Visibility, Params, Hit)
        || Hit.GetActor() != Floor.Get() || Hit.ImpactNormal.Z < 0.5)
    {
        return;
    }

    const FInt64Vector3 Chunk = Canvas->GetCanvasChunk();
    const FVector Origin = UCrowdyVoxelLibrary::GetChunkOrigin(this, Chunk.X, Chunk.Y, Chunk.Z);
    FVector Sample = Hit.ImpactPoint;
    if (!FMath::IsNearlyEqual(Sample.Z, Origin.Z, 1.0))
    {
        return;
    }
    // Sample the interior of the floor cell instead of its floating-point boundary.
    Sample.Z = Origin.Z + UCrowdyVoxelLibrary::GetVoxelSize(this) * 0.5;
    FInt64Vector3 HitChunk;
    FIntVector Cell;
    UCrowdyVoxelLibrary::WorldToVoxel(this, Sample, HitChunk.X, HitChunk.Y, HitChunk.Z, Cell);
    if (HitChunk == Chunk)
    {
        Canvas->PaintCell(Cell, SelectedType);
    }
}

#undef LOCTEXT_NAMESPACE