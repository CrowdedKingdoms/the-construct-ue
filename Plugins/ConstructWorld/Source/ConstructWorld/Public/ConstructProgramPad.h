#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructProgramPad.generated.h"

class ATextRenderActor;
class UBoxComponent;
struct FConstructHubProgram;

UCLASS()
class CONSTRUCTWORLD_API AConstructProgramPad final : public AActor
{
    GENERATED_BODY()

public:
    AConstructProgramPad();

    static int32 ReadProgramId(TConstArrayView<FName> ActorTags);
    void ApplyCatalog(TConstArrayView<FConstructHubProgram> Programs, bool bCatalogAvailable);
    const TSoftObjectPtr<UWorld>& GetDestination() const { return Destination; }

protected:
    virtual void BeginPlay() override;

private:
    friend struct FConstructProgramPadTestAccess;

    UFUNCTION()
    void EnterPad(UPrimitiveComponent* Component, AActor* OtherActor, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    void SetSign(const FText& Title, const FText& Description);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> Trigger;

    UPROPERTY(EditInstanceOnly, Category = "Construct Program")
    TObjectPtr<ATextRenderActor> TitleSign;

    UPROPERTY(EditInstanceOnly, Category = "Construct Program")
    TObjectPtr<ATextRenderActor> DescriptionSign;

    UPROPERTY(EditInstanceOnly, Category = "Construct Program", meta = (ClampMin = "100"))
    float SignWidth = 390.0f;

    UPROPERTY(EditInstanceOnly, Category = "Construct Program")
    bool bReturnToHolodeck = false;

    TSoftObjectPtr<UWorld> Destination;
    bool bTravelling = false;
};
