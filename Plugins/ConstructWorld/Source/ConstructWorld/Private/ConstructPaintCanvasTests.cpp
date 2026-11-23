#include "ConstructPaintCanvas.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Subsystem/CrowdyVoxelWorld.h"

struct FConstructPaintTestAccess
{
    static bool MakeWrite(const AConstructPaintCanvas& Canvas, FIntVector Cell, int32 Type, FCrowdyVoxelWrite& Write)
    {
        return Canvas.MakeWrite(Cell, Type, Write);
    }
};

namespace
{
    constexpr auto PaintTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    struct FPaintFixture
    {
        FPaintFixture()
        {
            Canvas = World->SpawnActor<AConstructPaintCanvas>();
        }

        ~FPaintFixture()
        {
            World->DestroyWorld(false);
        }

        UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
        AConstructPaintCanvas* Canvas = nullptr;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPaintLocation, "Construct.Paint.Canvas.Location", PaintTestFlags)
bool FConstructPaintLocation::RunTest(const FString&)
{
    const FPaintFixture Fixture;
    Fixture.Canvas->SetActorLocation(FVector(103200, 800, 10));
    FCrowdyVoxelWrite Write;
    TestTrue(TEXT("The canvas makes a floor write"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector(1, 2, 0), 1, Write));
    TestEqual(TEXT("The Paint floor is in chunk 64"), FInt64Vector3(Write.ChunkX, Write.ChunkY, Write.ChunkZ), FInt64Vector3(64, 0, 0));

    Fixture.Canvas->SetActorLocation(FVector(-10, -1610, 1710));
    TestTrue(TEXT("The moved canvas makes a floor write"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector(1, 2, 0), 1, Write));
    TestEqual(TEXT("The write follows the moved canvas"), FInt64Vector3(Write.ChunkX, Write.ChunkY, Write.ChunkZ), FInt64Vector3(-1, -2, 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPaintFloor, "Construct.Paint.Canvas.FloorOnly", PaintTestFlags)
bool FConstructPaintFloor::RunTest(const FString&)
{
    const FPaintFixture Fixture;
    FCrowdyVoxelWrite Write;
    TestTrue(TEXT("The last floor cell is accepted"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector(15, 15, 0), 1, Write));
    TestFalse(TEXT("A cell above the floor is ignored"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector(1, 2, 1), 1, Write));
    TestFalse(TEXT("A cell outside the chunk is ignored"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector(16, 0, 0), 1, Write));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPaintType, "Construct.Paint.Canvas.BlockTypes", PaintTestFlags)
bool FConstructPaintType::RunTest(const FString&)
{
    const FPaintFixture Fixture;
    FCrowdyVoxelWrite Write;
    TestTrue(TEXT("Erase is allowed"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector::ZeroValue, 0, Write));
    TestEqual(TEXT("Erase writes type zero"), Write.Voxel.Type, 0);
    TestTrue(TEXT("Block 12 is allowed"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector::ZeroValue, 12, Write));
    TestFalse(TEXT("Block 13 is ignored"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector::ZeroValue, 13, Write));
    TestFalse(TEXT("A negative block is ignored"), FConstructPaintTestAccess::MakeWrite(*Fixture.Canvas, FIntVector::ZeroValue, -1, Write));
    return true;
}

#endif
