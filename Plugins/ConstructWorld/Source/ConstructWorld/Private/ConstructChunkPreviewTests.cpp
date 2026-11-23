#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ConstructChunkPreview.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace
{
    UWorld* CreateChunkTestWorld(EWorldType::Type Type)
    {
        UWorld::InitializationValues Options;
        Options.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
        return UWorld::CreateWorld(Type, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Options);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkPreviewTest, "Construct.Chunks.Preview", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructChunkPreviewTest::RunTest(const FString&)
{
    UWorld* World = CreateChunkTestWorld(EWorldType::EditorPreview);
    if (!TestNotNull(TEXT("A local preview world was created"), World))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };

    const FVector Origin(1200.0, -300.0, 900.0);
    AConstructChunkPreview* Preview = World->SpawnActor<AConstructChunkPreview>(Origin, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("The chunk preview actor was spawned"), Preview))
    {
        return false;
    }

    TInlineComponentArray<UInstancedStaticMeshComponent*> Batches(Preview);
    if (!TestEqual(TEXT("The asymmetric fixture creates one batch"), Batches.Num(), 1))
    {
        return false;
    }
    TestEqual(TEXT("The fixture creates one instance"), Batches[0]->GetInstanceCount(), 1);
    TestEqual(TEXT("Preview cubes have no collision"), Batches[0]->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

    FTransform Instance;
    if (TestTrue(TEXT("The fixture's instance exists"), Batches[0]->GetInstanceTransform(0, Instance, true)))
    {
        TestEqual(TEXT("The centre uses the local XYZ cell and chunk origin"), Instance.GetLocation(), Origin + FVector(150.0, 250.0, 350.0));
        TestEqual(TEXT("The default cell uses the cube at its authored size"), Instance.GetScale3D(), FVector::OneVector);
    }

    UMaterialInstanceDynamic* SecondMaterial = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Preview);
    FConstructPreviewBlockStyle& SecondStyle = Preview->Palette.AddDefaulted_GetRef();
    SecondStyle.BlockId = 2;
    SecondStyle.Material = SecondMaterial;
    const auto AddCell = [Preview](FIntVector Cell, uint8 BlockId)
    {
        FConstructPreviewCell& Entry = Preview->Cells.AddDefaulted_GetRef();
        Entry.Cell = Cell;
        Entry.BlockId = BlockId;
    };
    AddCell(FIntVector(15, 15, 15), 2);
    AddCell(FIntVector(1, 2, 3), 2);
    AddCell(FIntVector(16, 0, 0), 2);
    AddCell(FIntVector(-1, 0, 0), 2);
    AddCell(FIntVector(0, 0, 0), 99);
    AddCell(FIntVector(0, 1, 0), 0);
    Preview->CellSize = 50.0f;
    Preview->RerunConstructionScripts();

    Batches.Reset();
    Preview->GetComponents(Batches);
    TestEqual(TEXT("Two block styles have exactly two batches after reconstruction"), Batches.Num(), 2);
    int32 InstanceCount = 0;
    bool bFoundSecondStyle = false;
    for (UInstancedStaticMeshComponent* Batch : Batches)
    {
        InstanceCount += Batch->GetInstanceCount();
        if (Batch->GetMaterial(0) == SecondMaterial)
        {
            bFoundSecondStyle = true;
            TestEqual(TEXT("Only the valid second-style cell was instanced"), Batch->GetInstanceCount(), 1);
            if (TestTrue(TEXT("The last cell in the chunk was accepted"), Batch->GetInstanceTransform(0, Instance, false)))
            {
                TestEqual(TEXT("The last cell uses the new cell size"), Instance.GetLocation(), FVector(775.0));
                TestEqual(TEXT("The cube scales with the cell size"), Instance.GetScale3D(), FVector(0.5));
            }
        }
    }
    TestTrue(TEXT("The second block style applies its palette material"), bFoundSecondStyle);
    TestEqual(TEXT("Duplicates, empty cells, unknown blocks and out-of-bounds cells add no instances"), InstanceCount, 2);

    Preview->Cells.Reset();
    Preview->RerunConstructionScripts();
    Batches.Reset();
    Preview->GetComponents(Batches);
    TestEqual(TEXT("An empty chunk removes the old geometry"), Batches.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkPreviewBeginPlayTest, "Construct.Chunks.BeginPlay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructChunkPreviewBeginPlayTest::RunTest(const FString&)
{
    UWorld* World = CreateChunkTestWorld(EWorldType::Game);
    if (!TestNotNull(TEXT("An isolated game world was created"), World))
    {
        return false;
    }
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };
    World->InitializeActorsForPlay(FURL());

    AConstructChunkPreview* Preview = World->SpawnActor<AConstructChunkPreview>();
    if (!TestNotNull(TEXT("The game world has a chunk preview actor"), Preview))
    {
        return false;
    }
    UInstancedStaticMeshComponent* ConstructionBatch = Preview->FindComponentByClass<UInstancedStaticMeshComponent>();
    if (!TestNotNull(TEXT("The actor initially has its construction batch"), ConstructionBatch))
    {
        return false;
    }
    // A loaded level does not carry the transient geometry from the editor.
    ConstructionBatch->DestroyComponent();
    Preview->DispatchBeginPlay();

    TInlineComponentArray<UInstancedStaticMeshComponent*> Batches(Preview);
    if (!TestEqual(TEXT("Begin Play creates exactly one replacement batch"), Batches.Num(), 1))
    {
        return false;
    }
    TestTrue(TEXT("The rebuilt batch is registered"), Batches[0]->IsRegistered());
    TestEqual(TEXT("The rebuilt batch contains the saved cell"), Batches[0]->GetInstanceCount(), 1);
    return true;
}

#endif
