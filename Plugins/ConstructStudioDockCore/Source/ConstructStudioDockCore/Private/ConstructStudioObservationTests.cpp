#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioObservation.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace ConstructStudioObservationTests
{
	constexpr EAutomationTestFlags ObsFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** The player at (150, -250, 1600.5), in chunk (0, -1, 1) and the claimed grid over it, with Others around. */
	FConstructStudioObservationInput ObsInput()
	{
		FConstructStudioObservationInput In;
		In.ObservationId = TEXT("obs-1");
		In.ControlledEntityId = TEXT("0123456789abcdef0123456789abcdef");
		In.Now = FDateTime(2026, 11, 17, 10, 0, 0);
		In.Location = FVector(150.0, -250.0, 1600.5);
		In.Velocity = FVector(100.0, 0.0, -50.0);
		In.Look = FRotator(350.0, 370.0, 0.0);
		In.VoxelSize = 100.0;
		FConstructStudioObservedGrid& Grid = In.Grid.Emplace();
		Grid.GridId = 100;
		Grid.Low = FInt64Vector(0, -1, 1);
		Grid.High = Grid.Low;
		Grid.bOwned = true;
		return In;
	}

	/** Another player at Location, standing in Chunk; by default the grid's own chunk (0, -1, 1). */
	void ObsAddOther(FConstructStudioObservationInput& In, const FString& ActorId, const FString& Label, const FVector& Location,
		const FInt64Vector& Chunk = FInt64Vector(0, -1, 1))
	{
		FConstructStudioObservedActor& Other = In.Others.AddDefaulted_GetRef();
		Other.ActorId = ActorId;
		Other.Label = Label;
		Other.Location = Location;
		Other.Chunk = Chunk;
	}

	FString ObsVector(const TSharedPtr<FJsonObject>& Vector)
	{
		return Vector.IsValid() ? Vector->GetStringField(TEXT("x")) + TEXT(",") + Vector->GetStringField(TEXT("y")) + TEXT(",") + Vector->GetStringField(TEXT("z")) : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioObservationShapeTest, "Construct.StudioDock.Observation.ShapeAndNumbers", ConstructStudioObservationTests::ObsFlags)
bool FConstructStudioObservationShapeTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioObservationTests;
	FConstructStudioObservationInput In = ObsInput();
	In.bModalOpen = true;
	// Twenty players inside the grid, listed farthest first, 10 uu apart along +X.
	for (int32 Step = 20; Step >= 1; --Step)
	{
		ObsAddOther(In, FString::Printf(TEXT("p%02d"), Step), FString::Printf(TEXT("Player %d"), Step), FVector(150.0 + 10.0 * Step, -250.0, 1600.5));
	}
	ObsAddOther(In, TEXT("outside"), TEXT("Next door"), FVector(-10.0, -250.0, 1600.5), FInt64Vector(-1, -1, 1));
	ObsAddOther(In, FString(), TEXT("No entity"), FVector(151.0, -250.0, 1600.5));
	const TSharedRef<FJsonObject> Out = UConstructStudioObservation::Build(In);

	TestEqual(TEXT("the platform's contract"), Out->GetStringField(TEXT("contractVersion")), FString(TEXT("crowdy.game-observation/1")));
	TestEqual(TEXT("its id"), Out->GetStringField(TEXT("observationId")), FString(TEXT("obs-1")));
	TestFalse(TEXT("a capability revision"), Out->GetStringField(TEXT("capabilityRevision")).IsEmpty());
	TestEqual(TEXT("the controlled entity"), Out->GetStringField(TEXT("controlledEntityId")), In.ControlledEntityId);
	TestEqual(TEXT("observed now"), Out->GetStringField(TEXT("observedAt")), FString(TEXT("2026-11-17T10:00:00.000Z")));
	TestEqual(TEXT("current for 1.5 s"), Out->GetStringField(TEXT("expiresAt")), FString(TEXT("2026-11-17T10:00:01.500Z")));

	const TSharedPtr<FJsonObject> Player = Out->GetObjectField(TEXT("player"));
	TestEqual(TEXT("the position in cells, one to one and z up"), ObsVector(Player->GetObjectField(TEXT("position"))), FString(TEXT("1.5,-2.5,16.005")));
	TestEqual(TEXT("the velocity in cells"), ObsVector(Player->GetObjectField(TEXT("velocity"))), FString(TEXT("1,0,-0.5")));
	TestEqual(TEXT("the yaw normalized, in degrees"), Player->GetObjectField(TEXT("look"))->GetStringField(TEXT("yaw")), FString(TEXT("10")));
	TestEqual(TEXT("the pitch normalized, in degrees"), Player->GetObjectField(TEXT("look"))->GetStringField(TEXT("pitch")), FString(TEXT("-10")));
	TestEqual(TEXT("health as a decimal string"), Player->GetStringField(TEXT("health")), FString(TEXT("1")));
	TestTrue(TEXT("alive"), Player->GetBoolField(TEXT("alive")));
	const TSharedPtr<FJsonObject> Controlled = Out->GetObjectField(TEXT("controlledEntity"));
	TestEqual(TEXT("the controlled entity is the player"), Controlled->GetStringField(TEXT("kind")), FString(TEXT("PLAYER")));
	TestEqual(TEXT("at the same pose"), ObsVector(Controlled->GetObjectField(TEXT("position"))), FString(TEXT("1.5,-2.5,16.005")));

	const TSharedPtr<FJsonObject>* Grid = nullptr;
	if (TestTrue(TEXT("the grid the player stands in"), Out->TryGetObjectField(TEXT("grid"), Grid)))
	{
		TestEqual(TEXT("by its ref"), (*Grid)->GetStringField(TEXT("gridRef")), FString(TEXT("100")));
		TestEqual(TEXT("its chunk box"), ObsVector((*Grid)->GetObjectField(TEXT("low"))) + TEXT("/") + ObsVector((*Grid)->GetObjectField(TEXT("high"))), FString(TEXT("0,-1,1/0,-1,1")));
		TArray<FString> Scopes;
		(*Grid)->TryGetStringArrayField(TEXT("effectiveScopes"), Scopes);
		TestEqual(TEXT("the owner's scopes there"), FString::Join(Scopes, TEXT(",")), FString(TEXT("observe,grid")));
	}

	const TArray<TSharedPtr<FJsonValue>> Nearby = Out->GetArrayField(TEXT("nearbyActors"));
	if (TestEqual(TEXT("at most 16 nearby players"), Nearby.Num(), UConstructStudioObservation::MaxNearbyActors))
	{
		const TSharedPtr<FJsonObject> Nearest = Nearby[0]->AsObject();
		TestEqual(TEXT("nearest first"), Nearest->GetStringField(TEXT("actorId")), FString(TEXT("p01")));
		TestEqual(TEXT("its distance in cells"), Nearest->GetStringField(TEXT("distance")), FString(TEXT("0.1")));
		TestEqual(TEXT("its position in cells"), ObsVector(Nearest->GetObjectField(TEXT("position"))), FString(TEXT("1.6,-2.5,16.005")));
		TestEqual(TEXT("a player"), Nearest->GetStringField(TEXT("kind")), FString(TEXT("PLAYER")));
		TestEqual(TEXT("neutral"), Nearest->GetStringField(TEXT("disposition")), FString(TEXT("NEUTRAL")));
		TestEqual(TEXT("labelled with their name"), Nearest->GetStringField(TEXT("label")), FString(TEXT("Player 1")));
		TestEqual(TEXT("the 16th nearest last"), Nearby.Last()->AsObject()->GetStringField(TEXT("actorId")), FString(TEXT("p16")));
	}
	const bool bListsOutside = Nearby.ContainsByPredicate([](const TSharedPtr<FJsonValue>& Each) { return Each->AsObject()->GetStringField(TEXT("actorId")) == TEXT("outside"); });
	TestFalse(TEXT("a nearer player outside the grid is left out"), bListsOutside);
	TestTrue(TEXT("no nearby voxels"), Out->GetArrayField(TEXT("nearbyVoxels")).IsEmpty());

	const TSharedPtr<FJsonObject> Input = Out->GetObjectField(TEXT("inputState"));
	TestTrue(TEXT("another modal is open"), Input->GetBoolField(TEXT("modalOpen")));
	TestFalse(TEXT("no text input has focus"), Input->GetBoolField(TEXT("textInputFocused")));
	TestFalse(TEXT("human input is never reported"), Input->GetBoolField(TEXT("humanInputActive")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioObservationNoGridTest, "Construct.StudioDock.Observation.OutsideAGridNobodyIsListed", ConstructStudioObservationTests::ObsFlags)
bool FConstructStudioObservationNoGridTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioObservationTests;
	FConstructStudioObservationInput In = ObsInput();
	In.Grid.Reset();
	ObsAddOther(In, TEXT("p01"), TEXT("Player 1"), FVector(160.0, -250.0, 1600.5));
	const TSharedRef<FJsonObject> Out = UConstructStudioObservation::Build(In);
	TestFalse(TEXT("no grid outside a claimed grid"), Out->HasField(TEXT("grid")));
	TestTrue(TEXT("and no nearby players"), Out->GetArrayField(TEXT("nearbyActors")).IsEmpty());

	FConstructStudioObservationInput Visiting = ObsInput();
	Visiting.Grid->bOwned = false;
	const TSharedRef<FJsonObject> Visited = UConstructStudioObservation::Build(Visiting);
	TArray<FString> Scopes;
	Visited->GetObjectField(TEXT("grid"))->TryGetStringArrayField(TEXT("effectiveScopes"), Scopes);
	TestEqual(TEXT("a grid the player does not own is observed only"), FString::Join(Scopes, TEXT(",")), FString(TEXT("observe")));

	FConstructStudioObservationInput Unknown = ObsInput();
	Unknown.ControlledEntityId.Reset();
	TestFalse(TEXT("a player with no entity yet still has a controlled entity id"), UConstructStudioObservation::Build(Unknown)->GetStringField(TEXT("controlledEntityId")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioObservationChunkTest, "Construct.StudioDock.Observation.NearbyPlayersAreInsideByTheirChunk", ConstructStudioObservationTests::ObsFlags)
bool FConstructStudioObservationChunkTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioObservationTests;
	FConstructStudioObservationInput In = ObsInput();
	// Inside by location but in another chunk, and the other way round: the chunk the observation was given decides.
	ObsAddOther(In, TEXT("near"), TEXT("Near"), FVector(160.0, -250.0, 1600.5), FInt64Vector(0, -1, 2));
	ObsAddOther(In, TEXT("far"), TEXT("Far"), FVector(-5000.0, -250.0, 1600.5));
	const TArray<TSharedPtr<FJsonValue>> Nearby = UConstructStudioObservation::Build(In)->GetArrayField(TEXT("nearbyActors"));
	if (TestEqual(TEXT("one player is inside the grid"), Nearby.Num(), 1))
	{
		TestEqualSensitive(TEXT("the one whose chunk is in the grid"), Nearby[0]->AsObject()->GetStringField(TEXT("actorId")), FString(TEXT("far")));
	}

	FConstructStudioObservationInput Wide = ObsInput();
	Wide.Grid->Low = FInt64Vector(-1, -2, 0);
	Wide.Grid->High = FInt64Vector(1, 0, 2);
	ObsAddOther(Wide, TEXT("corner"), TEXT("Corner"), FVector(0.0), FInt64Vector(1, 0, 2));
	ObsAddOther(Wide, TEXT("past"), TEXT("Past"), FVector(0.0), FInt64Vector(2, 0, 2));
	const TArray<TSharedPtr<FJsonValue>> InBox = UConstructStudioObservation::Build(Wide)->GetArrayField(TEXT("nearbyActors"));
	TestTrue(TEXT("the box's corners are inside it, one chunk past is not"), InBox.Num() == 1 && InBox[0]->AsObject()->GetStringField(TEXT("actorId")).Equals(TEXT("corner"), ESearchCase::CaseSensitive));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioObservationDecimalTest, "Construct.StudioDock.Observation.NumbersAreDecimalStrings", ConstructStudioObservationTests::ObsFlags)
bool FConstructStudioObservationDecimalTest::RunTest(const FString& Parameters)
{
	const TPair<double, const TCHAR*> Cases[] =
	{
		{0.0, TEXT("0")}, {2.0, TEXT("2")}, {1.5, TEXT("1.5")}, {-3.25, TEXT("-3.25")}, {1.23456, TEXT("1.235")},
		{-0.0001, TEXT("0")}, {100000000.0, TEXT("100000000")}, {std::numeric_limits<double>::quiet_NaN(), TEXT("0")},
		{std::numeric_limits<double>::infinity(), TEXT("0")}
	};
	for (const TPair<double, const TCHAR*>& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%s"), Case.Value), UConstructStudioObservation::Decimal(Case.Key), FString(Case.Value));
	}
	return true;
}

#endif
