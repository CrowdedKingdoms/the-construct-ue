#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructModScriptOverlay.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

enum class EConstructOverlayPrimitive : uint8
{
    Box,
    Sphere,
    Cylinder,
    Capsule,
    Plane
};

// Local presentation data. A later host adapter supplies world-space Unreal units.
struct CONSTRUCTMODSCRIPTS_API FConstructOverlayElement
{
    FString Id;
    EConstructOverlayPrimitive Primitive = EConstructOverlayPrimitive::Box;
    // Scale describes dimensions relative to a 100 uu primitive on each axis.
    FTransform Transform = FTransform::Identity;
    FLinearColor Color = FLinearColor::White;
    float Opacity = 1.0f;
    bool bVisible = true;
    bool bUnlit = false;
};

USTRUCT()
struct FConstructOverlaySource
{
    GENERATED_BODY()

    int64 Generation = 0;
    bool bStopped = false;

    UPROPERTY(Transient)
    TObjectPtr<AActor> Actor;

    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UStaticMeshComponent>> Components;
};

// Owns transient, non-interactive presentation in this client world only.
UCLASS()
class CONSTRUCTMODSCRIPTS_API UConstructModScriptOverlay : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UConstructModScriptOverlay();

    static constexpr int32 MaxSources = 8;
    static constexpr int32 MaxElementsPerSource = 64;

    // Game-thread only. Invalid input or missing assets rejects the entire update.
    // Empty replacement clears presentation but permits another update in the same run.
    bool Replace(const FString& Source, int64 Generation, const TArray<FConstructOverlayElement>& Elements);
    // Stops this generation, including when it has not drawn anything yet.
    bool Clear(const FString& Source, int64 Generation);
    // SDK finalization only: clear this run and release its record once no further host call can follow.
    bool ReleaseFinalizedScript(const FString& Source, int64 Generation);
    // Stops every known generation. Tombstones remain until world deinitialization.
    void ClearAll();
    // Includes retained components whose visibility is false.
    int32 Count(const FString& Source) const;
    // Reads the actual component transform, including normalization of mesh bounds.
    bool FindDrawn(const FString& Source, const FString& Id, FTransform& OutWorldTransform) const;

    virtual void Deinitialize() override;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    friend struct FConstructOverlayTestAccess;

    int32 ActiveSourceCount = 0;
    bool bDeinitialized = false;
    bool bLitMaterialReady = false;
    bool bUnlitMaterialReady = false;

    static void DestroyPresentation(FConstructOverlaySource& State);
    bool Validate(const TArray<FConstructOverlayElement>& Elements) const;
    UStaticMesh* MeshFor(EConstructOverlayPrimitive Primitive) const;

    UPROPERTY(Transient)
    TMap<FString, FConstructOverlaySource> Sources;

    // Hard references ensure the runtime assets are included when this class is cooked.
    UPROPERTY()
    TArray<TObjectPtr<UStaticMesh>> Meshes;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> LitMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> UnlitMaterial;
};
