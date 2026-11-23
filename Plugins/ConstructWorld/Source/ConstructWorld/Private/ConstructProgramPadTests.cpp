#include "ConstructProgramPad.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/TextRenderComponent.h"
#include "ConstructHubTypes.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Level.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescUtils.h"
#endif

struct FConstructProgramPadTestAccess
{
    static void SetWidth(AConstructProgramPad& Pad, float Width) { Pad.SignWidth = Width; }
    static void SetSigns(AConstructProgramPad& Pad, ATextRenderActor* Title, ATextRenderActor* Description)
    {
        Pad.TitleSign = Title;
        Pad.DescriptionSign = Description;
    }
};

namespace
{
    struct FPadFixture
    {
        FPadFixture()
        {
            Pad = World->SpawnActor<AConstructProgramPad>();
            Title = World->SpawnActor<ATextRenderActor>();
            Description = World->SpawnActor<ATextRenderActor>();
            FConstructProgramPadTestAccess::SetSigns(*Pad, Title, Description);
            Pad->Tags.Add(TEXT("Construct.ProgramId.1"));
            Paint.ProgramId = 1;
            Paint.SceneId = TEXT("paint");
            Paint.Name = TEXT("Catalog paint name");
            Paint.Description = TEXT("Catalog paint description");
        }

        ~FPadFixture()
        {
            World->DestroyWorld(false);
        }

        bool CheckUnavailable(FAutomationTestBase& Test, TConstArrayView<FConstructHubProgram> Rows, bool bCatalogAvailable, const TCHAR* Reason) const
        {
            Pad->ApplyCatalog(MakeArrayView(&Paint, 1), true);
            Test.TestFalse(TEXT("Fixture first has an available destination"), Pad->GetDestination().IsNull());
            Pad->ApplyCatalog(Rows, bCatalogAvailable);
            Test.TestTrue(TEXT("The pad cannot travel"), Pad->GetDestination().IsNull());
            Test.TestEqual(TEXT("Unavailable title"), Title->GetTextRender()->Text.ToString(), FString(TEXT("Unavailable")));
            Test.TestEqual(TEXT("The sign gives the reason"), Description->GetTextRender()->Text.ToString(), FString(Reason));
            return true;
        }

        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
        AConstructProgramPad* Pad;
        ATextRenderActor* Title;
        ATextRenderActor* Description;
        FConstructHubProgram Paint;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPadMatch, "Construct.Holodeck.Pad.CatalogMatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructPadMatch::RunTest(const FString&)
{
    const FPadFixture Fixture;
    FConstructHubProgram Other = Fixture.Paint;
    Other.ProgramId = 2;
    Other.Name = TEXT("Another program");
    const FConstructHubProgram Rows[] = {Other, Fixture.Paint};
    Fixture.Pad->ApplyCatalog(Rows, true);
    TestEqual(TEXT("Only the trusted Paint level is selected"), Fixture.Pad->GetDestination().ToSoftObjectPath().GetLongPackageName(),
        FString(TEXT("/Game/Construct/Maps/L_ConstructPaint")));
    TestEqual(TEXT("Catalog name is shown"), Fixture.Title->GetTextRender()->Text.ToString(), Fixture.Paint.Name);
    TestEqual(TEXT("Catalog description is shown"), Fixture.Description->GetTextRender()->Text.ToString(), Fixture.Paint.Description);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPadReadableDescription, "Construct.Holodeck.Pad.ReadableDescription", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructPadReadableDescription::RunTest(const FString&)
{
    FPadFixture F;
    FConstructProgramPadTestAccess::SetWidth(*F.Pad, 880);
    F.Description->GetTextRender()->SetWorldSize(80);
    F.Paint.Description = TEXT("A shared 3D canvas. Place and erase voxel blocks together.");
    F.Pad->ApplyCatalog(MakeArrayView(&F.Paint, 1), true);
    const UTextRenderComponent* Render = F.Description->GetTextRender();
    TestTrue(TEXT("The real catalog description wraps into readable lines"), Render->Text.ToString().Contains(TEXT("\n")));
    TestEqual(TEXT("Wrapping preserves the complete catalog description"), Render->Text.ToString().Replace(TEXT("\n"), TEXT(" ")), F.Paint.Description);
    TestTrue(TEXT("The description fits its enlarged board without squashing letters"), Render->GetTextLocalSize().Y <= 880.01 && Render->XScale == 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPadMissing, "Construct.Holodeck.Pad.MissingRow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructPadMissing::RunTest(const FString&)
{
    const FPadFixture Fixture;
    FConstructHubProgram Other = Fixture.Paint;
    Other.ProgramId = 2;
    return Fixture.CheckUnavailable(*this, MakeArrayView(&Other, 1), true, TEXT("Program missing from catalog"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPadDuplicate, "Construct.Holodeck.Pad.DuplicateRows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructPadDuplicate::RunTest(const FString&)
{
    const FPadFixture Fixture;
    const FConstructHubProgram Rows[] = {Fixture.Paint, Fixture.Paint};
    return Fixture.CheckUnavailable(*this, Rows, true, TEXT("Duplicate program in catalog"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPadUnknown, "Construct.Holodeck.Pad.UnknownScene", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructPadUnknown::RunTest(const FString&)
{
    const FPadFixture Fixture;
    FConstructHubProgram Unknown = Fixture.Paint;
    Unknown.SceneId = TEXT("/Game/Construct/Maps/L_ConstructHolodeck");
    return Fixture.CheckUnavailable(*this, MakeArrayView(&Unknown, 1), true, TEXT("Program scene unavailable"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructPadError, "Construct.Holodeck.Pad.CatalogError", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructPadError::RunTest(const FString&)
{
    const FPadFixture Fixture;
    return Fixture.CheckUnavailable(*this, MakeArrayView(&Fixture.Paint, 1), false, TEXT("Program catalog unavailable"));
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructReservedBays, "Construct.Holodeck.Pad.ReservedBays", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructReservedBays::RunTest(const FString&)
{
    const FString ActorPath = ULevel::GetExternalActorsPath(TEXT("/Game/Construct/Maps/L_ConstructHolodeck"));
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({ActorPath}, true);
    TArray<FAssetData> Actors;
    Registry.GetAssetsByPath(FName(*ActorPath), Actors, true);

    int32 Reserved = 0;
    int32 Tagged = 0;
    for (const FAssetData& Asset : Actors)
    {
        const TUniquePtr<FWorldPartitionActorDesc> Desc = FWorldPartitionActorDescUtils::GetActorDescriptorFromAssetData(Asset);
        if (!Desc)
        {
            continue;
        }

        const FName Label = Desc->GetActorLabel();
        const bool bReserved = Label == TEXT("Bay A pad") || Label == TEXT("Bay C pad");
        if (bReserved)
        {
            ++Reserved;
            TestEqual(TEXT("A reserved bay has no program ID"), AConstructProgramPad::ReadProgramId(Desc->GetTags()), 0);
        }
        for (const FName Tag : Desc->GetTags())
        {
            if (Tag.ToString().StartsWith(TEXT("Construct.ProgramId.")))
            {
                ++Tagged;
                TestFalse(TEXT("A reserved bay carries no program tag"), bReserved);
                TestEqual(TEXT("Only the Paint pad carries a program tag"), Label, FName(TEXT("Paint travel pad")));
                TestEqual(TEXT("Paint uses catalog ID 1"), Tag, FName(TEXT("Construct.ProgramId.1")));
            }
        }
    }
    TestEqual(TEXT("Both authored reserved bays were checked"), Reserved, 2);
    TestEqual(TEXT("Exactly one authored pad has a program ID"), Tagged, 1);
    return true;
}
#endif

#endif
