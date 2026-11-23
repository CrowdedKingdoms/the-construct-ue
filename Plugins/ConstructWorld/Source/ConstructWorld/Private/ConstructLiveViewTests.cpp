#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ConstructChunkView.h"
#include "ConstructLiveViewTestReceiver.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Subsystem/CrowdyVoxelWorld.h"

struct FConstructLiveViewTestAccess
{
    using FVisitCell = TFunctionRef<void(FIntVector, int32)>;
    using FVisitChunk = TFunctionRef<void(const FInt64Vector3&, FVisitCell)>;

    static void Begin(AConstructChunkView& View)
    {
        View.BeginLiveDraw(FInt64Vector3::ZeroValue);
    }

    static void Add(AConstructChunkView& View, const FInt64Vector3& Chunk, TFunctionRef<void(FVisitCell)> ForEachCell, FVisitChunk VisitChunk)
    {
        View.AddLiveChunk(Chunk, ForEachCell, VisitChunk);
    }

    static void Change(AConstructChunkView& View, const FInt64Vector3& Chunk, const TArray<FIntVector>& Positions, TFunctionRef<int32(FIntVector)> ReadCell, FVisitChunk VisitChunk)
    {
        View.ChangeLiveCells(Chunk, Positions, ReadCell, VisitChunk);
    }

    static bool IsLive(const AConstructChunkView& View)
    {
        return View.bLiveDraw;
    }

    static int32 FindInstanceId(const AConstructChunkView& View, const FInt64Vector3& Chunk, const FIntVector& Cell)
    {
        const auto* Drawn = View.DrawnCubes.Find({ Chunk, Cell });
        return Drawn ? Drawn->InstanceId.GetAsIndex() : INDEX_NONE;
    }
};

namespace
{
    using FVisitCell = FConstructLiveViewTestAccess::FVisitCell;
    constexpr auto LiveTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    struct FLiveViewFixture
    {
        explicit FLiveViewFixture(EWorldType::Type WorldType = EWorldType::EditorPreview)
        {
            UWorld::InitializationValues Options;
            Options.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(WorldType, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Options);
            if (World)
            {
                View = World->SpawnActor<AConstructChunkView>();
                if (View && WorldType == EWorldType::EditorPreview)
                {
                    FConstructLiveViewTestAccess::Begin(*View);
                }
            }
        }

        ~FLiveViewFixture()
        {
            if (World)
            {
                World->DestroyWorld(false);
            }
        }

        void AddChunk(const FInt64Vector3& Chunk, TArray<FConstructChunkCell> Cells)
        {
            SourceCells.Add(Chunk, MoveTemp(Cells));
            FConstructLiveViewTestAccess::Add(*View, Chunk,
                [this, &Chunk](FVisitCell Visit)
                {
                    VisitCells(Chunk, Visit);
                },
                [this](const FInt64Vector3& Coord, FVisitCell Visit)
                {
                    ++FullChunkVisits;
                    VisitCells(Coord, Visit);
                });
        }

        void SetSourceType(const FInt64Vector3& Chunk, const FIntVector& Cell, int32 Type)
        {
            TArray<FConstructChunkCell>& Cells = SourceCells.FindChecked(Chunk);
            FConstructChunkCell* Existing = Cells.FindByPredicate([&Cell](const FConstructChunkCell& Entry)
            {
                return Entry.Cell == Cell;
            });
            if (Existing)
            {
                Existing->Type = Type;
            }
            else
            {
                Cells.Add({ Cell, Type });
            }
        }

        void Change(const FInt64Vector3& Chunk, const TArray<FIntVector>& Positions)
        {
            FConstructLiveViewTestAccess::Change(*View, Chunk, Positions,
                [this, &Chunk](FIntVector Cell)
                {
                    Reads.Add(Cell);
                    const auto* Entry = SourceCells.FindChecked(Chunk).FindByPredicate([&Cell](const FConstructChunkCell& Candidate)
                    {
                        return Candidate.Cell == Cell;
                    });
                    return Entry ? Entry->Type : 0;
                },
                [this](const FInt64Vector3& Coord, FVisitCell Visit)
                {
                    ++FullChunkVisits;
                    VisitCells(Coord, Visit);
                });
        }

        int32 DrawnType(const FInt64Vector3& Chunk, const FIntVector& Cell) const
        {
            FVector Centre;
            int32 Type = 0;
            return View->FindCube(Chunk.X, Chunk.Y, Chunk.Z, Cell, Centre, Type) ? Type : 0;
        }

        void VisitCells(const FInt64Vector3& Chunk, FVisitCell Visit) const
        {
            for (const FConstructChunkCell& Entry : SourceCells.FindChecked(Chunk))
            {
                if (Entry.Type != 0)
                {
                    Visit(Entry.Cell, Entry.Type);
                }
            }
        }

        UWorld* World = nullptr;
        AConstructChunkView* View = nullptr;
        TMap<FInt64Vector3, TArray<FConstructChunkCell>> SourceCells;
        TArray<FIntVector> Reads;
        int32 FullChunkVisits = 0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLiveViewNamedCells, "Construct.Paint.LiveView.NamedCells", LiveTestFlags)

bool FConstructLiveViewNamedCells::RunTest(const FString&)
{
    FLiveViewFixture Fixture;
    if (!TestNotNull(TEXT("Chunk view"), Fixture.View))
    {
        return false;
    }

    const FInt64Vector3 Near = FInt64Vector3::ZeroValue;
    const FInt64Vector3 Other(2, 0, 0);
    const FIntVector Removed(0, 0, 0);
    const FIntVector Unnamed(1, 0, 0);
    const FIntVector Added(2, 0, 0);
    Fixture.AddChunk(Near, {{ Removed, 1 }, { Unnamed, 1 }});
    Fixture.AddChunk(Other, {{ Removed, 1 }});
    const int32 OtherId = FConstructLiveViewTestAccess::FindInstanceId(*Fixture.View, Other, Removed);
    if (!TestTrue(TEXT("The other chunk has a stable instance ID"), OtherId != INDEX_NONE))
    {
        return false;
    }

    FVector OtherCentreBefore;
    int32 OtherType = 0;
    if (!TestTrue(TEXT("The other chunk starts with a readable instance"),
        Fixture.View->FindCube(Other.X, Other.Y, Other.Z, Removed, OtherCentreBefore, OtherType)))
    {
        return false;
    }

    Fixture.SetSourceType(Near, Removed, 0);
    Fixture.SetSourceType(Near, Unnamed, 7);
    Fixture.SetSourceType(Near, Added, 2);
    const int32 VisitsBefore = Fixture.FullChunkVisits;
    Fixture.Change(Near, { Removed, Added });

    TestEqual(TEXT("Only two positions are read"), Fixture.Reads.Num(), 2);
    TestTrue(TEXT("Reads name both affected positions"), Fixture.Reads.Contains(Removed) && Fixture.Reads.Contains(Added));
    TestEqual(TEXT("Under cap, no whole chunk is reread"), Fixture.FullChunkVisits, VisitsBefore);
    TestEqual(TEXT("The removed cube is gone"), Fixture.DrawnType(Near, Removed), 0);
    TestEqual(TEXT("The new cube is drawn"), Fixture.DrawnType(Near, Added), 2);
    TestEqual(TEXT("The unnamed cell still has its drawn type"), Fixture.DrawnType(Near, Unnamed), 1);
    FVector OtherCentreAfter;
    if (TestTrue(TEXT("The other chunk still has a readable instance"),
        Fixture.View->FindCube(Other.X, Other.Y, Other.Z, Removed, OtherCentreAfter, OtherType)))
    {
        TestEqual(TEXT("Other chunk still has its drawn type"), OtherType, 1);
        TestEqual(TEXT("Other chunk retains its world transform"), OtherCentreAfter, OtherCentreBefore);
    }
    TestEqual(TEXT("The other chunk's stable instance is untouched"),
        FConstructLiveViewTestAccess::FindInstanceId(*Fixture.View, Other, Removed), OtherId);

    const FIntVector Wide(3, 0, 0);
    Fixture.SetSourceType(Near, Wide, 256);
    Fixture.Change(Near, {Wide});
    TestEqual(TEXT("A live type outside the grid range is empty"), Fixture.DrawnType(Near, Wide), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLiveViewCapOrder, "Construct.Paint.LiveView.CapOrder", LiveTestFlags)

bool FConstructLiveViewCapOrder::RunTest(const FString&)
{
    FLiveViewFixture Fixture;
    if (!TestNotNull(TEXT("Chunk view"), Fixture.View))
    {
        return false;
    }

    const FInt64Vector3 Near = FInt64Vector3::ZeroValue;
    const FInt64Vector3 Far(2, 0, 0);
    const FIntVector First = FIntVector::ZeroValue;
    const FIntVector Last(15, 15, 15);
    TArray<FConstructChunkCell> NearCells;
    NearCells.Reserve(4095);
    for (int32 Index = 0; Index < 4095; ++Index)
    {
        NearCells.Add({ FIntVector(Index & 15, (Index >> 4) & 15, Index >> 8), 1 });
    }

    Fixture.AddChunk(Far, {{ First, 2 }});
    Fixture.AddChunk(Near, MoveTemp(NearCells));
    TestEqual(TEXT("The view starts exactly at the cap"), Fixture.View->GetCubeCount(), 4096);
    TestEqual(TEXT("The last slot initially holds the farther cube"), Fixture.DrawnType(Far, First), 2);

    Fixture.SetSourceType(Near, Last, 3);
    Fixture.Change(Near, { Last });
    TestEqual(TEXT("Crossing the cap keeps 4096 cubes"), Fixture.View->GetCubeCount(), 4096);
    TestEqual(TEXT("The new nearer cube takes the last slot"), Fixture.DrawnType(Near, Last), 3);
    TestEqual(TEXT("The farther cube is displaced"), Fixture.DrawnType(Far, First), 0);

    Fixture.SetSourceType(Near, Last, 0);
    Fixture.Change(Near, { Last });
    TestEqual(TEXT("A newly free slot restores the farther cube"), Fixture.DrawnType(Far, First), 2);
    TestEqual(TEXT("The old nearer cube was removed"), Fixture.DrawnType(Near, Last), 0);
    TestEqual(TEXT("The farther cube refills the cap"), Fixture.View->GetCubeCount(), 4096);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLiveViewRedrawn, "Construct.Paint.LiveView.Redrawn", LiveTestFlags)

bool FConstructLiveViewRedrawn::RunTest(const FString&)
{
    FLiveViewFixture Fixture;
    if (!TestNotNull(TEXT("Chunk view"), Fixture.View))
    {
        return false;
    }

    const FInt64Vector3 Chunk(3, -2, 0);
    const FIntVector Retyped(1, 2, 3);
    const FIntVector Removed(4, 5, 6);
    Fixture.AddChunk(Chunk, {{ Retyped, 1 }, { Removed, 2 }});

    int32 Broadcasts = 0;
    auto* Receiver = NewObject<UConstructLiveViewTestReceiver>(Fixture.View);
    Receiver->Handle = [this, &Fixture, &Chunk, &Retyped, &Removed, &Broadcasts](int64 X, int64 Y, int64 Z, const TArray<FIntVector>& Positions)
    {
        ++Broadcasts;
        TestTrue(TEXT("OnRedrawn names the actual chunk"), FInt64Vector3(X, Y, Z) == Chunk);
        TestEqual(TEXT("OnRedrawn names exactly two cells"), Positions.Num(), 2);
        TestTrue(TEXT("OnRedrawn names the retyped and removed cells"), Positions.Contains(Retyped) && Positions.Contains(Removed));
        TestEqual(TEXT("OnRedrawn sees the new type through FindCube"), Fixture.DrawnType(Chunk, Retyped), 7);
        TestEqual(TEXT("OnRedrawn sees that the erased cube is gone"), Fixture.DrawnType(Chunk, Removed), 0);
    };
    Fixture.View->OnRedrawn.AddDynamic(Receiver, &UConstructLiveViewTestReceiver::Redrawn);

    Fixture.SetSourceType(Chunk, Retyped, 7);
    Fixture.SetSourceType(Chunk, Removed, 0);
    Fixture.Change(Chunk, { Retyped, Removed });

    Fixture.View->OnRedrawn.RemoveDynamic(Receiver, &UConstructLiveViewTestReceiver::Redrawn);
    TestEqual(TEXT("One chunk update broadcasts once"), Broadcasts, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLiveViewStop, "Construct.Paint.LiveView.ShowChunksStops", LiveTestFlags)

bool FConstructLiveViewStop::RunTest(const FString&)
{
    FLiveViewFixture Fixture(EWorldType::Game);
    if (!TestNotNull(TEXT("Chunk view"), Fixture.View))
    {
        return false;
    }
    const auto* VoxelWorld = Fixture.World->GetSubsystem<UCrowdyVoxelWorld>();
    if (!TestNotNull(TEXT("Real voxel world subsystem"), VoxelWorld))
    {
        return false;
    }

    Fixture.View->ShowLoadedChunks(0, 0, 0);
    TestTrue(TEXT("Live view binds the chunk load event"), VoxelWorld->OnChunkLoaded.GetAllObjects().Contains(Fixture.View));
    TestTrue(TEXT("Live view binds the chunk unload event"), VoxelWorld->OnChunkUnloaded.GetAllObjects().Contains(Fixture.View));
    TestTrue(TEXT("Live view binds the chunk changed event"), VoxelWorld->OnChunkChanged.GetAllObjects().Contains(Fixture.View));

    const FInt64Vector3 LiveChunk = FInt64Vector3::ZeroValue;
    const FIntVector First = FIntVector::ZeroValue;
    Fixture.AddChunk(LiveChunk, {{ First, 1 }});
    TestTrue(TEXT("The view starts in live mode"), FConstructLiveViewTestAccess::IsLive(*Fixture.View));
    TestEqual(TEXT("A live cube was present"), Fixture.DrawnType(LiveChunk, First), 1);

    FCrowdyVoxelChunk Snapshot;
    Snapshot.X = 2;
    Snapshot.Grid.Add(4);
    Fixture.View->ShowChunks({ Snapshot }, 0, 0, 0);
    TestFalse(TEXT("ShowChunks ends live mode"), FConstructLiveViewTestAccess::IsLive(*Fixture.View));
    TestFalse(TEXT("ShowChunks unbinds the chunk load event"), VoxelWorld->OnChunkLoaded.GetAllObjects().Contains(Fixture.View));
    TestFalse(TEXT("ShowChunks unbinds the chunk unload event"), VoxelWorld->OnChunkUnloaded.GetAllObjects().Contains(Fixture.View));
    TestFalse(TEXT("ShowChunks unbinds the chunk changed event"), VoxelWorld->OnChunkChanged.GetAllObjects().Contains(Fixture.View));
    TestEqual(TEXT("The live cube was replaced"), Fixture.DrawnType(LiveChunk, First), 0);
    TestEqual(TEXT("The fetched cube is now shown"), Fixture.DrawnType(FInt64Vector3(2, 0, 0), First), 4);
    return true;
}

#endif
