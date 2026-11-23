#include "ConstructWebcamCore.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "IMediaCaptureSupport.h"
#include "ImageCore.h"
#include "RHITypes.h"

namespace
{
uint8 ClampWebcamByte(int32 Value)
{
	return static_cast<uint8>(FMath::Clamp(Value, 0, 255));
}

FColor WebcamYuvToColor(int32 Y, int32 U, int32 V)
{
	const int32 Luma = 298 * (Y - 16) + 128;
	const int32 Blue = U - 128;
	const int32 Red = V - 128;
	return FColor(ClampWebcamByte((Luma + 409 * Red) >> 8),
	              ClampWebcamByte((Luma - 100 * Blue - 208 * Red) >> 8),
	              ClampWebcamByte((Luma + 516 * Blue) >> 8),
	              255);
}

bool IsWebcamLayoutReadable(const FConstructWebcamSourceFrame& Source)
{
	const int64 Width = Source.Size.X;
	const int64 Height = Source.Size.Y;
	if (!Source.Data || Width < 4 || Height < 3)
	{
		return false;
	}
	switch (Source.Format)
	{
	case EMediaTextureSampleFormat::CharNV12:
		return Width % 2 == 0 && Height % 2 == 0 && Source.Stride >= Width && Source.Rows >= Height * 3 / 2;
	case EMediaTextureSampleFormat::CharYUY2:
		return Width % 2 == 0 && Source.Stride >= Width * 2 && Source.Rows >= Height;
	case EMediaTextureSampleFormat::CharBGRA:
	case EMediaTextureSampleFormat::CharBMP:
		return Source.Stride >= Width * 4 && Source.Rows >= Height;
	default:
		return false;
	}
}

bool IsJpegFrameHeaderMarker(uint8 Marker)
{
	return Marker >= 0xC0 && Marker <= 0xCF && Marker != 0xC4 && Marker != 0xC8 && Marker != 0xCC;
}

int32 FindWebcamJpegComponents(TConstArrayView<uint8> Bytes)
{
	int64 Offset = 2;
	while (Offset + 4 <= Bytes.Num())
	{
		if (Bytes[Offset] != 0xFF)
		{
			return 0;
		}
		const uint8 Marker = Bytes[Offset + 1];
		if (Marker == 0xFF)
		{
			++Offset;
			continue;
		}
		if (Marker == 0xDA || Marker == 0xD9)
		{
			return 0;
		}
		const int64 Length = (static_cast<int64>(Bytes[Offset + 2]) << 8) | Bytes[Offset + 3];
		if (IsJpegFrameHeaderMarker(Marker))
		{
			return Length >= 8 && Offset + 9 < Bytes.Num() ? Bytes[Offset + 9] : 0;
		}
		Offset += 2 + Length;
	}
	return 0;
}

template <typename FReadPixel>
void SampleWebcamFrame(const FConstructWebcamSourceFrame& Source, TArray<FColor>& OutPixels, FReadPixel ReadPixel)
{
	int64 CropWidth = Source.Size.X;
	int64 CropHeight = Source.Size.Y;
	if (CropWidth * 3 > CropHeight * 4)
	{
		CropWidth = CropHeight * 4 / 3;
	}
	else
	{
		CropHeight = CropWidth * 3 / 4;
	}
	const int64 Left = (Source.Size.X - CropWidth) / 2;
	const int64 Top = (Source.Size.Y - CropHeight) / 2;

	int32 Columns[ConstructWebcam::FrameWidth];
	for (int32 X = 0; X < ConstructWebcam::FrameWidth; ++X)
	{
		Columns[X] = static_cast<int32>(Left + ((2 * X + 1) * CropWidth) / (2 * ConstructWebcam::FrameWidth));
	}

	OutPixels.SetNumUninitialized(ConstructWebcam::FramePixels);
	FColor* Dest = OutPixels.GetData();
	for (int32 Y = 0; Y < ConstructWebcam::FrameHeight; ++Y)
	{
		const int32 Row = static_cast<int32>(Top + ((2 * Y + 1) * CropHeight) / (2 * ConstructWebcam::FrameHeight));
		for (int32 X = 0; X < ConstructWebcam::FrameWidth; ++X)
		{
			*Dest++ = ReadPixel(Columns[X], Row);
		}
	}
}
}

FString PickWebcamDevice(TConstArrayView<FMediaCaptureDeviceInfo> Devices)
{
	const FMediaCaptureDeviceInfo* Software = nullptr;
	for (const FMediaCaptureDeviceInfo& Device : Devices)
	{
		if (Device.Type == EMediaCaptureDeviceType::Video)
		{
			return Device.Url;
		}
		if (!Software && Device.Type == EMediaCaptureDeviceType::VideoSoftware)
		{
			Software = &Device;
		}
	}
	return Software ? Software->Url : FString();
}

bool DescribeWebcamSample(IMediaTextureSample& Sample, FConstructWebcamSourceFrame& OutFrame)
{
	const FIntPoint Dim = Sample.GetDim();
	OutFrame.Data = static_cast<const uint8*>(Sample.GetBuffer());
	OutFrame.Format = Sample.GetFormat();
	OutFrame.Stride = static_cast<int32>(Sample.GetStride());
	OutFrame.Rows = Dim.Y;
	OutFrame.Size = Sample.GetOutputDim();
	if (OutFrame.Size.X <= 0 || OutFrame.Size.Y <= 0)
	{
		// A YUY2 texel holds two pixels and an NV12 buffer carries its chroma rows below the picture.
		const bool bYuy2 = OutFrame.Format == EMediaTextureSampleFormat::CharYUY2;
		const bool bNv12 = OutFrame.Format == EMediaTextureSampleFormat::CharNV12;
		OutFrame.Size = bYuy2 ? FIntPoint(Dim.X * 2, Dim.Y) : bNv12 ? FIntPoint(Dim.X, Dim.Y * 2 / 3) : Dim;
	}
	return OutFrame.Data != nullptr;
}

bool ConvertWebcamFrame(const FConstructWebcamSourceFrame& Source, TArray<FColor>& OutPixels)
{
	OutPixels.Reset();
	if (!IsWebcamLayoutReadable(Source))
	{
		return false;
	}
	const uint8* Data = Source.Data;
	const int64 Stride = Source.Stride;
	switch (Source.Format)
	{
	case EMediaTextureSampleFormat::CharNV12:
	{
		const uint8* Chroma = Data + Stride * Source.Size.Y;
		SampleWebcamFrame(Source, OutPixels, [Data, Chroma, Stride](int32 X, int32 Y)
		{
			const uint8* UV = Chroma + Stride * (Y / 2) + (X & ~1);
			return WebcamYuvToColor(Data[Stride * Y + X], UV[0], UV[1]);
		});
		return true;
	}
	case EMediaTextureSampleFormat::CharYUY2:
		SampleWebcamFrame(Source, OutPixels, [Data, Stride](int32 X, int32 Y)
		{
			const uint8* Pair = Data + Stride * Y + (X / 2) * 4;
			return WebcamYuvToColor(Pair[(X & 1) * 2], Pair[1], Pair[3]);
		});
		return true;
	case EMediaTextureSampleFormat::CharBMP:
	{
		const int64 LastRow = Source.Size.Y - 1;
		SampleWebcamFrame(Source, OutPixels, [Data, Stride, LastRow](int32 X, int32 Y)
		{
			const uint8* Pixel = Data + Stride * (LastRow - Y) + X * 4;
			return FColor(Pixel[2], Pixel[1], Pixel[0], 255);
		});
		return true;
	}
	default:
		SampleWebcamFrame(Source, OutPixels, [Data, Stride](int32 X, int32 Y)
		{
			const uint8* Pixel = Data + Stride * Y + X * 4;
			return FColor(Pixel[2], Pixel[1], Pixel[0], 255);
		});
		return true;
	}
}

FConstructWebcamEncoded EncodeWebcamFrame(IImageWrapperModule& Wrapper,
                                          TConstArrayView<FColor> Pixels,
                                          int32 Quality,
                                          int64 MaxBytes)
{
	FConstructWebcamEncoded Result;
	Result.NextQuality = Quality;
	if (Pixels.Num() != ConstructWebcam::FramePixels)
	{
		return Result;
	}
	TArray64<uint8> Jpeg;
	const FImageView Image(Pixels.GetData(), ConstructWebcam::FrameWidth, ConstructWebcam::FrameHeight);
	if (!Wrapper.CompressImage(Jpeg, EImageFormat::JPEG, Image, Quality))
	{
		return Result;
	}
	if (Jpeg.Num() > MaxBytes)
	{
		Result.NextQuality = FMath::Max(ConstructWebcam::MinQuality, Quality - ConstructWebcam::QualityStep);
		return Result;
	}
	Result.Jpeg.Append(Jpeg.GetData(), static_cast<int32>(Jpeg.Num()));
	return Result;
}

bool DecodeWebcamJpeg(IImageWrapperModule& Wrapper,
                      TConstArrayView<uint8> Bytes,
                      TArray<FColor>& OutPixels,
                      FIntPoint& OutSize)
{
	OutPixels.Reset();
	OutSize = FIntPoint::ZeroValue;
	if (Bytes.IsEmpty() || Wrapper.DetectImageFormat(Bytes.GetData(), Bytes.Num()) != EImageFormat::JPEG)
	{
		return false;
	}
	// The engine's reader logs an error for every CMYK or YCCK picture, so those are refused from the frame header.
	const int32 Components = FindWebcamJpegComponents(Bytes);
	if (Components != 1 && Components != 3)
	{
		return false;
	}
	const TSharedPtr<IImageWrapper> Image = Wrapper.CreateImageWrapper(EImageFormat::JPEG);
	if (!Image.IsValid() || !Image->SetCompressed(Bytes.GetData(), Bytes.Num()))
	{
		return false;
	}
	const int64 Width = Image->GetWidth();
	const int64 Height = Image->GetHeight();
	if (Width <= 0 || Height <= 0 || Width > ConstructWebcam::FrameWidth || Height > ConstructWebcam::FrameHeight)
	{
		return false;
	}
	TArray64<uint8> Raw;
	if (!Image->GetRaw(ERGBFormat::BGRA, 8, Raw) || Raw.Num() != Width * Height * 4)
	{
		return false;
	}
	OutPixels.Init(FColor::Black, ConstructWebcam::FramePixels);
	const int64 Left = (ConstructWebcam::FrameWidth - Width) / 2;
	const int64 Top = (ConstructWebcam::FrameHeight - Height) / 2;
	for (int64 Row = 0; Row < Height; ++Row)
	{
		FMemory::Memcpy(OutPixels.GetData() + (Top + Row) * ConstructWebcam::FrameWidth + Left,
		                Raw.GetData() + Row * Width * 4, Width * 4);
	}
	OutSize = FIntPoint(static_cast<int32>(Width), static_cast<int32>(Height));
	return true;
}

UTexture2D* WriteWebcamTexture(UTexture2D* Texture, TConstArrayView<FColor> Pixels, FIntPoint Size)
{
	if (Size.X <= 0 || Size.Y <= 0 || Pixels.Num() != Size.X * Size.Y)
	{
		return Texture;
	}
	const int64 Bytes = static_cast<int64>(Pixels.Num()) * sizeof(FColor);
	const bool bReusable = Texture && Texture->GetSizeX() == Size.X && Texture->GetSizeY() == Size.Y;
	if (!bReusable)
	{
		const TConstArrayView64<uint8> Data(reinterpret_cast<const uint8*>(Pixels.GetData()), Bytes);
		return UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8, NAME_None, Data);
	}
	// Without a renderer (a headless run) there is no resource to upload into; the texture is still the feed's.
	if (!Texture->GetResource())
	{
		return Texture;
	}
	uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes));
	FMemory::Memcpy(Copy, Pixels.GetData(), Bytes);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y);
	Texture->UpdateTextureRegions(0, 1, Region, Size.X * sizeof(FColor), sizeof(FColor), Copy,
	                              [](uint8* Data, const FUpdateTextureRegion2D* Regions)
	                              {
		                              FMemory::Free(Data);
		                              delete Regions;
	                              });
	return Texture;
}

void RunOnWebcamGameThread(TUniqueFunction<void()> Work)
{
	if (IsInGameThread())
	{
		Work();
		return;
	}
	AsyncTask(ENamedThreads::GameThread, MoveTemp(Work));
}
