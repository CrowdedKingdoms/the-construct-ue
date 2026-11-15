#include "ConstructProgramPad.h"

#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "ConstructHubTypes.h"
#include "ConstructProgramCatalogSubsystem.h"
#include "ConstructProgramLevels.h"
#include "Algo/Find.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/UnrealString.h"

#define LOCTEXT_NAMESPACE "ConstructProgramPad"

AConstructProgramPad::AConstructProgramPad()
{
    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
    SetRootComponent(Trigger);
    Trigger->SetBoxExtent(FVector(132.0, 132.0, 65.0));
    Trigger->SetCollisionObjectType(ECC_WorldDynamic);
    Trigger->SetGenerateOverlapEvents(true);
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AConstructProgramPad::BeginPlay()
{
    Super::BeginPlay();
    if (bReturnToHolodeck)
    {
        Destination = UConstructProgramCatalogSubsystem::GetHolodeck();
    }
    else if (const UConstructProgramCatalogSubsystem* Catalog = GetWorld()->GetSubsystem<UConstructProgramCatalogSubsystem>())
    {
        Catalog->ApplyToPad(*this);
    }
    Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::EnterPad);
}

int32 AConstructProgramPad::ReadProgramId(TConstArrayView<FName> ActorTags)
{
    const FString Prefix = TEXT("Construct.ProgramId.");
    int32 ProgramId = 0;
    for (const FName Tag : ActorTags)
    {
        const FString Name = Tag.ToString();
        if (!Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
        {
            continue;
        }
        const FString Number = Name.Mid(Prefix.Len());
        int32 Parsed = 0;
        if (ProgramId != 0 || !LexTryParseString(Parsed, *Number) || Parsed < 1 || Parsed > 255 || Number != FString::FromInt(Parsed))
        {
            return 0;
        }
        ProgramId = Parsed;
    }
    return ProgramId;
}

void AConstructProgramPad::ApplyCatalog(TConstArrayView<FConstructHubProgram> Programs, bool bCatalogAvailable)
{
    if (bReturnToHolodeck)
    {
        return;
    }

    Destination.Reset();
    const int32 ProgramId = ReadProgramId(Tags);
    if (ProgramId == 0)
    {
        SetSign(LOCTEXT("Reserved", "Reserved"), FText::GetEmpty());
        return;
    }
    if (!bCatalogAvailable)
    {
        SetSign(LOCTEXT("Unavailable", "Unavailable"), LOCTEXT("CatalogUnavailable", "Program catalog unavailable"));
        return;
    }

    const FConstructHubProgram* Match = nullptr;
    for (const FConstructHubProgram& Program : Programs)
    {
        if (Program.ProgramId == ProgramId)
        {
            if (Match)
            {
                SetSign(LOCTEXT("Unavailable", "Unavailable"), LOCTEXT("DuplicateProgram", "Duplicate program in catalog"));
                return;
            }
            Match = &Program;
        }
    }
    if (!Match)
    {
        SetSign(LOCTEXT("Unavailable", "Unavailable"), LOCTEXT("MissingProgram", "Program missing from catalog"));
        return;
    }

    const auto* Level = Algo::FindByPredicate(ConstructProgramLevels::Programs, [Match](const ConstructProgramLevels::FProgramLevel& Candidate)
    {
        return Match->SceneId.Equals(Candidate.SceneId, ESearchCase::CaseSensitive);
    });
    if (Level)
    {
        Destination = TSoftObjectPtr<UWorld>(FSoftObjectPath(Level->Map));
        SetSign(FText::FromString(Match->Name), FText::FromString(Match->Description));
    }
    else
    {
        SetSign(LOCTEXT("Unavailable", "Unavailable"), LOCTEXT("UnknownScene", "Program scene unavailable"));
    }
}

void AConstructProgramPad::SetSign(const FText& Title, const FText& Description)
{
    const auto SetFittedText = [this](ATextRenderActor* Sign, const FText& Text, bool bWrap)
    {
        if (IsValid(Sign))
        {
            UTextRenderComponent* Render = Sign->GetTextRender();
            Render->SetXScale(1.0f);
            Render->SetText(Text);
            if (bWrap && Render->GetTextLocalSize().Y > SignWidth)
            {
                TArray<FString> Words;
                Text.ToString().ParseIntoArrayWS(Words);
                FString Wrapped;
                FString Line;
                for (const FString& Word : Words)
                {
                    const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
                    Render->SetText(FText::FromString(Candidate));
                    if (!Line.IsEmpty() && Render->GetTextLocalSize().Y > SignWidth)
                    {
                        Wrapped += Line + TEXT("\n");
                        Line = Word;
                    }
                    else
                    {
                        Line = Candidate;
                    }
                }
                Render->SetText(FText::FromString(Wrapped + Line));
            }
            const double Width = Render->GetTextLocalSize().Y;
            if (Width > SignWidth)
            {
                Render->SetXScale(static_cast<float>(SignWidth / Width));
            }
        }
    };
    SetFittedText(TitleSign, Title, false);
    SetFittedText(DescriptionSign, Description, true);
}

void AConstructProgramPad::EnterPad(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    const APawn* Pawn = Cast<APawn>(OtherActor);
    const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (bTravelling || Destination.IsNull() || !Controller || !Controller->IsLocalController() || Controller->GetPawn() != Pawn)
    {
        return;
    }

    bTravelling = true;
    UGameplayStatics::OpenLevelBySoftObjectPtr(this, Destination);
}

#undef LOCTEXT_NAMESPACE
