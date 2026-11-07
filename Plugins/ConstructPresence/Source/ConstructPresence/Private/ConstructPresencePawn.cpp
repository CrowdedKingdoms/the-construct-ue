#include "ConstructPresencePawn.h"

#include "ConstructPresenceSubsystem.h"
#include "ConstructPlayerNameWidget.h"
#include "Subsystem/CrowdyAuthentication.h"
#include "ConstructUIInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Subsystem/CrowdyGameSession.h"
#include "Subsystem/CrowdyPlayers.h"
#include "UObject/ConstructorHelpers.h"

AConstructPresencePawn::AConstructPresencePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	SetReplicateMovement(false);
	OverrideInputComponentClass = UEnhancedInputComponent::StaticClass();
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = true;
	JumpMaxCount = 1;
	JumpMaxHoldTime = 0.0f;
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCapsuleComponent()->InitCapsuleSize(35.0f, 80.0f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->GravityScale = 1.0f;
	Movement->MaxWalkSpeed = 600.0f;
	Movement->JumpZVelocity = 420.0f;
	Movement->bRunPhysicsWithNoController = false;
	Movement->SetAutoActivate(false);
	Movement->PrimaryComponentTick.bStartWithTickEnabled = false;
	Movement->SetMovementMode(MOVE_None);
	Movement->Deactivate();
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetRootComponent());
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
	Camera->bUsePawnControlRotation = true;

	Entity = CreateDefaultSubobject<UCrowdyEntityComponent>(TEXT("Entity"));
	Entity->ReplicateMovement = ECrowdyReplicateMovement::On;
	Entity->IdentityPolicy = ECrowdyIdentityPolicy::PlayerDerived;
	Entity->OwnedBy = ECrowdyOwnedBy::Player;
	Entity->bAutoRegister = true;

	CapsuleBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapsuleBody"));
	CapsuleTop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapsuleTop"));
	CapsuleBottom = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapsuleBottom"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CapsuleMaterial(
	    TEXT("/ConstructPresence/Materials/M_ConstructPresenceCapsule.M_ConstructPresenceCapsule"));
	for (UStaticMeshComponent* Part : {CapsuleBody.Get(), CapsuleTop.Get(), CapsuleBottom.Get()})
	{
		Part->SetupAttachment(GetRootComponent());
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetOwnerNoSee(true);
		if (CapsuleMaterial.Succeeded())
		{
			Part->SetMaterial(0, CapsuleMaterial.Object);
		}
	}

	// Component-held hard references are inherited by native subclasses and retained by the cooker.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	constexpr float Radius = 35.0f;
	constexpr float HalfHeight = 80.0f;
	constexpr float CapOffset = HalfHeight - Radius;
	if (Cylinder.Succeeded())
	{
		CapsuleBody->SetStaticMesh(Cylinder.Object);
		const FVector Extent = Cylinder.Object->GetBounds().BoxExtent;
		CapsuleBody->SetRelativeScale3D(FVector(Radius / FMath::Max(Extent.X, 1.0f),
		                                        Radius / FMath::Max(Extent.Y, 1.0f),
		                                        CapOffset / FMath::Max(Extent.Z, 1.0f)));
	}
	if (Sphere.Succeeded())
	{
		const FVector Extent = Sphere.Object->GetBounds().BoxExtent;
		const FVector Scale(Radius / FMath::Max(Extent.X, 1.0f),
		                    Radius / FMath::Max(Extent.Y, 1.0f),
		                    Radius / FMath::Max(Extent.Z, 1.0f));
		CapsuleTop->SetStaticMesh(Sphere.Object);
		CapsuleBottom->SetStaticMesh(Sphere.Object);
		CapsuleTop->SetRelativeScale3D(Scale);
		CapsuleBottom->SetRelativeScale3D(Scale);
	}
	CapsuleTop->SetRelativeLocation(FVector(0.0f, 0.0f, CapOffset));
	CapsuleBottom->SetRelativeLocation(FVector(0.0f, 0.0f, -CapOffset));

	NameTag = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameTag"));
	NameTag->SetupAttachment(GetRootComponent());
	NameTag->SetRelativeLocation(FVector(0.0f, 0.0f, HalfHeight + 30.0f));
	NameTag->SetHorizontalAlignment(EHTA_Center);
	NameTag->SetVerticalAlignment(EVRTA_TextCenter);
	NameTag->SetWorldSize(24.0f);
	NameTag->SetTextRenderColor(FColor(235, 235, 240));
	NameTag->SetText(FText::GetEmpty());
	NameTag->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NameTag->SetGenerateOverlapEvents(false);
	NameTag->SetCanEverAffectNavigation(false);
}

void AConstructPresencePawn::BeginPlay()
{
	Super::BeginPlay();
	Entity->OnCrowdyOwnershipAssigned.AddUniqueDynamic(this, &ThisClass::HandleOwnershipAssigned);
	RefreshMovementState();
}

void AConstructPresencePawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownPlayerNameWidget();
	RemoveInputMappings();
	ApplyLocalMovementState(false);
	bNameLookupPending = false;
	NameLookupSession.Reset();
	Entity->OnCrowdyOwnershipAssigned.RemoveDynamic(this, &ThisClass::HandleOwnershipAssigned);
	NameCamera.Reset();
	Super::EndPlay(EndPlayReason);
}

void AConstructPresencePawn::UnPossessed()
{
	ShutdownPlayerNameWidget();
	RemoveInputMappings();
	ApplyLocalMovementState(false);
	bNameLookupPending = false;
	NameLookupSession.Reset();
	Super::UnPossessed();
}

void AConstructPresencePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshMovementState();
	EnsurePlayerNameWidget();
}

void AConstructPresencePawn::Restart()
{
	Super::Restart();
	RefreshMovementState();
	EnsurePlayerNameWidget();
}

bool AConstructPresencePawn::ShouldUseLocalCharacterMovement() const
{
	return true;
}

void AConstructPresencePawn::RefreshMovementState()
{
	const APlayerController* LocalController = Cast<APlayerController>(GetController());
	ApplyLocalMovementState(Entity && Entity->IsLocallyOwned() && IsLocallyControlled() && LocalController &&
	                        LocalController->IsLocalController() && ShouldUseLocalCharacterMovement());
}

void AConstructPresencePawn::ApplyLocalMovementState(bool bEnabled)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		bLocalMovementEnabled = false;
		return;
	}
	if (bEnabled)
	{
		if (!bLocalMovementEnabled || !Movement->IsActive() || Movement->MovementMode == MOVE_None)
		{
			Movement->Activate(true);
			Movement->SetComponentTickEnabled(true);
			Movement->SetDefaultMovementMode();
		}
	}
	else
	{
		StopJumping();
		ConsumeMovementInputVector();
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
		Movement->Deactivate();
		Movement->SetComponentTickEnabled(false);
	}
	bLocalMovementEnabled = bEnabled;
}

void AConstructPresencePawn::AddInputMappings()
{
	APlayerController* LocalController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = LocalController ? LocalController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input =
	    LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input || MappingContext)
	{
		return;
	}
	MappingSubsystem = Input;
	InputGate = LocalPlayer->GetSubsystem<UConstructUIInputSubsystem>();
	if (UConstructUIInputSubsystem* Gate = InputGate.Get())
	{
		Gate->OnGameplayInputChanged.AddUniqueDynamic(this, &ThisClass::HandleGameplayInputChanged);
	}
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_ConstructPresence"));
	MoveAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPresenceMove"));
	MoveAction->ValueType = EInputActionValueType::Axis2D;
	MoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
	LookAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPresenceLook"));
	LookAction->ValueType = EInputActionValueType::Axis2D;

	LookRateAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPresenceLookRate"));
	LookRateAction->ValueType = EInputActionValueType::Axis2D;
	LookRateAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
	JumpAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPresenceJump"));
	JumpAction->ValueType = EInputActionValueType::Boolean;
	PlayerNameAction = NewObject<UInputAction>(this, TEXT("IA_ConstructPresencePlayerName"));
	PlayerNameAction->ValueType = EInputActionValueType::Boolean;
	auto MapMove = [this](FKey Key, bool bForward, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(MoveAction, Key);
		if (Key.IsGamepadKey())
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierDeadZone>(MappingContext));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		}
		if (bForward)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Mapping.Modifiers.Add(Swizzle);
		}
	};
	MapMove(EKeys::W, true, false);
	MapMove(EKeys::S, true, true);
	MapMove(EKeys::A, false, true);
	MapMove(EKeys::D, false, false);
	MapMove(EKeys::Up, true, false);
	MapMove(EKeys::Down, true, true);
	MapMove(EKeys::Left, false, true);
	MapMove(EKeys::Right, false, false);
	MapMove(EKeys::Gamepad_LeftX, false, false);
	MapMove(EKeys::Gamepad_LeftY, true, false);
	ConfigureLookMappings(MappingContext, LookAction, LookRateAction);
	MappingContext->MapKey(JumpAction, EKeys::SpaceBar);
	MappingContext->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	MappingContext->MapKey(PlayerNameAction, EKeys::F2);
	MappingContext->MapKey(PlayerNameAction, EKeys::Gamepad_Special_Left);
	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Input->AddMappingContext(MappingContext, 0, Options);
}

void AConstructPresencePawn::ConfigureLookMappings(UInputMappingContext* Context,
                                                   UInputAction* MouseAction,
                                                   UInputAction* GamepadAction)
{
	if (!Context || !MouseAction || !GamepadAction)
	{
		return;
	}

	FEnhancedActionKeyMapping& MouseLook = Context->MapKey(MouseAction, EKeys::Mouse2D);
	UInputModifierScalar* MouseSensitivity = NewObject<UInputModifierScalar>(Context);
	MouseSensitivity->Scalar = FVector(2.5f, -2.5f, 1.0f);
	MouseLook.Modifiers.Add(MouseSensitivity);

	FEnhancedActionKeyMapping& HorizontalLook = Context->MapKey(GamepadAction, EKeys::Gamepad_RightX);
	HorizontalLook.Modifiers.Add(NewObject<UInputModifierDeadZone>(Context));
	FEnhancedActionKeyMapping& VerticalLook = Context->MapKey(GamepadAction, EKeys::Gamepad_RightY);
	VerticalLook.Modifiers.Add(NewObject<UInputModifierDeadZone>(Context));
	UInputModifierSwizzleAxis* LookSwizzle = NewObject<UInputModifierSwizzleAxis>(Context);
	LookSwizzle->Order = EInputAxisSwizzle::YXZ;
	VerticalLook.Modifiers.Add(LookSwizzle);
	UInputModifierNegate* LookPitchDirection = NewObject<UInputModifierNegate>(Context);
	LookPitchDirection->bX = false;
	LookPitchDirection->bY = true;
	VerticalLook.Modifiers.Add(LookPitchDirection);
}
void AConstructPresencePawn::RemoveInputMappings()
{
	if (UConstructUIInputSubsystem* Gate = InputGate.Get())
	{
		Gate->OnGameplayInputChanged.RemoveDynamic(this, &ThisClass::HandleGameplayInputChanged);
	}
	if (UEnhancedInputLocalPlayerSubsystem* Input = MappingSubsystem.Get(); Input && MappingContext)
	{
		Input->RemoveMappingContext(MappingContext);
	}
	MappingContext = nullptr;
	MoveAction = nullptr;
	LookAction = nullptr;
	LookRateAction = nullptr;
	JumpAction = nullptr;
	PlayerNameAction = nullptr;
	MappingSubsystem.Reset();
	InputGate.Reset();
}

void AConstructPresencePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	AddInputMappings();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent); Input && MappingContext)
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);
		Input->BindAction(LookRateAction, ETriggerEvent::Triggered, this, &ThisClass::LookRate);
		Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ThisClass::JumpStarted);
		Input->BindAction(PlayerNameAction, ETriggerEvent::Started, this, &ThisClass::TogglePlayerName);
		Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ThisClass::JumpReleased);
		Input->BindAction(JumpAction, ETriggerEvent::Canceled, this, &ThisClass::JumpReleased);
	}
	RefreshMovementState();
}

bool AConstructPresencePawn::CanUseGameplayInput() const
{
	const APlayerController* LocalController = Cast<APlayerController>(GetController());
	const UConstructUIInputSubsystem* Gate = InputGate.Get();
	return LocalController && LocalController->IsLocalController() && IsLocallyControlled() && Entity &&
	       Entity->IsLocallyOwned() && Gate && Gate->IsGameplayInputAllowed();
}

void AConstructPresencePawn::Move(const FInputActionValue& Value)
{
	if (!bLocalMovementEnabled || !CanUseGameplayInput() || Controller->IsMoveInputIgnored())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FRotationMatrix Rotation(Yaw);
	AddMovementInput(Rotation.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(Rotation.GetUnitAxis(EAxis::Y), Axis.X);
}

void AConstructPresencePawn::Look(const FInputActionValue& Value)
{
	if (!CanUseGameplayInput() || Controller->IsLookInputIgnored())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(ControllerPitchDelta(Axis.Y, 1.0f));
}

void AConstructPresencePawn::LookRate(const FInputActionValue& Value)
{
	if (!CanUseGameplayInput() || Controller->IsLookInputIgnored())
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const float Scale = 112.5f * GetWorld()->GetDeltaSeconds() * CustomTimeDilation;
	AddControllerYawInput(Axis.X * Scale);
	AddControllerPitchInput(ControllerPitchDelta(Axis.Y, Scale));
}

void AConstructPresencePawn::JumpStarted(const FInputActionValue& Value)
{
	if (bLocalMovementEnabled && IsValid(GetCharacterMovement()) && CanUseGameplayInput() &&
	    !Controller->IsMoveInputIgnored())
	{
		Jump();
	}
}

void AConstructPresencePawn::TogglePlayerName(const FInputActionValue& Value)
{
	EnsurePlayerNameWidget();
	if (PlayerNameWidget)
	{
		if (PlayerNameWidget->IsOpen())
		{
			PlayerNameWidget->CloseEditor();
		}
		else
		{
			PlayerNameWidget->OpenEditor();
		}
	}
}

void AConstructPresencePawn::JumpReleased(const FInputActionValue& Value)
{
	StopJumping();
}

void AConstructPresencePawn::HandleGameplayInputChanged(bool bAllowed)
{
	if (!bAllowed)
	{
		StopJumping();
	}
}

void AConstructPresencePawn::HandleOwnershipAssigned(const FGuid& NewOwnerID, ECrowdyRole NewRole, bool bIsLocallyOwned)
{
	RefreshMovementState();
	NameTag->SetVisibility(!bIsLocallyOwned);
	if (!bIsLocallyOwned)
	{
		ShutdownPlayerNameWidget();
		bNameLookupPending = false;
		NameLookupSession.Reset();
		return;
	}
	EnsurePlayerNameWidget();
	if (bLocalEntryStarted)
	{
		return;
	}
	bLocalEntryStarted = true;
	UE_LOG(LogConstructPresence, Log, TEXT("ConstructPresence: local player entered"));

	UGameInstance* Instance = GetGameInstance();
	UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	UCrowdyPlayers* Players = Instance ? Instance->GetSubsystem<UCrowdyPlayers>() : nullptr;
	if (!Session || !Players || Session->GetUserID() <= 0)
	{
		ApplyNameLookupFailure();
		return;
	}
	NameLookupSession = Session;
	NameLookupEntity = Entity->GetNetID();
	NameLookupOwner = NewOwnerID;
	NameLookupUser = Session->GetUserID();
	bNameLookupPending = true;
	FOnPlayerProfileRead Result;
	Result.BindDynamic(this, &ThisClass::HandleNameRead);
	FOnPlayersError Error;
	Error.BindDynamic(this, &ThisClass::HandleNameReadFailed);
	Players->GetPlayerName(NameLookupUser, Result, Error);
}

float AConstructPresencePawn::ControllerPitchDelta(float AxisY, float Scale)
{
	return -AxisY * Scale;
}

bool AConstructPresencePawn::ConsumeNameReply()
{
	const UCrowdyGameSession* Session = NameLookupSession.Get();
	const bool bAccept = bNameLookupPending && !IsActorBeingDestroyed() && Entity && Entity->IsLocallyOwned() &&
	                     Session && Session->GetGameInstance() == GetGameInstance() &&
	                     Session->GetUserID() == NameLookupUser && Entity->GetNetID() == NameLookupEntity &&
	                     Entity->GetOwnerID() == NameLookupOwner;
	bNameLookupPending = false;
	NameLookupSession.Reset();
	return bAccept;
}

void AConstructPresencePawn::HandleNameRead(FCrowdyPlayerProfile Profile)
{
	if (ConsumeNameReply())
	{
		ApplyProfileName(Profile);
	}
}

void AConstructPresencePawn::HandleNameReadFailed(FCrowdyAccountError Error, FString Message)
{
	if (ConsumeNameReply())
	{
		ApplyNameLookupFailure();
	}
}

void AConstructPresencePawn::ApplyProfileName(const FCrowdyPlayerProfile& Profile)
{
	LocalProfile = Profile;
	bLocalProfileReadComplete = true;
	bLocalProfileReadSucceeded = true;
	if (Profile.Gamertag.IsEmpty())
	{
		SetDisplayedPlayerName(TEXT("Player"));
		UE_LOG(LogConstructPresence, Log, TEXT("ConstructPresence: name set to fallback: no gamertag"));
	}
	else
	{
		SetDisplayedPlayerName(Profile.Gamertag);
		UE_LOG(LogConstructPresence, Log, TEXT("ConstructPresence: name set"));
	}
	if (PlayerNameWidget)
	{
		PlayerNameWidget->SetInitialProfile(Profile, true);
	}
}

void AConstructPresencePawn::ApplyNameLookupFailure()
{
	LocalProfile = FCrowdyPlayerProfile();
	bLocalProfileReadComplete = true;
	bLocalProfileReadSucceeded = false;
	SetDisplayedPlayerName(TEXT("Player"));
	UE_LOG(LogConstructPresence, Log, TEXT("ConstructPresence: name set to fallback: lookup failed"));
	if (PlayerNameWidget)
	{
		PlayerNameWidget->SetInitialProfile(LocalProfile, false);
	}
}

void AConstructPresencePawn::EnsurePlayerNameWidget()
{
	APlayerController* LocalController = Cast<APlayerController>(GetController());
	if (PlayerNameWidget || !Entity || !Entity->IsLocallyOwned() || !IsLocallyControlled() || !LocalController ||
	    !LocalController->IsLocalController())
	{
		return;
	}
	PlayerNameWidget =
	    CreateWidget<UConstructPlayerNameWidget>(LocalController, UConstructPlayerNameWidget::StaticClass());
	if (PlayerNameWidget)
	{
		PlayerNameWidget->InitializeForPawn(this);
		PlayerNameWidget->AddToViewport(6);
		if (bLocalProfileReadComplete)
		{
			PlayerNameWidget->SetInitialProfile(LocalProfile, bLocalProfileReadSucceeded);
		}
	}
}

void AConstructPresencePawn::ShutdownPlayerNameWidget()
{
	if (PlayerNameWidget)
	{
		PlayerNameWidget->Shutdown();
		PlayerNameWidget = nullptr;
	}
}

bool AConstructPresencePawn::ApplyConfirmedPlayerProfile(const FCrowdyPlayerProfile& Profile)
{
	UGameInstance* Instance = GetGameInstance();
	const UCrowdyGameSession* Session = Instance ? Instance->GetSubsystem<UCrowdyGameSession>() : nullptr;
	const UCrowdyAuthentication* Auth = Instance ? Instance->GetSubsystem<UCrowdyAuthentication>() : nullptr;
	const APlayerController* LocalController = Cast<APlayerController>(GetController());
	if (IsActorBeingDestroyed() || !Entity || !Entity->IsLocallyOwned() || !IsLocallyControlled() || !LocalController ||
	    !LocalController->IsLocalController() || !Session || !Auth || !Auth->IsSignedIn() ||
	    Session->GetUserID() <= 0 || Profile.UserID != Session->GetUserID())
	{
		return false;
	}
	CommitPlayerProfile(Profile);
	return true;
}

void AConstructPresencePawn::CommitPlayerProfile(const FCrowdyPlayerProfile& Profile)
{
	// A confirmed refresh/save supersedes an older initial lookup still in flight.
	bNameLookupPending = false;
	NameLookupSession.Reset();
	ApplyProfileName(Profile);
}

void AConstructPresencePawn::SetDisplayedPlayerName(const FString& Name)
{
	PlayerName = Name;
	OnRep_PlayerName();
}

void AConstructPresencePawn::OnRep_PlayerName()
{
	int32 DisplayLength = PlayerName.Len();
	for (const TCHAR* BreakMarker : {TEXT("\n"), TEXT("\r"), TEXT("<br>")})
	{
		const int32 BreakAt = PlayerName.Find(BreakMarker, ESearchCase::IgnoreCase);
		if (BreakAt != INDEX_NONE)
		{
			DisplayLength = FMath::Min(DisplayLength, BreakAt);
		}
	}
	const FString DisplayName = PlayerName.Left(FMath::Min(DisplayLength, 64));
	NameTag->SetText(FText::FromString(DisplayName));
}

void AConstructPresencePawn::ResetRemoteName()
{
	ApplyLocalMovementState(false);
	PlayerName.Reset();
	NameTag->SetText(FText::GetEmpty());
	NameTag->SetVisibility(true);
}

void AConstructPresencePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!NameTag->IsVisible() || PlayerName.IsEmpty())
	{
		return;
	}
	if (!NameCamera.IsValid())
	{
		APlayerController* LocalController = GetWorld()->GetFirstPlayerController();
		NameCamera = LocalController ? LocalController->PlayerCameraManager : nullptr;
	}
	if (APlayerCameraManager* ViewCamera = NameCamera.Get())
	{
		const FVector ToCamera = ViewCamera->GetCameraLocation() - NameTag->GetComponentLocation();
		if (!ToCamera.IsNearlyZero())
		{
			NameTag->SetWorldRotation(ToCamera.Rotation());
		}
	}
}
