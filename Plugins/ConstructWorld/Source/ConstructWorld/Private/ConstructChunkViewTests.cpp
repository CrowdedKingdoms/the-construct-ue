#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ConstructChunkView.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

namespace
{
    struct FChunkViewTestWorld
    {
        explicit FChunkViewTestWorld(const FTransform& Transform = FTransform::Identity)
        {
            UWorld::InitializationValues Options;
            Options.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Options);
            if (World)
            {
                View = World->SpawnActor<AConstructChunkView>(AConstructChunkView::StaticClass(), Transform);
            }
        }

        ~FChunkViewTestWorld()
        {
            if (World)
            {
                World->DestroyWorld(false);
            }
        }

        UWorld* World = nullptr;
        AConstructChunkView* View = nullptr;
    };

    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkViewPlacement, "Construct.Chunks.View.Placement", TestFlags)

bool FConstructChunkViewPlacement::RunTest(const FString&)
{
    FChunkViewTestWorld TestWorld;
    AConstructChunkView* View = TestWorld.View;
    if (!TestNotNull(TEXT("Chunk view"), View))
    {
        return false;
    }

    View->ShowCells({
        { FInt64Vector3::ZeroValue, { { FIntVector(1, 2, 3), 1 } } },
        { FInt64Vector3(-1, -1, -1), { { FIntVector::ZeroValue, 2 } } }
    }, FInt64Vector3::ZeroValue);
    FVector Centre;
    int32 Type;
    if (TestTrue(TEXT("Asymmetric cell exists"), View->FindCube(0, 0, 0, FIntVector(1, 2, 3), Centre, Type)))
    {
        TestEqual(TEXT("Asymmetric cell uses Unreal XYZ"), Centre, FVector(150, 250, 350));
    }
    if (TestTrue(TEXT("Negative chunk cell exists"), View->FindCube(-1, -1, -1, FIntVector::ZeroValue, Centre, Type)))
    {
        TestEqual(TEXT("Negative chunk uses its minimum corner"), Centre, FVector(-1550));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkViewPalette, "Construct.Chunks.View.Palette", TestFlags)

bool FConstructChunkViewPalette::RunTest(const FString&)
{
    FChunkViewTestWorld TestWorld;
    AConstructChunkView* View = TestWorld.View;
    if (!TestNotNull(TEXT("Chunk view"), View))
    {
        return false;
    }

    const TCHAR* HexColours[] = {
        TEXT("14532d"), TEXT("6b3a1f"), TEXT("f8fafc"), TEXT("5ef2c8"), TEXT("ff7ac8"), TEXT("7ab8ff"),
        TEXT("ffd166"), TEXT("c084fc"), TEXT("ff8f6b"), TEXT("9ff27a"), TEXT("6be5ff"), TEXT("111827")
    };
    FConstructChunkCells Chunk;
    for (uint8 Type = 0; Type <= 13; ++Type)
    {
        Chunk.Cells.Add({ FIntVector(Type, 0, 0), Type });
    }
    View->ShowCells({ Chunk }, FInt64Vector3::ZeroValue);
    TestEqual(TEXT("Only the 13 occupied cells draw"), View->GetCubeCount(), 13);

    TInlineComponentArray<UInstancedStaticMeshComponent*> Batches(View);
    TMap<int32, UMaterialInterface*> MaterialsByCell;
    for (UInstancedStaticMeshComponent* Batch : Batches)
    {
        TestEqual(TEXT("Cube batch has no collision"), Batch->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
        for (int32 Index = 0; Index < Batch->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            if (TestTrue(TEXT("Material is read from a drawn instance"), Batch->GetInstanceTransform(Index, Instance, true)))
            {
                MaterialsByCell.Add(FMath::FloorToInt(Instance.GetLocation().X / 100.0), Batch->GetMaterial(0));
            }
        }
    }
    TestFalse(TEXT("Type 0 has no mesh instance"), MaterialsByCell.Contains(0));
    for (int32 Type = 1; Type <= 12; ++Type)
    {
        UMaterialInterface* Material = MaterialsByCell.FindRef(Type);
        if (TestNotNull(FString::Printf(TEXT("Type %d material"), Type), Material))
        {
            FLinearColor Colour;
            if (TestTrue(TEXT("The block material exposes its colour"), Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BlockColor")), Colour)))
            {
                TestEqual(FString::Printf(TEXT("Type %d sRGB"), Type), Colour.ToFColorSRGB(), FColor::FromHex(HexColours[Type - 1]));
            }
            TestTrue(TEXT("The block parent supports instancing"), Material->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        }
    }
    TestTrue(TEXT("Type 13 shares type 1's material"), MaterialsByCell.FindRef(13) == MaterialsByCell.FindRef(1));
    FVector Centre;
    int32 Type = 0;
    TestTrue(TEXT("Wrapped type has a drawn cube"), View->FindCube(0, 0, 0, FIntVector(13, 0, 0), Centre, Type));
    TestEqual(TEXT("Wrapped cube reports its original type"), Type, 13);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkViewReplace, "Construct.Chunks.View.ReplaceAndClear", TestFlags)

bool FConstructChunkViewReplace::RunTest(const FString&)
{
    FChunkViewTestWorld TestWorld;
    AConstructChunkView* View = TestWorld.View;
    if (!TestNotNull(TEXT("Chunk view"), View))
    {
        return false;
    }

    View->ShowCells({ { FInt64Vector3::ZeroValue, { { FIntVector::ZeroValue, 1 } } } }, FInt64Vector3::ZeroValue);
    View->ShowCells({ { FInt64Vector3::ZeroValue, { { FIntVector(1, 0, 0), 2 } } } }, FInt64Vector3::ZeroValue);
    FVector Centre;
    int32 Type;
    TestEqual(TEXT("Second draw replaces the first"), View->GetCubeCount(), 1);
    TestFalse(TEXT("The old cube is gone"), View->FindCube(0, 0, 0, FIntVector::ZeroValue, Centre, Type));
    TestTrue(TEXT("The replacement cube is drawn"), View->FindCube(0, 0, 0, FIntVector(1, 0, 0), Centre, Type));

    View->ClearChunks();
    TestEqual(TEXT("Clear resets the count"), View->GetCubeCount(), 0);
    TInlineComponentArray<UInstancedStaticMeshComponent*> Batches(View);
    for (const UInstancedStaticMeshComponent* Batch : Batches)
    {
        TestEqual(TEXT("Clear removes drawn geometry"), Batch->GetInstanceCount(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkViewCap, "Construct.Chunks.View.CapAndOrder", TestFlags)

bool FConstructChunkViewCap::RunTest(const FString&)
{
    FChunkViewTestWorld TestWorld;
    AConstructChunkView* View = TestWorld.View;
    if (!TestNotNull(TEXT("Chunk view"), View))
    {
        return false;
    }

    const FIntVector First(0, 0, 0);
    const FIntVector Last(15, 15, 15);
    FConstructChunkCells CentreChunk;
    for (int32 Index = 0; Index < 4095; ++Index)
    {
        CentreChunk.Cells.Add({ FIntVector(Index % 16, Index / 16 % 16, Index / 256), 1 });
    }
    View->ShowCells({
        { FInt64Vector3(3, 0, 0), { { First, 1 } } },
        { FInt64Vector3(2, 2, 2), { { First, 1 }, { Last, 1 } } },
        { FInt64Vector3(-2, 0, 0), { { First, 1 } } },
        CentreChunk
    }, FInt64Vector3::ZeroValue);

    FVector Centre;
    int32 Type;
    TestEqual(TEXT("The view caps drawn cubes at 4096"), View->GetCubeCount(), 4096);
    TestTrue(TEXT("The centre chunk is drawn first"), View->FindCube(0, 0, 0, FIntVector(14, 15, 15), Centre, Type));
    TestTrue(TEXT("Largest axis distance and input tie order pick the first tied chunk"), View->FindCube(2, 2, 2, First, Centre, Type));
    TestFalse(TEXT("The crossing chunk stops at the cap"), View->FindCube(2, 2, 2, Last, Centre, Type));
    TestFalse(TEXT("A later chunk at equal distance is excluded"), View->FindCube(-2, 0, 0, First, Centre, Type));
    TestFalse(TEXT("The farther chunk is excluded"), View->FindCube(3, 0, 0, First, Centre, Type));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkViewFind, "Construct.Chunks.View.FindCube", TestFlags)

bool FConstructChunkViewFind::RunTest(const FString&)
{
    FChunkViewTestWorld TestWorld;
    AConstructChunkView* View = TestWorld.View;
    if (!TestNotNull(TEXT("Chunk view"), View))
    {
        return false;
    }

    View->ShowCells({ { FInt64Vector3::ZeroValue, { { FIntVector(1, 2, 3), 4 } } } }, FInt64Vector3::ZeroValue);
    const FVector MovedCentre(934, -432, 832);
    TInlineComponentArray<UInstancedStaticMeshComponent*> Batches(View);
    bool bUpdated = false;
    for (UInstancedStaticMeshComponent* Batch : Batches)
    {
        if (Batch->GetInstanceCount() == 1)
        {
            bUpdated = Batch->UpdateInstanceTransform(0, FTransform(MovedCentre), true, true, true);
        }
    }
    TestTrue(TEXT("The test moves the real instance"), bUpdated);
    FVector Centre;
    int32 Type;
    TestTrue(TEXT("The moved cube still exists"), View->FindCube(0, 0, 0, FIntVector(1, 2, 3), Centre, Type));
    TestEqual(TEXT("FindCube returns the moved instance centre"), Centre, MovedCentre);
    TestEqual(TEXT("FindCube returns the drawn type"), Type, 4);
    TestFalse(TEXT("FindCube is false for an empty cell"), View->FindCube(0, 0, 0, FIntVector::ZeroValue, Centre, Type));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkViewShowChunks, "Construct.Chunks.View.ShowChunks", TestFlags)

bool FConstructChunkViewShowChunks::RunTest(const FString&)
{
    FChunkViewTestWorld TestWorld;
    AConstructChunkView* View = TestWorld.View;
    if (!TestNotNull(TEXT("Chunk view"), View))
    {
        return false;
    }

    // A hand-built fetched chunk: two grid cells, and edits the grid cannot hold.
    FCrowdyVoxelChunk Chunk;
    Chunk.X = 37;
    Chunk.Y = -52;
    Chunk.Z = 1;
    Chunk.Grid.SetNumZeroed(4096);
    Chunk.Grid[1 + 16 * 2 + 256 * 3] = 9;
    Chunk.Grid[15 + 16 * 15 + 256 * 15] = 12;
    const auto AddEdit = [&Chunk](FIntVector Position, int32 Type)
    {
        FCrowdyVoxel& Edit = Chunk.Edits.AddDefaulted_GetRef();
        Edit.Position = Position;
        Edit.Type = Type;
    };
    AddEdit(FIntVector(-4, 0, 0), 5);
    AddEdit(FIntVector(4, 4, 4), 300);
    AddEdit(FIntVector(5, 5, 5), -5);
    AddEdit(FIntVector(20, 0, 0), 3);

    View->ShowChunks({ Chunk }, 37, -52, 1);
    FVector Centre;
    int32 Type = 0;
    TestEqual(TEXT("Exactly the grid's two filled cells draw"), View->GetCubeCount(), 2);
    if (TestTrue(TEXT("The lone cube draws"), View->FindCube(37, -52, 1, FIntVector(1, 2, 3), Centre, Type)))
    {
        TestEqual(TEXT("At C5's predicted centre"), Centre, FVector(59350, -82950, 1950));
        TestEqual(TEXT("With its type"), Type, 9);
    }
    TestTrue(TEXT("The far corner draws"), View->FindCube(37, -52, 1, FIntVector(15, 15, 15), Centre, Type));
    TestFalse(TEXT("A wide type inside the grid is not drawn"), View->FindCube(37, -52, 1, FIntVector(4, 4, 4), Centre, Type));
    TestFalse(TEXT("A negative type is not drawn"), View->FindCube(37, -52, 1, FIntVector(5, 5, 5), Centre, Type));

    View->ShowCells({ { FInt64Vector3::ZeroValue, { { FIntVector(1, 1, 1), -5 } } } }, FInt64Vector3::ZeroValue);
    TestEqual(TEXT("A hand-built negative type draws nothing"), View->GetCubeCount(), 0);

    // Two solid chunks, the far one first: the cap goes to the nearer one, whatever the order given.
    FCrowdyVoxelChunk Far;
    Far.X = 3;
    Far.Grid.Init(2, 4096);
    FCrowdyVoxelChunk Near = Far;
    Near.X = 1;
    View->ShowChunks({ Far, Near }, 0, 0, 0);
    TestEqual(TEXT("Two solid chunks stop at the cap"), View->GetCubeCount(), 4096);
    TestTrue(TEXT("The nearer chunk is drawn"), View->FindCube(1, 0, 0, FIntVector(15, 15, 15), Centre, Type));
    TestFalse(TEXT("The farther chunk is not"), View->FindCube(3, 0, 0, FIntVector::ZeroValue, Centre, Type));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructChunkViewWorldSpace, "Construct.Chunks.View.WorldSpace", TestFlags)

bool FConstructChunkViewWorldSpace::RunTest(const FString&)
{
    const FTransform ViewTransform(FRotator(10, 90, 20), FVector(-2000, 500, 1000), FVector(2, 3, 4));
    FChunkViewTestWorld TestWorld(ViewTransform);
    AConstructChunkView* View = TestWorld.View;
    if (!TestNotNull(TEXT("Chunk view"), View))
    {
        return false;
    }

    View->ShowCells({ { FInt64Vector3(1, -2, 3), { { FIntVector(1, 2, 3), 1 } } } }, FInt64Vector3::ZeroValue);
    View->SetActorTransform(FTransform(FRotator(30, 10, 90), FVector(1000, -500, 2000), FVector(3, 2, 1)));
    const FVector Expected(1750, -2950, 5150);
    FVector Centre;
    int32 Type;
    TestTrue(TEXT("A transformed view draws the cube"), View->FindCube(1, -2, 3, FIntVector(1, 2, 3), Centre, Type));
    TestEqual(TEXT("The actor's transform cannot move the drawn cube"), Centre, Expected);
    TInlineComponentArray<UInstancedStaticMeshComponent*> Batches(View);
    for (const UInstancedStaticMeshComponent* Batch : Batches)
    {
        if (Batch->GetInstanceCount() == 1)
        {
            FTransform Instance;
            if (TestTrue(TEXT("The drawn transform can be read"), Batch->GetInstanceTransform(0, Instance, true)))
            {
                TestEqual(TEXT("The actor's scale cannot distort the cube"), Instance.GetScale3D(), FVector::OneVector);
                TestTrue(TEXT("The actor's rotation cannot rotate the cube"), Instance.GetRotation().Equals(FQuat::Identity));
            }
        }
    }
    return true;
}

#endif
