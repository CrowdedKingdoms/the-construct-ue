#include "ConstructLayer9Check.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using FLayer9Check = UConstructLayer9CheckSubsystem;

	// The same encode the webcam subsystem sends with, and the same decode its feeds read with.
	bool JpegRoundTrip(const TArray<FColor>& Pixels, const int32 Quality, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TArray64<uint8> Jpeg;
		const FImageView Image(Pixels.GetData(), FLayer9Check::FrameWidth, FLayer9Check::FrameHeight);
		if (!Wrapper.CompressImage(Jpeg, EImageFormat::JPEG, Image, Quality))
		{
			return false;
		}
		const TSharedPtr<IImageWrapper> Decoder = Wrapper.CreateImageWrapper(EImageFormat::JPEG);
		TArray64<uint8> Raw;
		if (!Decoder.IsValid() || !Decoder->SetCompressed(Jpeg.GetData(), Jpeg.Num()) || !Decoder->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return false;
		}
		OutSize = FIntPoint(static_cast<int32>(Decoder->GetWidth()), static_cast<int32>(Decoder->GetHeight()));
		OutPixels.SetNumUninitialized(static_cast<int32>(Raw.Num() / sizeof(FColor)));
		FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), OutPixels.Num() * sizeof(FColor));
		return true;
	}

	TArray<FColor> Gradient()
	{
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(FLayer9Check::FrameWidth * FLayer9Check::FrameHeight);
		for (int32 Y = 0; Y < FLayer9Check::FrameHeight; ++Y)
		{
			for (int32 X = 0; X < FLayer9Check::FrameWidth; ++X)
			{
				const uint8 Value = static_cast<uint8>((X * 2 + Y) % 256);
				Pixels[Y * FLayer9Check::FrameWidth + X] = FColor(Value, static_cast<uint8>(255 - Value), static_cast<uint8>(Y * 2));
			}
		}
		return Pixels;
	}

	FInt64Vector ChunkOf(const FVector& Spot, const double ChunkSize)
	{
		return FInt64Vector(FMath::FloorToInt64(Spot.X / ChunkSize), FMath::FloorToInt64(Spot.Y / ChunkSize),
		                    FMath::FloorToInt64(Spot.Z / ChunkSize));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer9SequenceTest, "Construct.Harness.Layer9.SequenceReadsBackExactly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer9SequenceTest::RunTest(const FString& Parameters)
{
	const FIntPoint Size(FLayer9Check::FrameWidth, FLayer9Check::FrameHeight);
	for (const int32 Sequence : {1, 2, 200, 201, 255, 256, 4095, 32768, 65535})
	{
		TArray<FColor> Pixels;
		FLayer9Check::DrawSequence(Sequence, Pixels);
		TestEqual(TEXT("A frame is 128x96"), Pixels.Num(), Size.X * Size.Y);
		TestEqual(FString::Printf(TEXT("Sequence %d reads back unencoded"), Sequence), FLayer9Check::ReadSequence(Pixels, Size), Sequence);
	}
	TArray<FColor> Pixels;
	FLayer9Check::DrawSequence(77, Pixels);
	TestEqual(TEXT("Another size is no pattern"), FLayer9Check::ReadSequence(Pixels, FIntPoint(96, 128)), 0);
	TestEqual(TEXT("A short buffer is no pattern"), FLayer9Check::ReadSequence(MakeArrayView(Pixels.GetData(), 100), Size), 0);

	// One flipped data block breaks the complement check.
	for (int32 Y = 16; Y < 32; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			FColor& Pixel = Pixels[Y * Size.X + X];
			Pixel = Pixel == FColor::White ? FColor::Black : FColor::White;
		}
	}
	TestEqual(TEXT("A frame whose bit and complement disagree is no pattern"), FLayer9Check::ReadSequence(Pixels, Size), 0);

	TArray<FColor> Grey;
	Grey.Init(FColor(128, 128, 128), Size.X * Size.Y);
	TestEqual(TEXT("A grey frame is no pattern"), FLayer9Check::ReadSequence(Grey, Size), 0);
	TestEqual(TEXT("A gradient is no pattern"), FLayer9Check::ReadSequence(Gradient(), Size), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer9JpegTest, "Construct.Harness.Layer9.SequenceSurvivesJpeg",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer9JpegTest::RunTest(const FString& Parameters)
{
	for (const int32 Quality : {30, 50})
	{
		for (const int32 Sequence : {1, 99, 200, 201, 450, 21845, 43690, 65535})
		{
			TArray<FColor> Pixels;
			FLayer9Check::DrawSequence(Sequence, Pixels);
			TArray<FColor> Decoded;
			FIntPoint Size;
			if (!TestTrue(TEXT("The frame encodes and decodes"), JpegRoundTrip(Pixels, Quality, Decoded, Size)))
			{
				return false;
			}
			TestEqual(FString::Printf(TEXT("Sequence %d reads back at quality %d"), Sequence, Quality),
			          FLayer9Check::ReadSequence(Decoded, Size), Sequence);
		}
		TArray<FColor> Decoded;
		FIntPoint Size;
		if (TestTrue(TEXT("A gradient encodes and decodes"), JpegRoundTrip(Gradient(), Quality, Decoded, Size)))
		{
			TestEqual(FString::Printf(TEXT("A gradient reads as no pattern at quality %d"), Quality),
			          FLayer9Check::ReadSequence(Decoded, Size), 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer9LumaTest, "Construct.Harness.Layer9.LumaDeviationSeparatesFlatFromVaried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer9LumaTest::RunTest(const FString& Parameters)
{
	const int32 Count = FLayer9Check::FrameWidth * FLayer9Check::FrameHeight;
	TArray<FColor> Flat;
	Flat.Init(FColor(40, 90, 200), Count);
	TestEqual(TEXT("One colour has no deviation"), FLayer9Check::LumaDeviation(Flat), 0.0, 1.0e-3);
	TestEqual(TEXT("No pixels have no deviation"), FLayer9Check::LumaDeviation(TArray<FColor>()), 0.0);

	// Half black, half white: the luma is 0 or 255, so its deviation is 127.5.
	TArray<FColor> Halves;
	Halves.Init(FColor::Black, Count);
	for (int32 Index = 0; Index < Count / 2; ++Index)
	{
		Halves[Index] = FColor::White;
	}
	TestEqual(TEXT("Half black, half white"), FLayer9Check::LumaDeviation(Halves), 127.5, 1.0e-3);

	// A dark, almost flat picture, as a covered lens gives: luma 10 or 14.
	TArray<FColor> Covered;
	Covered.Init(FColor(10, 10, 10), Count);
	for (int32 Index = 0; Index < Count; Index += 2)
	{
		Covered[Index] = FColor(14, 14, 14);
	}
	TestTrue(TEXT("A covered lens is flat"), FLayer9Check::LumaDeviation(Covered) <= FLayer9Check::LumaVariedThreshold);
	TestTrue(TEXT("A gradient is varied"), FLayer9Check::LumaDeviation(Gradient()) > FLayer9Check::LumaVariedThreshold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructLayer9SpotsTest, "Construct.Harness.Layer9.SpotsStandInTheirChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructLayer9SpotsTest::RunTest(const FString& Parameters)
{
	for (const double ChunkSize : {1000.0, 1600.0, 3200.0})
	{
		const FVector Sender = FLayer9Check::SpotFor(true, false, ChunkSize);
		const FVector Near = FLayer9Check::SpotFor(false, false, ChunkSize);
		const FVector Far = FLayer9Check::SpotFor(false, true, ChunkSize);
		TestEqual(TEXT("A stands at the centre of chunk 6 0 0"), Sender, FVector(6.5, 0.5, 0.5) * ChunkSize);
		TestEqual(TEXT("A never moves"), FLayer9Check::SpotFor(true, true, ChunkSize), Sender);
		TestTrue(TEXT("A is in chunk 6 0 0"), ChunkOf(Sender, ChunkSize) == FInt64Vector(6, 0, 0));
		TestEqual(TEXT("B near is 400 units from A"), FVector::Dist(Sender, Near), 400.0, 1.0e-6);
		TestTrue(TEXT("B near shares A's chunk"), ChunkOf(Near, ChunkSize) == FInt64Vector(6, 0, 0));
		TestTrue(TEXT("B far is in chunk 9 0 0"), ChunkOf(Far, ChunkSize) == FInt64Vector(9, 0, 0));
		TestTrue(TEXT("B far is beyond distance 1"), ChunkOf(Far, ChunkSize).X - ChunkOf(Sender, ChunkSize).X > 1);
	}
	return true;
}

#endif
