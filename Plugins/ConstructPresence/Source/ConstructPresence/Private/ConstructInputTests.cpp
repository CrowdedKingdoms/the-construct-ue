#include "ConstructPresencePawn.h"
#include "ConstructPreviewPawn.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructUIInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

struct FConstructInputTestAccess
{
	static void Install(APawn& Pawn)
	{
		if (AConstructPresencePawn* Presence = Cast<AConstructPresencePawn>(&Pawn))
		{
			Presence->AddInputMappings();
		}
		else
		{
			CastChecked<AConstructPreviewPawn>(&Pawn)->AddInputMappings();
		}
	}

	static UInputMappingContext* Context(APawn& Pawn)
	{
		if (AConstructPresencePawn* Presence = Cast<AConstructPresencePawn>(&Pawn))
		{
			return Presence->MappingContext.Get();
		}
		return CastChecked<AConstructPreviewPawn>(&Pawn)->MappingContext.Get();
	}

	static void Look(APawn& Pawn, const FInputActionValue& Value, bool bRate)
	{
		if (AConstructPresencePawn* Presence = Cast<AConstructPresencePawn>(&Pawn))
		{
			if (bRate)
			{
				Presence->LookRate(Value);
			}
			else
			{
				Presence->Look(Value);
			}
		}
		else
		{
			AConstructPreviewPawn* Preview = CastChecked<AConstructPreviewPawn>(&Pawn);
			if (bRate)
			{
				Preview->LookRate(Value);
			}
			else
			{
				Preview->Look(Value);
			}
		}
	}
};

namespace ConstructRuntimeInputTests
{
struct FFixture
{
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	APawn* Pawn = nullptr;
	TStrongObjectPtr<ULocalPlayer> Local;

	~FFixture()
	{
		if (Controller)
		{
			Controller->UnPossess();
		}
		if (Local.IsValid())
		{
			Local->PlayerRemoved();
		}
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	}

	bool Initialize(FAutomationTestBase& Test, bool bPresence)
	{
		if (!Test.TestNotNull(TEXT("Runtime input fixture requires Engine"), GEngine))
		{
			return false;
		}
		UWorld::InitializationValues Init;
		Init.InitializeScenes(true)
		    .AllowAudioPlayback(false)
		    .CreatePhysicsScene(false)
		    .CreateNavigation(false)
		    .CreateAISystem(false)
		    .ShouldSimulatePhysics(false);
		World = UWorld::CreateWorld(
		    EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!Test.TestNotNull(TEXT("Input fixture owns an isolated world"), World))
		{
			return false;
		}
		GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->DeltaTimeSeconds = 1.0f / 60.0f;
		Controller = World->SpawnActor<APlayerController>();
		Local.Reset(NewObject<ULocalPlayer>(GEngine));
		Local->PlayerAdded(nullptr, int32(0));
		if (!Test.TestNotNull(TEXT("Input fixture owns a controller"), Controller))
		{
			return false;
		}
		Controller->SetPlayer(Local.Get());
		UClass* PawnClass = bPresence ? AConstructPresencePawn::StaticClass() : AConstructPreviewPawn::StaticClass();
		Pawn = World->SpawnActor<APawn>(PawnClass);
		if (!Test.TestNotNull(TEXT("Input fixture spawns the production pawn"), Pawn))
		{
			return false;
		}
		Controller->Possess(Pawn);
		if (UCrowdyEntityComponent* Entity = Pawn->FindComponentByClass<UCrowdyEntityComponent>())
		{
			Entity->AssignPooledIdentity(FGuid::NewGuid(), FGuid::NewGuid(), 0, ECrowdyRole::Owner, 0);
		}
		FConstructInputTestAccess::Install(*Pawn);
		UConstructUIInputSubsystem* Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
		if (!Test.TestNotNull(TEXT("Input fixture uses the actual shell input gate"), Gate))
		{
			return false;
		}
		Gate->SetSessionReady(true);
		return Test.TestNotNull(TEXT("Production pawn installed its mapping context"),
		                        FConstructInputTestAccess::Context(*Pawn));
	}

	const FEnhancedActionKeyMapping* Mapping(FKey Key) const
	{
		return FConstructInputTestAccess::Context(*Pawn)->GetMappings().FindByPredicate(
		    [Key](const FEnhancedActionKeyMapping& Entry)
		    {
			    return Entry.Key == Key;
		    });
	}

	FInputActionValue Apply(const FEnhancedActionKeyMapping& Mapping, const FVector& Raw) const
	{
		FInputActionValue Value(Mapping.Action->ValueType, Raw);
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
		{
			if (Modifier)
			{
				Value = Modifier->ModifyRaw(nullptr, Value, World->GetDeltaSeconds());
			}
		}
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Action->Modifiers)
		{
			if (Modifier)
			{
				Value = Modifier->ModifyRaw(nullptr, Value, World->GetDeltaSeconds());
			}
		}
		return Value;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructRuntimePawnInputTest,
                                 "Construct.Presence.Input.RuntimePawnMappings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
                                     EAutomationTestFlags::EngineFilter)

bool FConstructRuntimePawnInputTest::RunTest(const FString& Parameters)
{
	for (const bool bPresence : {true, false})
	{
		const FString Label = bPresence ? TEXT("Presence pawn") : TEXT("Shell free-camera pawn");
		ConstructRuntimeInputTests::FFixture Fixture;
		if (!Fixture.Initialize(*this, bPresence))
		{
			return false;
		}
		for (const FKey Key :
		     {EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY, EKeys::Gamepad_RightX, EKeys::Gamepad_RightY})
		{
			const FEnhancedActionKeyMapping* Mapping = Fixture.Mapping(Key);
			if (!TestNotNull(*FString::Printf(TEXT("%s has a stick-axis mapping"), *Label), Mapping))
			{
				return false;
			}
			for (const float Direction : {-1.0f, 1.0f})
			{
				TestFalse(*FString::Printf(TEXT("%s suppresses neutral %s stick noise"), *Label, *Key.ToString()),
				          Fixture.Apply(*Mapping, FVector(Direction * 0.05f, 0.0f, 0.0f)).IsNonZero());
				TestTrue(*FString::Printf(TEXT("%s preserves deliberate %s input"), *Label, *Key.ToString()),
				         Fixture.Apply(*Mapping, FVector(Direction, 0.0f, 0.0f)).IsNonZero());
			}
		}
		const FEnhancedActionKeyMapping* Mouse = Fixture.Mapping(EKeys::Mouse2D);
		const FEnhancedActionKeyMapping* Stick = Fixture.Mapping(EKeys::Gamepad_RightY);
		const FEnhancedActionKeyMapping* Keyboard = Fixture.Mapping(EKeys::W);
		if (!TestNotNull(TEXT("Mouse mapping exists"), Mouse) || !TestNotNull(TEXT("Stick mapping exists"), Stick) ||
		    !TestNotNull(TEXT("Keyboard mapping exists"), Keyboard))
		{
			return false;
		}
		TestTrue(TEXT("Mouse retains small deliberate motion"),
		         Fixture.Apply(*Mouse, FVector(0.0f, 0.05f, 0.0f)).IsNonZero());
		TestTrue(TEXT("Keyboard forward input remains mapped"),
		         Fixture.Apply(*Keyboard, FVector(1.0f, 0.0f, 0.0f)).IsNonZero());
		for (const float Direction : {-1.0f, 1.0f})
		{
			Fixture.Controller->RotationInput = FRotator::ZeroRotator;
			FConstructInputTestAccess::Look(
			    *Fixture.Pawn, Fixture.Apply(*Mouse, FVector(0.0f, Direction, 0.0f)), false);
			const double MousePitch = Fixture.Controller->RotationInput.Pitch;
			Fixture.Controller->RotationInput = FRotator::ZeroRotator;
			FConstructInputTestAccess::Look(*Fixture.Pawn, Fixture.Apply(*Stick, FVector(Direction, 0.0f, 0.0f)), true);
			const double StickPitch = Fixture.Controller->RotationInput.Pitch;
			TestTrue(*FString::Printf(TEXT("%s mouse pitch follows requested direction"), *Label),
			         MousePitch * Direction > 0.0);
			TestTrue(*FString::Printf(TEXT("%s actual gamepad handler agrees with mouse"), *Label),
			         MousePitch * StickPitch > 0.0);
		}
	}
	return true;
}
#endif
