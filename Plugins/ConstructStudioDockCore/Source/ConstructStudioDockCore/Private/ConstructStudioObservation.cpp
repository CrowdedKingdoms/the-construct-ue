#include "ConstructStudioObservation.h"

#include "ConstructPresencePawn.h"
#include "ConstructUIInputSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Replication/Components/CrowdyEntityComponent.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdyVoxelLibrary.h"

namespace ConstructStudioObservationParts
{
	/** The C1 modal the Studio dock's own panel holds; every other one counts as a modal open. */
	const TCHAR* const DockModal = TEXT("StudioDock");

	TSharedRef<FJsonObject> Vector(const FVector& Cells)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("x"), UConstructStudioObservation::Decimal(Cells.X));
		Out->SetStringField(TEXT("y"), UConstructStudioObservation::Decimal(Cells.Y));
		Out->SetStringField(TEXT("z"), UConstructStudioObservation::Decimal(Cells.Z));
		return Out;
	}

	TSharedRef<FJsonObject> ChunkVector(const FInt64Vector& Chunk)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("x"), LexToString(Chunk.X));
		Out->SetStringField(TEXT("y"), LexToString(Chunk.Y));
		Out->SetStringField(TEXT("z"), LexToString(Chunk.Z));
		return Out;
	}

	bool IsInside(const FInt64Vector& Chunk, const FConstructStudioObservedGrid& Grid)
	{
		return Chunk.X >= Grid.Low.X && Chunk.Y >= Grid.Low.Y && Chunk.Z >= Grid.Low.Z
			&& Chunk.X <= Grid.High.X && Chunk.Y <= Grid.High.Y && Chunk.Z <= Grid.High.Z;
	}

	FString EntityIdOf(const AActor& Actor)
	{
		const UCrowdyEntityComponent* Entity = Actor.FindComponentByClass<UCrowdyEntityComponent>();
		return Entity && Entity->GetNetID().IsValid() ? Entity->GetNetID().ToString(EGuidFormats::DigitsLower) : FString();
	}

	TSharedRef<FJsonObject> GridOf(const FConstructStudioObservedGrid& Grid)
	{
		TArray<TSharedPtr<FJsonValue>> Scopes;
		Scopes.Add(MakeShared<FJsonValueString>(TEXT("observe")));
		if (Grid.bOwned)
		{
			Scopes.Add(MakeShared<FJsonValueString>(TEXT("grid")));
		}
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("gridRef"), LexToString(Grid.GridId));
		Out->SetObjectField(TEXT("low"), ChunkVector(Grid.Low));
		Out->SetObjectField(TEXT("high"), ChunkVector(Grid.High));
		Out->SetArrayField(TEXT("effectiveScopes"), Scopes);
		return Out;
	}

	TSharedRef<FJsonObject> ActorOf(const FConstructStudioObservedActor& Other, double DistanceCells, double Cell)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("actorId"), Other.ActorId.Left(128));
		Out->SetStringField(TEXT("kind"), TEXT("PLAYER"));
		Out->SetObjectField(TEXT("position"), Vector(Other.Location / Cell));
		Out->SetStringField(TEXT("distance"), UConstructStudioObservation::Decimal(DistanceCells));
		Out->SetStringField(TEXT("disposition"), TEXT("NEUTRAL"));
		// Other players choose these names, and the model reads them as text.
		Out->SetStringField(TEXT("label"), Other.Label.Left(128));
		return Out;
	}

	/** The other players standing inside the grid, nearest first; none outside a grid. */
	TArray<TSharedPtr<FJsonValue>> NearbyOf(const FConstructStudioObservationInput& In, double Cell)
	{
		TArray<TSharedPtr<FJsonValue>> Nearby;
		if (!In.Grid.IsSet())
		{
			return Nearby;
		}
		// One pass keeps each listed player's squared distance and index; only the nearest are written out.
		TArray<TPair<double, int32>> Inside;
		Inside.Reserve(In.Others.Num());
		for (int32 Index = 0; Index < In.Others.Num(); ++Index)
		{
			const FConstructStudioObservedActor& Other = In.Others[Index];
			if (!Other.ActorId.IsEmpty() && IsInside(Other.Chunk, In.Grid.GetValue()))
			{
				Inside.Emplace(FVector::DistSquared(In.Location, Other.Location), Index);
			}
		}
		Inside.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
		const int32 Listed = FMath::Min(Inside.Num(), UConstructStudioObservation::MaxNearbyActors);
		for (int32 Rank = 0; Rank < Listed; ++Rank)
		{
			Nearby.Add(MakeShared<FJsonValueObject>(ActorOf(In.Others[Inside[Rank].Value], FMath::Sqrt(Inside[Rank].Key) / Cell, Cell)));
		}
		return Nearby;
	}

	bool IsTextInputFocused()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		const FName Type = Focused.IsValid() ? Focused->GetType() : NAME_None;
		return Type == TEXT("SEditableText") || Type == TEXT("SMultiLineEditableText");
	}
}

void UConstructStudioObservation::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The transient world made at engine start has no game instance, and nothing in it is observed.
	const UWorld* World = GetWorld();
	bHasGame = World && World->GetGameInstance();
}

bool UConstructStudioObservation::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TOptional<FInt64Vector> UConstructStudioObservation::GetPlayerChunk() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* Game = bHasGame && World ? World->GetGameInstance() : nullptr;
	const UCrowdyGameSession* Session = Game ? Game->GetSubsystem<UCrowdyGameSession>() : nullptr;
	FInt64Vector Chunk(0, 0, 0);
	if (!Session || !Session->TryGetLocalPlayerChunk(World, Chunk))
	{
		return {};
	}
	return Chunk;
}

TSharedPtr<FJsonObject> UConstructStudioObservation::Observe(const TOptional<FConstructStudioObservedGrid>& Grid) const
{
	using namespace ConstructStudioObservationParts;
	const UWorld* World = GetWorld();
	APlayerController* Controller = bHasGame && World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Self = Controller ? Controller->GetPawn() : nullptr;
	if (!Self)
	{
		return nullptr;
	}
	FConstructStudioObservationInput In;
	In.ObservationId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	In.ControlledEntityId = EntityIdOf(*Self);
	In.Now = FDateTime::UtcNow();
	In.Location = Self->GetActorLocation();
	In.Velocity = Self->GetVelocity();
	In.Look = Controller->GetControlRotation();
	In.VoxelSize = UCrowdyVoxelLibrary::GetVoxelSize(this);
	In.Grid = Grid;
	const ULocalPlayer* Player = Controller->GetLocalPlayer();
	const UConstructUIInputSubsystem* Input = Player ? Player->GetSubsystem<UConstructUIInputSubsystem>() : nullptr;
	In.bModalOpen = Input && !Input->CanUseNavigationInput(FName(DockModal));
	In.bTextInputFocused = IsTextInputFocused();
	// Players are listed only inside the grid, so outside one none is gathered.
	if (!Grid.IsSet())
	{
		return Build(In);
	}
	// Each pawn's chunk is tested first, so only the players inside the grid have their id and name read.
	for (TActorIterator<AConstructPresencePawn> It(World); It; ++It)
	{
		if (*It == Self || It->IsHidden())
		{
			continue;
		}
		const FVector Location = It->GetActorLocation();
		FInt64Vector Chunk(0, 0, 0);
		FIntVector InChunk(0, 0, 0);
		UCrowdyVoxelLibrary::WorldToVoxel(this, Location, Chunk.X, Chunk.Y, Chunk.Z, InChunk);
		if (!IsInside(Chunk, Grid.GetValue()))
		{
			continue;
		}
		FConstructStudioObservedActor& Other = In.Others.AddDefaulted_GetRef();
		Other.ActorId = EntityIdOf(**It);
		Other.Label = It->GetPlayerDisplayName();
		Other.Location = Location;
		Other.Chunk = Chunk;
	}
	return Build(In);
}

TSharedRef<FJsonObject> UConstructStudioObservation::Build(const FConstructStudioObservationInput& In)
{
	using namespace ConstructStudioObservationParts;
	const double Cell = In.VoxelSize > 0.0 ? In.VoxelSize : 100.0;
	const FVector Position = In.Location / Cell;
	const FVector Velocity = In.Velocity / Cell;
	const FRotator Look = In.Look.GetNormalized();

	const TSharedRef<FJsonObject> LookOut = MakeShared<FJsonObject>();
	LookOut->SetStringField(TEXT("yaw"), Decimal(Look.Yaw));
	LookOut->SetStringField(TEXT("pitch"), Decimal(Look.Pitch));

	const TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
	Player->SetObjectField(TEXT("position"), Vector(Position));
	Player->SetObjectField(TEXT("velocity"), Vector(Velocity));
	Player->SetObjectField(TEXT("look"), LookOut);
	Player->SetStringField(TEXT("health"), TEXT("1"));
	Player->SetBoolField(TEXT("alive"), true);

	const TSharedRef<FJsonObject> Controlled = MakeShared<FJsonObject>();
	Controlled->SetStringField(TEXT("kind"), TEXT("PLAYER"));
	Controlled->SetObjectField(TEXT("position"), Vector(Position));
	Controlled->SetObjectField(TEXT("velocity"), Vector(Velocity));

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("contractVersion"), TEXT("crowdy.game-observation/1"));
	Out->SetStringField(TEXT("observationId"), In.ObservationId.IsEmpty() ? FString(TEXT("observation")) : In.ObservationId.Left(128));
	Out->SetStringField(TEXT("capabilityRevision"), TEXT("construct-unreal-observe-1"));
	Out->SetStringField(TEXT("controlledEntityId"), In.ControlledEntityId.IsEmpty() ? FString(TEXT("local-player")) : In.ControlledEntityId.Left(128));
	Out->SetStringField(TEXT("observedAt"), In.Now.ToIso8601());
	Out->SetStringField(TEXT("expiresAt"), (In.Now + FTimespan::FromSeconds(LifeSeconds)).ToIso8601());
	Out->SetObjectField(TEXT("player"), Player);
	Out->SetObjectField(TEXT("controlledEntity"), Controlled);

	if (In.Grid.IsSet())
	{
		Out->SetObjectField(TEXT("grid"), GridOf(In.Grid.GetValue()));
	}
	Out->SetArrayField(TEXT("nearbyActors"), NearbyOf(In, Cell));
	Out->SetArrayField(TEXT("nearbyVoxels"), TArray<TSharedPtr<FJsonValue>>());

	const TSharedRef<FJsonObject> InputState = MakeShared<FJsonObject>();
	InputState->SetBoolField(TEXT("modalOpen"), In.bModalOpen);
	InputState->SetBoolField(TEXT("textInputFocused"), In.bTextInputFocused);
	// Never reported, so the agent cannot time a prompt to the player's clicks.
	InputState->SetBoolField(TEXT("humanInputActive"), false);
	Out->SetObjectField(TEXT("inputState"), InputState);
	return Out;
}

FString UConstructStudioObservation::Decimal(double Value)
{
	if (!FMath::IsFinite(Value))
	{
		return TEXT("0");
	}
	FString Text = FString::Printf(TEXT("%.3f"), Value);
	while (Text.EndsWith(TEXT("0")))
	{
		Text.LeftChopInline(1);
	}
	if (Text.EndsWith(TEXT(".")))
	{
		Text.LeftChopInline(1);
	}
	return Text == TEXT("-0") ? FString(TEXT("0")) : Text;
}
