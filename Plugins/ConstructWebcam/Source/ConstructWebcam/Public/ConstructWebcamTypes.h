#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "ConstructWebcamTypes.generated.h"

UENUM(BlueprintType)
enum class EConstructCameraState : uint8
{
	Unavailable,
	Off,
	Starting,
	On
};

UENUM(BlueprintType)
enum class EConstructCameraBlockedReason : uint8
{
	None,
	WebcamsDisabled UMETA(DisplayName = "Webcams disabled for the project"),
	NoCamera UMETA(DisplayName = "No camera found"),
	NotSignedIn UMETA(DisplayName = "Not signed in"),
	CheckingRecording UMETA(DisplayName = "Checking whether this app records video"),
	SendingOff UMETA(DisplayName = "Video sending is off in your settings"),
	NeedsParentConsent UMETA(DisplayName = "Needs a parent's consent"),
	ConsentUnavailable UMETA(DisplayName = "Parental consent is not available yet"),
	TryAgainLater UMETA(DisplayName = "Try again later"),
	AppDoesNotPermit UMETA(DisplayName = "The app does not permit video"),
	CaptureFailed UMETA(DisplayName = "Capture failed")
};

/** One player's current video: empty means a null texture and an empty sender id. */
USTRUCT(BlueprintType)
struct CONSTRUCTWEBCAM_API FConstructWebcamFeed
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Construct|Webcam")
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Construct|Webcam")
	FString SenderId;

	UPROPERTY(BlueprintReadOnly, Category = "Construct|Webcam")
	int32 Generation = 0;
};
