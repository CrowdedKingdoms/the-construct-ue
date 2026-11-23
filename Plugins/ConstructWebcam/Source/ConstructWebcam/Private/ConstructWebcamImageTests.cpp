#include "ConstructWebcamCore.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "IImageWrapperModule.h"
#include "IMediaCaptureSupport.h"
#include "ImageCore.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

namespace ConstructWebcamImageTests
{
constexpr EAutomationTestFlags ImageTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

FConstructWebcamSourceFrame MakeSource(const TArray<uint8>& Buffer,
                                       FIntPoint Size,
                                       int32 Stride,
                                       int32 Rows,
                                       EMediaTextureSampleFormat Format)
{
	FConstructWebcamSourceFrame Source;
	Source.Data = Buffer.GetData();
	Source.Size = Size;
	Source.Stride = Stride;
	Source.Rows = Rows;
	Source.Format = Format;
	return Source;
}

bool AllPixels(const TArray<FColor>& Pixels, const FColor& Expected)
{
	return Pixels.Num() == ConstructWebcam::FramePixels &&
	       !Pixels.ContainsByPredicate([&Expected](const FColor& Pixel) { return Pixel != Expected; });
}

TArray<FColor> FlatFrame(const FColor& Color)
{
	TArray<FColor> Pixels;
	Pixels.Init(Color, ConstructWebcam::FramePixels);
	return Pixels;
}

TArray<uint8> EncodeAt(IImageWrapperModule& Wrapper, int32 Width, int32 Height, const FColor& Color)
{
	TArray<FColor> Pixels;
	Pixels.Init(Color, Width * Height);
	TArray64<uint8> Jpeg;
	Wrapper.CompressImage(Jpeg, EImageFormat::JPEG, FImageView(Pixels.GetData(), Width, Height), 90);
	return TArray<uint8>(Jpeg.GetData(), static_cast<int32>(Jpeg.Num()));
}

int32 FindFrameHeader(const TArray<uint8>& Jpeg)
{
	for (int32 Index = 2; Index + 1 < Jpeg.Num(); ++Index)
	{
		if (Jpeg[Index] == 0xFF && (Jpeg[Index + 1] == 0xC0 || Jpeg[Index + 1] == 0xC2))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}
}

using namespace ConstructWebcamImageTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamConvertNv12Test, "Construct.Webcam.Image.ConvertNV12", ImageTestFlags)
bool FConstructWebcamConvertNv12Test::RunTest(const FString& Parameters)
{
	// 160x120 with a padded stride: Y 81, U 90, V 240 is BT.601 limited-range red.
	const FIntPoint Size(160, 120);
	const int32 Stride = 176;
	const int32 Rows = Size.Y * 3 / 2;
	TArray<uint8> Buffer;
	Buffer.Init(0, Stride * Rows);
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		FMemory::Memset(Buffer.GetData() + Y * Stride, 81, Size.X);
	}
	for (int32 Y = Size.Y; Y < Rows; ++Y)
	{
		for (int32 X = 0; X < Size.X; X += 2)
		{
			Buffer[Y * Stride + X] = 90;
			Buffer[Y * Stride + X + 1] = 240;
		}
	}
	TArray<FColor> Pixels;
	TestTrue(TEXT("NV12 converts"),
	         ConvertWebcamFrame(MakeSource(Buffer, Size, Stride, Rows, EMediaTextureSampleFormat::CharNV12), Pixels));
	TestEqual(TEXT("Output is 128x96"), Pixels.Num(), ConstructWebcam::FramePixels);
	TestTrue(TEXT("Every pixel is red"), AllPixels(Pixels, FColor(255, 0, 0, 255)));

	TArray<uint8> White;
	White.Init(235, 128 * 144);
	FMemory::Memset(White.GetData() + 128 * 96, 128, 128 * 48);
	TestTrue(TEXT("Unscaled NV12 converts"),
	         ConvertWebcamFrame(MakeSource(White, FIntPoint(128, 96), 128, 144, EMediaTextureSampleFormat::CharNV12), Pixels));
	TestTrue(TEXT("Luma 235 with neutral chroma is white"), AllPixels(Pixels, FColor(255, 255, 255, 255)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamConvertYuy2Test, "Construct.Webcam.Image.ConvertYUY2", ImageTestFlags)
bool FConstructWebcamConvertYuy2Test::RunTest(const FString& Parameters)
{
	// Each pair is white then black, so even output columns are white and odd ones black at 1:1.
	const FIntPoint Size(128, 96);
	const int32 Stride = Size.X * 2;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(Stride * Size.Y);
	for (int32 Offset = 0; Offset < Buffer.Num(); Offset += 4)
	{
		Buffer[Offset] = 235;
		Buffer[Offset + 1] = 128;
		Buffer[Offset + 2] = 16;
		Buffer[Offset + 3] = 128;
	}
	TArray<FColor> Pixels;
	TestTrue(TEXT("YUY2 converts"),
	         ConvertWebcamFrame(MakeSource(Buffer, Size, Stride, Size.Y, EMediaTextureSampleFormat::CharYUY2), Pixels));
	TestEqual(TEXT("Output is 128x96"), Pixels.Num(), ConstructWebcam::FramePixels);
	if (Pixels.Num() != ConstructWebcam::FramePixels)
	{
		return true;
	}
	TestEqual(TEXT("Column 0 is the pair's first luma"), Pixels[0], FColor(255, 255, 255, 255));
	TestEqual(TEXT("Column 1 is the pair's second luma"), Pixels[1], FColor(0, 0, 0, 255));
	TestEqual(TEXT("Last row keeps the pattern"), Pixels[95 * 128 + 127], FColor(0, 0, 0, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamConvertBmpTest, "Construct.Webcam.Image.ConvertBMPFlip", ImageTestFlags)
bool FConstructWebcamConvertBmpTest::RunTest(const FString& Parameters)
{
	// The first buffer row is red and the rest blue; a bitmap stores its bottom row first.
	const FIntPoint Size(128, 96);
	const int32 Stride = Size.X * 4;
	TArray<uint8> Buffer;
	Buffer.SetNumZeroed(Stride * Size.Y);
	for (int32 Pixel = 0; Pixel < Size.X * Size.Y; ++Pixel)
	{
		const bool bFirstRow = Pixel < Size.X;
		Buffer[Pixel * 4] = bFirstRow ? 0 : 255;
		Buffer[Pixel * 4 + 2] = bFirstRow ? 255 : 0;
		Buffer[Pixel * 4 + 3] = 255;
	}
	TArray<FColor> Pixels;
	TestTrue(TEXT("BMP converts"),
	         ConvertWebcamFrame(MakeSource(Buffer, Size, Stride, Size.Y, EMediaTextureSampleFormat::CharBMP), Pixels));
	if (!TestEqual(TEXT("Output is 128x96"), Pixels.Num(), ConstructWebcam::FramePixels))
	{
		return true;
	}
	TestEqual(TEXT("BMP's first row lands at the bottom"), Pixels[95 * 128], FColor(255, 0, 0, 255));
	TestEqual(TEXT("BMP's top is the last buffer row"), Pixels[0], FColor(0, 0, 255, 255));

	TestTrue(TEXT("BGRA converts"),
	         ConvertWebcamFrame(MakeSource(Buffer, Size, Stride, Size.Y, EMediaTextureSampleFormat::CharBGRA), Pixels));
	TestEqual(TEXT("BGRA is not flipped"), Pixels[0], FColor(255, 0, 0, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamCropTest, "Construct.Webcam.Image.CropsToFourThree", ImageTestFlags)
bool FConstructWebcamCropTest::RunTest(const FString& Parameters)
{
	// 256x144 crops to the central 192x144, so the 32 red columns on the left never show.
	const FIntPoint Size(256, 144);
	const int32 Stride = Size.X * 4;
	TArray<uint8> Buffer;
	Buffer.SetNumZeroed(Stride * Size.Y);
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			uint8* Pixel = Buffer.GetData() + Y * Stride + X * 4;
			Pixel[X < 32 ? 2 : 1] = 255;
		}
	}
	TArray<FColor> Pixels;
	TestTrue(TEXT("Wide BGRA converts"),
	         ConvertWebcamFrame(MakeSource(Buffer, Size, Stride, Size.Y, EMediaTextureSampleFormat::CharBGRA), Pixels));
	TestTrue(TEXT("Only the 4:3 centre is sampled"), AllPixels(Pixels, FColor(0, 255, 0, 255)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamRejectLayoutTest, "Construct.Webcam.Image.RejectsUnreadableLayouts", ImageTestFlags)
bool FConstructWebcamRejectLayoutTest::RunTest(const FString& Parameters)
{
	TArray<uint8> Buffer;
	Buffer.SetNumZeroed(160 * 180);
	TArray<FColor> Pixels;
	TestFalse(TEXT("NV12 without its chroma rows is refused"),
	          ConvertWebcamFrame(MakeSource(Buffer, FIntPoint(160, 120), 160, 120, EMediaTextureSampleFormat::CharNV12), Pixels));
	TestFalse(TEXT("A stride shorter than a row is refused"),
	          ConvertWebcamFrame(MakeSource(Buffer, FIntPoint(160, 45), 160, 45, EMediaTextureSampleFormat::CharYUY2), Pixels));
	TestFalse(TEXT("Another format is refused"),
	          ConvertWebcamFrame(MakeSource(Buffer, FIntPoint(160, 120), 160, 180, EMediaTextureSampleFormat::CharUYVY), Pixels));
	TestTrue(TEXT("A refusal leaves no pixels"), Pixels.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamEncodeTest, "Construct.Webcam.Image.EncodeStepsDownToFloor", ImageTestFlags)
bool FConstructWebcamEncodeTest::RunTest(const FString& Parameters)
{
	IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TArray<FColor> Gray = FlatFrame(FColor(128, 128, 128, 255));

	const FConstructWebcamEncoded Fits = EncodeWebcamFrame(Wrapper, Gray, 50);
	TestTrue(TEXT("A flat frame fits in 8 KB"), Fits.Jpeg.Num() > 2 && Fits.Jpeg.Num() <= ConstructWebcam::MaxFrameBytes);
	TestTrue(TEXT("It is a JPEG"), Fits.Jpeg.Num() > 2 && Fits.Jpeg[0] == 0xFF && Fits.Jpeg[1] == 0xD8);
	TestEqual(TEXT("A frame that fits keeps the quality"), Fits.NextQuality, 50);

	const FConstructWebcamEncoded Over = EncodeWebcamFrame(Wrapper, Gray, 50, 16);
	TestTrue(TEXT("A frame over the limit is skipped"), Over.Jpeg.IsEmpty());
	TestEqual(TEXT("And the quality steps down by 5"), Over.NextQuality, 45);
	TestEqual(TEXT("Never below 30"), EncodeWebcamFrame(Wrapper, Gray, 32, 16).NextQuality, 30);
	TestEqual(TEXT("At 30 it stays"), EncodeWebcamFrame(Wrapper, Gray, 30, 16).NextQuality, 30);

	TArray<FColor> Short;
	Short.Init(FColor::Black, 10);
	TestTrue(TEXT("A frame of the wrong size encodes nothing"), EncodeWebcamFrame(Wrapper, Short, 50).Jpeg.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamDecodeTest, "Construct.Webcam.Image.DecodeHeaderLimit", ImageTestFlags)
bool FConstructWebcamDecodeTest::RunTest(const FString& Parameters)
{
	IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TArray<FColor> Pixels;
	FIntPoint Size;

	TestTrue(TEXT("A 128x96 JPEG decodes"), DecodeWebcamJpeg(Wrapper, EncodeAt(Wrapper, 128, 96, FColor::Blue), Pixels, Size));
	TestEqual(TEXT("At its own size"), Size, FIntPoint(128, 96));
	TestTrue(TEXT("With its colour"), Pixels.Num() == 128 * 96 && Pixels[0].B > 200 && Pixels[0].R < 60);

	TestTrue(TEXT("A smaller JPEG decodes"), DecodeWebcamJpeg(Wrapper, EncodeAt(Wrapper, 64, 48, FColor::Red), Pixels, Size));
	TestEqual(TEXT("At its own size"), Size, FIntPoint(64, 48));
	if (TestEqual(TEXT("Into the full 128x96 frame"), Pixels.Num(), ConstructWebcam::FramePixels))
	{
		TestEqual(TEXT("Black outside the picture"), Pixels[0], FColor::Black);
		TestTrue(TEXT("The picture is centred"), Pixels[48 * 128 + 64].R > 200 && Pixels[48 * 128 + 64].B < 60);
		TestEqual(TEXT("Black right of the picture"), Pixels[48 * 128 + 100], FColor::Black);
	}

	TestFalse(TEXT("A 160x120 JPEG is refused from its header"),
	          DecodeWebcamJpeg(Wrapper, EncodeAt(Wrapper, 160, 120, FColor::Blue), Pixels, Size));
	TestTrue(TEXT("And yields no pixels"), Pixels.IsEmpty());
	TestFalse(TEXT("A 129x96 JPEG is refused"), DecodeWebcamJpeg(Wrapper, EncodeAt(Wrapper, 129, 96, FColor::Blue), Pixels, Size));

	TArray64<uint8> Png;
	const TArray<FColor> Flat = FlatFrame(FColor::Green);
	Wrapper.CompressImage(Png, EImageFormat::PNG, FImageView(Flat.GetData(), 128, 96));
	TestFalse(TEXT("A PNG is refused"),
	          DecodeWebcamJpeg(Wrapper, TArray<uint8>(Png.GetData(), static_cast<int32>(Png.Num())), Pixels, Size));
	const TArray<uint8> Garbage = {0xFF, 0xD8, 0xFF, 0x00, 0x01, 0x02};
	TestFalse(TEXT("A truncated JPEG is refused"), DecodeWebcamJpeg(Wrapper, Garbage, Pixels, Size));
	TestFalse(TEXT("An empty frame is refused"), DecodeWebcamJpeg(Wrapper, TArray<uint8>(), Pixels, Size));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamComponentsTest, "Construct.Webcam.Image.RefusesFourComponentJpeg", ImageTestFlags)
bool FConstructWebcamComponentsTest::RunTest(const FString& Parameters)
{
	IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TArray<uint8> Jpeg = EncodeAt(Wrapper, 128, 96, FColor::Blue);
	const int32 FrameHeader = FindFrameHeader(Jpeg);
	if (!TestTrue(TEXT("The encoded JPEG has a frame header"), FrameHeader > 0 && FrameHeader + 9 < Jpeg.Num()))
	{
		return true;
	}
	TArray<FColor> Pixels;
	FIntPoint Size;
	TestEqual(TEXT("It names three components"), static_cast<int32>(Jpeg[FrameHeader + 9]), 3);
	TestTrue(TEXT("Three components decode"), DecodeWebcamJpeg(Wrapper, Jpeg, Pixels, Size));

	// Patched to four components, as a CMYK picture declares; refused before the engine's reader sees it.
	Jpeg[FrameHeader + 9] = 4;
	TestFalse(TEXT("Four components are refused"), DecodeWebcamJpeg(Wrapper, Jpeg, Pixels, Size));
	TestTrue(TEXT("With no pixels"), Pixels.IsEmpty());

	TArray<uint8> NoFrame = {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00};
	TestFalse(TEXT("A JPEG with no frame header is refused"), DecodeWebcamJpeg(Wrapper, NoFrame, Pixels, Size));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamDeviceTest, "Construct.Webcam.Image.PicksCameraDevice", ImageTestFlags)
bool FConstructWebcamDeviceTest::RunTest(const FString& Parameters)
{
	TArray<FMediaCaptureDeviceInfo> Devices;
	TestTrue(TEXT("No device, no camera"), PickWebcamDevice(Devices).IsEmpty());
	Devices.Add({FText(), FString(), EMediaCaptureDeviceType::Audio, TEXT("vidcap://audio")});
	Devices.Add({FText(), FString(), EMediaCaptureDeviceType::VideoSoftware, TEXT("vidcap://soft")});
	TestEqual(TEXT("A software device when that is all"), PickWebcamDevice(Devices), FString(TEXT("vidcap://soft")));
	Devices.Add({FText(), FString(), EMediaCaptureDeviceType::Video, TEXT("vidcap://first")});
	Devices.Add({FText(), FString(), EMediaCaptureDeviceType::Video, TEXT("vidcap://second")});
	TestEqual(TEXT("The first plain video device wins"), PickWebcamDevice(Devices), FString(TEXT("vidcap://first")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructWebcamChunkTest, "Construct.Webcam.Image.NearbyChunks", ImageTestFlags)
bool FConstructWebcamChunkTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Floors toward negative"), WebcamChunkAt(FVector(-1.0, 99.0, 100.0), 100.0), FInt64Vector(-1, 0, 1));
	TestTrue(TEXT("Same chunk is near"), IsWebcamChunkWithin(FInt64Vector(6, 0, 0), FInt64Vector(6, 0, 0), 1));
	TestTrue(TEXT("A diagonal neighbour is near"), IsWebcamChunkWithin(FInt64Vector(6, 0, 0), FInt64Vector(7, -1, 1), 1));
	TestFalse(TEXT("Two chunks away is far"), IsWebcamChunkWithin(FInt64Vector(6, 0, 0), FInt64Vector(8, 0, 0), 1));
	TestFalse(TEXT("Three chunks on X is far"), IsWebcamChunkWithin(FInt64Vector(6, 0, 0), FInt64Vector(9, 0, 0), 1));
	return true;
}

#endif
