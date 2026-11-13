#pragma once

#include "CoreMinimal.h"
#include "IMediaTextureSample.h"
#include "MediaSampleQueue.h"

class IImageWrapperModule;
class UTexture2D;
struct FMediaCaptureDeviceInfo;

namespace ConstructWebcam
{
inline constexpr int32 FrameWidth = 128;
inline constexpr int32 FrameHeight = 96;
inline constexpr int32 FramePixels = FrameWidth * FrameHeight;
inline constexpr int64 MaxFrameBytes = 8 * 1024;
inline constexpr int32 StartQuality = 50;
inline constexpr int32 MinQuality = 30;
inline constexpr int32 QualityStep = 5;
inline constexpr int32 SendDistance = 1;
inline constexpr float FrameInterval = 0.1f;
inline constexpr int32 MaxDecodesPerSecond = 10;
inline constexpr double FeedExpirySeconds = 1.0;
inline constexpr float SettingsRefreshSeconds = 10.0f;
}

/** One captured picture as the camera delivered it. Size is the picture; Rows is how many rows the buffer holds. */
struct FConstructWebcamSourceFrame
{
	const uint8* Data = nullptr;
	FIntPoint Size = FIntPoint::ZeroValue;
	int32 Stride = 0;
	int32 Rows = 0;
	EMediaTextureSampleFormat Format = EMediaTextureSampleFormat::Undefined;
};

struct FConstructWebcamEncoded
{
	TArray<uint8> Jpeg;
	int32 NextQuality = ConstructWebcam::StartQuality;
};

/** The camera's sample queue, drained on the game thread. */
struct FConstructWebcamSamples
{
	TSharedRef<FMediaTextureSampleQueue, ESPMode::ThreadSafe> Queue =
	    MakeShared<FMediaTextureSampleQueue, ESPMode::ThreadSafe>();
};

/** The first plain video device, else the first software one; empty when there is neither. */
FString PickWebcamDevice(TConstArrayView<FMediaCaptureDeviceInfo> Devices);

/** Reads a sample's layout; false when it has no CPU buffer. */
bool DescribeWebcamSample(IMediaTextureSample& Sample, FConstructWebcamSourceFrame& OutFrame);

/** Samples Source straight into a 128x96 BGRA frame, centre-cropped to 4:3; YUV is BT.601 limited range. */
bool ConvertWebcamFrame(const FConstructWebcamSourceFrame& Source, TArray<FColor>& OutPixels);

/** Encodes a 128x96 frame. A JPEG over MaxBytes is dropped and the next quality steps down, never below 30. */
FConstructWebcamEncoded EncodeWebcamFrame(IImageWrapperModule& Wrapper,
                                          TConstArrayView<FColor> Pixels,
                                          int32 Quality,
                                          int64 MaxBytes = ConstructWebcam::MaxFrameBytes);

/**
 * Decodes a grey or colour JPEG whose header says at most 128x96 into a 128x96 frame, centred on black, so a
 * feed's texture never changes size. Anything else is refused before a pixel is read. OutSize is the picture's own.
 */
bool DecodeWebcamJpeg(IImageWrapperModule& Wrapper,
                      TConstArrayView<uint8> Bytes,
                      TArray<FColor>& OutPixels,
                      FIntPoint& OutSize);

/** Writes Pixels into Texture when it has that size, else returns a new transient texture holding them. */
UTexture2D* WriteWebcamTexture(UTexture2D* Texture, TConstArrayView<FColor> Pixels, FIntPoint Size);

/** Runs Work on the game thread, at once when already on it. */
void RunOnWebcamGameThread(TUniqueFunction<void()> Work);

inline FInt64Vector WebcamChunkAt(const FVector& Location, double ChunkSize)
{
	return FInt64Vector(static_cast<int64>(FMath::FloorToDouble(Location.X / ChunkSize)),
	                    static_cast<int64>(FMath::FloorToDouble(Location.Y / ChunkSize)),
	                    static_cast<int64>(FMath::FloorToDouble(Location.Z / ChunkSize)));
}

inline bool IsWebcamChunkWithin(const FInt64Vector& A, const FInt64Vector& B, int64 Distance)
{
	return FMath::Abs(A.X - B.X) <= Distance && FMath::Abs(A.Y - B.Y) <= Distance && FMath::Abs(A.Z - B.Z) <= Distance;
}
