#include "ConstructStudioCaptureTestListener.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioCaptureJob.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"

namespace
{
constexpr EAutomationTestFlags StudioCaptureTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;

enum class EStudioCaptureColour : uint8
{
    Red,
    Green,
    Blue,
    Magenta,
    Cyan,
    Yellow,
};

bool IsStudioCaptureColour(FColor Pixel, EStudioCaptureColour Colour)
{
    const int32 R = Pixel.R;
    const int32 G = Pixel.G;
    const int32 B = Pixel.B;
    switch (Colour)
    {
        case EStudioCaptureColour::Red:
            return R > 80 && R > G * 1.5 && R > B * 1.5;
        case EStudioCaptureColour::Green:
            return G > 80 && G > R * 1.5 && G > B * 1.5;
        case EStudioCaptureColour::Blue:
            return B > 80 && B > R * 1.5 && B > G * 1.5;
        case EStudioCaptureColour::Magenta:
            return R > 80 && B > 80 && R - G > 30 && B - G > 30;
        case EStudioCaptureColour::Cyan:
            return G > 80 && B > 80 && G - R > 30 && B - R > 30;
        case EStudioCaptureColour::Yellow:
            return R > 80 && G > 80 && R > B * 1.5 && G > B * 1.5;
    }
    return false;
}

int32 CountStudioCaptureColour(const FImage& Image, EStudioCaptureColour Colour, FVector4f Bounds)
{
    const int32 X0 = FMath::Clamp(FMath::FloorToInt(Bounds.X * Image.SizeX), 0, Image.SizeX);
    const int32 X1 = FMath::Clamp(FMath::CeilToInt(Bounds.Z * Image.SizeX), 0, Image.SizeX);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Bounds.Y * Image.SizeY), 0, Image.SizeY);
    const int32 Y1 = FMath::Clamp(FMath::CeilToInt(Bounds.W * Image.SizeY), 0, Image.SizeY);
    const TArrayView64<const FColor> Pixels = Image.AsBGRA8();
    int32 Count = 0;
    for (int32 Y = Y0; Y < Y1; ++Y)
    {
        for (int32 X = X0; X < X1; ++X)
        {
            Count += IsStudioCaptureColour(Pixels[Y * Image.SizeX + X], Colour) ? 1 : 0;
        }
    }
    return Count;
}

FImage StudioCaptureBGRA(FImage& Source)
{
    FImage Converted;
    Source.CopyTo(Converted, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    return Converted;
}

void SaveStudioCaptureSyntheticProof(const FImage& Image, const TCHAR* Name)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("StudioPanel"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    TArray64<uint8> Png;
    const FImageView View(Image.AsBGRA8().GetData(), Image.SizeX, Image.SizeY);
    if (FImageUtils::CompressImage(Png, TEXT("png"), View))
    {
        FFileHelper::SaveArrayToFile(Png, *FPaths::Combine(Directory, Name));
    }
}

struct FStudioCapturePrivacyFixture
{
    TStrongObjectPtr<UWorld> World;
    TStrongObjectPtr<UConstructStudioCaptureTestListener> Listener;
    TStrongObjectPtr<UConstructStudioCaptureJob> Job;
    TStrongObjectPtr<UConstructStudioCaptureComponent> BaselineCapture;
    TStrongObjectPtr<UTextureRenderTarget2D> BaselineTarget;
    UStaticMeshComponent* Video = nullptr;
    UStaticMeshComponent* Neutral = nullptr;
    UStaticMeshComponent* Geometry = nullptr;
    UTextRenderComponent* Name = nullptr;
    UConstructStudioCaptureTestWidgetComponent* Widget = nullptr;
    UMaterialInterface* OriginalVideoMaterial = nullptr;
    FMinimalViewInfo View;
    double Started = FPlatformTime::Seconds();
    double LastBaseline = 0.0;
    double LastBaselineProof = 0.0;
    int32 Phase = 0;
    bool bCancelledTest = false;
    int32 BaselineName = 0;
    int32 BaselineVideoMagenta = 0;
    int32 BaselineVideoCyan = 0;
    int32 BaselineWidget = 0;

    ~FStudioCapturePrivacyFixture()
    {
        if (Job.IsValid() && !Job->IsFinished())
        {
            Job->Cancel();
        }
        if (BaselineCapture.IsValid())
        {
            BaselineCapture->DestroyComponent();
        }
        if (World.IsValid())
        {
            World->DestroyWorld(false);
        }
    }

    UMaterialInstanceDynamic* MakeColourMaterial(UObject* Outer, FColor First, FColor Second)
    {
        UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
        if (!Surface)
        {
            return nullptr;
        }
        TArray<FColor> Texels;
        Texels.SetNumUninitialized(64);
        for (int32 Y = 0; Y < 8; ++Y)
        {
            for (int32 X = 0; X < 8; ++X)
            {
                Texels[Y * 8 + X] = ((X / 2 + Y / 2) % 2 == 0) ? First : Second;
            }
        }
        const TConstArrayView64<uint8> Bytes(reinterpret_cast<const uint8*>(Texels.GetData()), Texels.Num() * sizeof(FColor));
        UTexture2D* Texture = UTexture2D::CreateTransient(8, 8, PF_B8G8R8A8, NAME_None, Bytes);
        if (!Texture)
        {
            return nullptr;
        }
        Texture->NeverStream = true;
        Texture->SRGB = true;
        Texture->UpdateResource();
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Surface, Outer);
        Material->SetTextureParameterValue(TEXT("SlateUI"), Texture);
        Material->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor::White);
        return Material;
    }

    UStaticMeshComponent* AddCube(FVector Location, FColor First, FColor Second)
    {
        AActor* Actor = World->SpawnActor<AActor>();
        UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
        Actor->SetRootComponent(Mesh);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetMaterial(0, MakeColourMaterial(Actor, First, Second));
        Mesh->SetWorldLocation(Location);
        Mesh->SetWorldScale3D(FVector(1.0));
        Mesh->RegisterComponentWithWorld(World.Get());
        return Mesh;
    }

    bool Create(FAutomationTestBase& Test)
    {
        if (!FSlateApplication::IsInitialized())
        {
            Test.AddError(TEXT("The rendered privacy test requires Slate and a non-null RHI."));
            return false;
        }
        const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).RequiresHitProxies(false).SetTransactional(false).CreateFXSystem(false);
        World.Reset(UWorld::CreateWorld(EWorldType::GamePreview, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("StudioCapturePrivacy")), GetTransientPackage(), false, GMaxRHIFeatureLevel, &Init));
        if (!Test.TestNotNull(TEXT("A native render world exists"), World.Get()))
        {
            return false;
        }
        Listener.Reset(NewObject<UConstructStudioCaptureTestListener>());
        Listener->UnlitNameMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/UnlitText.UnlitText"));
        if (!Test.TestNotNull(TEXT("The native name has a cooked unlit font material"), Listener->UnlitNameMaterial.Get()))
        {
            return false;
        }
        Geometry = AddCube(FVector(500, -200, -150), FColor::Green, FColor::Green);
        Neutral = AddCube(FVector(500, 200, -150), FColor::Blue, FColor::Blue);
        Video = AddCube(FVector(500, -250, 100), FColor::Magenta, FColor::Cyan);
        Video->ComponentTags.Add(TEXT("Construct.Capture.Exclude"));
        OriginalVideoMaterial = Video->GetMaterial(0);

        AActor* NameActor = World->SpawnActor<AActor>();
        Name = NewObject<UTextRenderComponent>(NameActor);
        NameActor->SetRootComponent(Name);
        Name->SetText(FText::FromString(TEXT("PRIVATE")));
        Name->SetTextMaterial(Listener->UnlitNameMaterial.Get());
        Name->SetTextRenderColor(FColor::Red);
        Name->SetWorldSize(48.0f);
        Name->SetHorizontalAlignment(EHTA_Center);
        Name->SetVerticalAlignment(EVRTA_TextCenter);
        Name->SetWorldLocationAndRotation(FVector(500, 0, 100), FRotator(0, 180, 0));
        Name->RegisterComponentWithWorld(World.Get());

        AActor* WidgetActor = World->SpawnActor<AActor>();
        Widget = NewObject<UConstructStudioCaptureTestWidgetComponent>(WidgetActor);
        WidgetActor->SetRootComponent(Widget);
        Widget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Widget->SetWidgetSpace(EWidgetSpace::World);
        Widget->SetBlendMode(EWidgetBlendMode::Opaque);
        Widget->SetTwoSided(true);
        Widget->SetDrawSize(FVector2D(100, 100));
        Widget->SetBackgroundColor(FLinearColor::Yellow);
        Widget->SetWorldLocationAndRotation(FVector(500, 250, 100), FRotator(0, 180, 0));
        Widget->RegisterComponentWithWorld(World.Get());
        Widget->SetSlateWidget(SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Yellow));

        View.Location = FVector::ZeroVector;
        View.Rotation = FRotator::ZeroRotator;
        View.FOV = 90.0f;
        View.AspectRatio = 16.0f / 9.0f;
        View.PostProcessBlendWeight = 1.0f;
        View.PostProcessSettings.bOverride_AutoExposureMethod = true;
        View.PostProcessSettings.AutoExposureMethod = AEM_Manual;
        View.PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        View.PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
        View.PostProcessSettings.bOverride_BloomIntensity = true;
        View.PostProcessSettings.BloomIntensity = 0;
        View.PostProcessSettings.bOverride_MotionBlurAmount = true;
        View.PostProcessSettings.MotionBlurAmount = 0;
        View.PostProcessSettings.bOverride_VignetteIntensity = true;
        View.PostProcessSettings.VignetteIntensity = 0;

        BaselineTarget.Reset(NewObject<UTextureRenderTarget2D>());
        BaselineTarget->ClearColor = FLinearColor::Black;
        BaselineTarget->InitCustomFormat(640, 360, PF_B8G8R8A8, false);
        BaselineTarget->UpdateResourceImmediate(true);
        BaselineCapture.Reset(NewObject<UConstructStudioCaptureComponent>());
        BaselineCapture->bCaptureEveryFrame = false;
        BaselineCapture->bCaptureOnMovement = false;
        BaselineCapture->TextureTarget = BaselineTarget.Get();
        BaselineCapture->SetCameraView(View);
        BaselineCapture->PostProcessSettings = View.PostProcessSettings;
        BaselineCapture->PostProcessBlendWeight = View.PostProcessBlendWeight;
        BaselineCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        BaselineCapture->bUseCustomProjectionMatrix = true;
        BaselineCapture->CustomProjectionMatrix = View.CalculateProjectionMatrix();
        BaselineCapture->RegisterComponentWithWorld(World.Get());
        World->UpdateWorldComponents(false, false);
        return Test.TestNotNull(TEXT("The video test has a native surface material"), OriginalVideoMaterial);
    }

    bool ReadBaseline()
    {
        Widget->DrawOnce();
        BaselineCapture->CaptureScene();
        FImage Raw;
        if (!FImageUtils::GetRenderTargetImage(BaselineTarget.Get(), Raw))
        {
            return false;
        }
        const FImage Image = StudioCaptureBGRA(Raw);
        if (FPlatformTime::Seconds() - LastBaselineProof > 3.0)
        {
            SaveStudioCaptureSyntheticProof(Image, TEXT("PrivacyBaseline.png"));
            LastBaselineProof = FPlatformTime::Seconds();
        }
        BaselineName = CountStudioCaptureColour(Image, EStudioCaptureColour::Red, FVector4f(0.34f, 0.14f, 0.66f, 0.46f));
        BaselineVideoMagenta = CountStudioCaptureColour(Image, EStudioCaptureColour::Magenta, FVector4f(0.12f, 0.14f, 0.34f, 0.46f));
        BaselineVideoCyan = CountStudioCaptureColour(Image, EStudioCaptureColour::Cyan, FVector4f(0.12f, 0.14f, 0.34f, 0.46f));
        BaselineWidget = CountStudioCaptureColour(Image, EStudioCaptureColour::Yellow, FVector4f(0.66f, 0.14f, 0.88f, 0.46f));
        return BaselineName > 8 && BaselineVideoMagenta > 20 && BaselineVideoCyan > 20 && BaselineWidget > 20;
    }

    void Start(bool bCancel)
    {
        FOnConstructStudioCaptured Answer;
        Answer.BindDynamic(Listener.Get(), &UConstructStudioCaptureTestListener::Captured);
        Job.Reset(UConstructStudioCaptureJob::StartFromView(Listener.Get(), World.Get(), View, View.CalculateProjectionMatrix(), FIntPoint(2560, 1440), nullptr, TEXT("privacy fixture"), Answer));
        if (bCancel)
        {
            Job->Cancel();
        }
        bCancelledTest = bCancel;
        Phase = 1;
    }
};

class FStudioCapturePrivacyCommand final : public IAutomationLatentCommand
{
public:
    FStudioCapturePrivacyCommand(FAutomationTestBase* InTest, TSharedRef<FStudioCapturePrivacyFixture> InFixture, bool bInCancel) : Test(InTest), Fixture(InFixture), bCancel(bInCancel) {}

    virtual bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Fixture->Started > 30.0)
        {
            Test->AddError(FString::Printf(TEXT("Native screenshot timed out; baseline name=%d magenta=%d cyan=%d widget=%d, phase=%d"), Fixture->BaselineName, Fixture->BaselineVideoMagenta, Fixture->BaselineVideoCyan, Fixture->BaselineWidget, Fixture->Phase));
            return true;
        }
        if (Fixture->Phase == 0)
        {
            if (Now - Fixture->LastBaseline < 0.1)
            {
                return false;
            }
            Fixture->LastBaseline = Now;
            if (!Fixture->ReadBaseline())
            {
                return false;
            }
            Test->AddInfo(FString::Printf(TEXT("Privacy stimulus rendered: name=%d, video magenta=%d/cyan=%d, widget=%d"), Fixture->BaselineName, Fixture->BaselineVideoMagenta, Fixture->BaselineVideoCyan, Fixture->BaselineWidget));
            Fixture->Start(bCancel);
            Test->TestEqual(TEXT("Capture does not answer inside the request"), Fixture->Listener->Answers, 0);
            return false;
        }
        if (!Fixture->Job->IsFinished())
        {
            return false;
        }
        if (bCancel)
        {
            Test->TestEqual(TEXT("Cancelled rendering never answers a closed host"), Fixture->Listener->Answers, 0);
            return true;
        }
        Test->TestEqual(TEXT("One request receives exactly one answer"), Fixture->Listener->Answers, 1);
        Test->TestTrue(TEXT("Capture completion runs on the game thread"), Fixture->Listener->bOnGameThread);
        const FConstructStudioCapture& Capture = Fixture->Listener->Capture;
        if (!Test->TestTrue(*FString::Printf(TEXT("Production capture succeeds: %s"), *Capture.Refusal), Capture.bCaptured))
        {
            return true;
        }
        Test->TestEqual(TEXT("Longest side is capped"), Capture.Width, 1280);
        Test->TestEqual(TEXT("Aspect ratio is preserved"), Capture.Height, 720);
        Test->TestEqual(TEXT("The caption is carried unchanged"), Capture.Caption, FString(TEXT("privacy fixture")));
        TArray<uint8> Png;
        FImage Raw;
        if (!Test->TestTrue(TEXT("The real PNG result decodes"), FBase64::Decode(Capture.PngBase64, Png) && FImageUtils::DecompressImage(Png.GetData(), Png.Num(), Raw)))
        {
            return true;
        }
        const FImage Image = StudioCaptureBGRA(Raw);
        SaveStudioCaptureSyntheticProof(Image, TEXT("PrivacyFiltered.png"));
        Test->TestEqual(TEXT("PNG width agrees with its response"), Image.SizeX, Capture.Width);
        Test->TestEqual(TEXT("PNG height agrees with its response"), Image.SizeY, Capture.Height);
        Test->TestEqual(TEXT("Native player name pixels are excluded"), CountStudioCaptureColour(Image, EStudioCaptureColour::Red, FVector4f(0.34f, 0.14f, 0.66f, 0.46f)), 0);
        Test->TestEqual(TEXT("Video magenta pixels are excluded"), CountStudioCaptureColour(Image, EStudioCaptureColour::Magenta, FVector4f(0.12f, 0.14f, 0.34f, 0.46f)), 0);
        Test->TestEqual(TEXT("Video cyan pixels are excluded"), CountStudioCaptureColour(Image, EStudioCaptureColour::Cyan, FVector4f(0.12f, 0.14f, 0.34f, 0.46f)), 0);
        Test->TestEqual(TEXT("World widget pixels are excluded"), CountStudioCaptureColour(Image, EStudioCaptureColour::Yellow, FVector4f(0.66f, 0.14f, 0.88f, 0.46f)), 0);
        Test->TestTrue(TEXT("Ordinary world geometry remains visible"), CountStudioCaptureColour(Image, EStudioCaptureColour::Green, FVector4f(0.16f, 0.6f, 0.43f, 0.98f)) > 100);
        Test->TestTrue(TEXT("An unmarked neutral face remains visible"), CountStudioCaptureColour(Image, EStudioCaptureColour::Blue, FVector4f(0.57f, 0.6f, 0.84f, 0.98f)) > 100);
        Test->TestTrue(TEXT("The video remains visible in gameplay"), Fixture->Video->IsVisible());
        Test->TestTrue(TEXT("The name remains visible in gameplay"), Fixture->Name->IsVisible());
        Test->TestTrue(TEXT("The widget remains visible in gameplay"), Fixture->Widget->IsVisible());
        Test->TestEqual(TEXT("The video material is unchanged"), Fixture->Video->GetMaterial(0), Fixture->OriginalVideoMaterial);
        return true;
    }

private:
    FAutomationTestBase* Test;
    TSharedRef<FStudioCapturePrivacyFixture> Fixture;
    bool bCancel;
};

class FStudioCaptureNoFrameCommand final : public IAutomationLatentCommand
{
public:
    explicit FStudioCaptureNoFrameCommand(FAutomationTestBase* InTest) : Test(InTest)
    {
        Listener.Reset(NewObject<UConstructStudioCaptureTestListener>());
        FOnConstructStudioCaptured Answer;
        Answer.BindDynamic(Listener.Get(), &UConstructStudioCaptureTestListener::Captured);
        Job.Reset(UConstructStudioCaptureJob::Begin(Listener.Get(), nullptr, TEXT("no frame"), Answer));
        Test->TestEqual(TEXT("A missing frame is refused asynchronously"), Listener->Answers, 0);
    }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 5.0)
        {
            Job->Cancel();
            Test->AddError(TEXT("Missing-frame refusal did not complete."));
            return true;
        }
        if (!Job->IsFinished())
        {
            return false;
        }
        Test->TestEqual(TEXT("A missing frame has one refusal answer"), Listener->Answers, 1);
        Test->TestTrue(TEXT("The refusal arrives on the game thread"), Listener->bOnGameThread);
        Test->TestFalse(TEXT("No frame cannot claim capture success"), Listener->Capture.bCaptured);
        Test->TestFalse(TEXT("The refusal explains why"), Listener->Capture.Refusal.IsEmpty());
        Test->TestTrue(TEXT("No image bytes are returned"), Listener->Capture.PngBase64.IsEmpty());
        return true;
    }

private:
    FAutomationTestBase* Test;
    TStrongObjectPtr<UConstructStudioCaptureTestListener> Listener;
    TStrongObjectPtr<UConstructStudioCaptureJob> Job;
    double Started = FPlatformTime::Seconds();
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioCapturePrivacyTest, "Construct.StudioDock.UI.Capture.RenderedPrivacy", StudioCaptureTestFlags)
bool FConstructStudioCapturePrivacyTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FStudioCapturePrivacyFixture> Fixture = MakeShared<FStudioCapturePrivacyFixture>();
    if (Fixture->Create(*this))
    {
        ADD_LATENT_AUTOMATION_COMMAND(FStudioCapturePrivacyCommand(this, Fixture, false));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioCaptureCancellationTest, "Construct.StudioDock.UI.Capture.Cancellation", StudioCaptureTestFlags)
bool FConstructStudioCaptureCancellationTest::RunTest(const FString& Parameters)
{
    const TSharedRef<FStudioCapturePrivacyFixture> Fixture = MakeShared<FStudioCapturePrivacyFixture>();
    if (Fixture->Create(*this))
    {
        ADD_LATENT_AUTOMATION_COMMAND(FStudioCapturePrivacyCommand(this, Fixture, true));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioCaptureNoFrameTest, "Construct.StudioDock.UI.Capture.NoFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FConstructStudioCaptureNoFrameTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FStudioCaptureNoFrameCommand(this));
    return true;
}

#endif
