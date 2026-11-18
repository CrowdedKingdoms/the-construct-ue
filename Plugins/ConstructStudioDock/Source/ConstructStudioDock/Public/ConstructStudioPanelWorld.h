#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructStudioPanelWorld.generated.h"

class UConstructStudioPanel;
class ULocalPlayer;
class APlayerController;

USTRUCT()
struct FConstructStudioPlayerPanel
{
    GENERATED_BODY()

    TWeakObjectPtr<ULocalPlayer> Player;
    TWeakObjectPtr<APlayerController> Controller;
    UPROPERTY(Transient)
    TObjectPtr<UConstructStudioPanel> Panel;
};

/** Installs one local frame per player; the core still enforces one open dock per process. */
UCLASS()
class CONSTRUCTSTUDIODOCK_API UConstructStudioPanelWorld : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;
    UConstructStudioPanel* GetPanel(ULocalPlayer* Player) const;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    void ClearPanels();
    UPROPERTY(Transient)
    TArray<FConstructStudioPlayerPanel> Players;
    bool bEnabled = false;
    bool bEnded = false;
    float RefreshIn = 0.0f;
};

