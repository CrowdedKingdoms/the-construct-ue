#pragma once

#include "CoreMinimal.h"
#include "ConstructModScriptPresentation.h"
#include "Subsystem/CrowdyModScriptHost.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "ConstructModScriptHost.generated.h"

class APlayerController;
class UConstructClaimsSubsystem;
class UConstructModScriptPanel;
class UCrowdyVoxelWorld;
class UCrowdyModScripts;

/** The Construct's game-thread answers and player-facing source binding. */
UCLASS()
class CONSTRUCTMODSCRIPTS_API UConstructModScriptHost : public UTickableWorldSubsystem, public ICrowdyModScriptHost
{
    GENERATED_BODY()

public:
    virtual void OnWorldBeginPlay(UWorld& World) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;

    virtual void DrainClicks_Implementation(FCrowdyModScriptClicks& Clicks) override;
    virtual bool ReadChunk_Implementation(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FCrowdyModScriptChunk& Chunk) override;
    virtual void ListPlayers_Implementation(const FCrowdyModScriptGrid& Box, TArray<FCrowdyModScriptPlayer>& Players) override;
    virtual bool SetVoxel_Implementation(const FCrowdyModScript& Script, const FCrowdyVoxelWrite& Write, FString& Refusal) override;
    virtual void ShowHud_Implementation(const FCrowdyModScript& Script, const FString& Text) override;
    virtual void DrawOverlay_Implementation(const FCrowdyModScript& Script, const TArray<FCrowdyModScriptShape>& Shapes) override;
    virtual void ClearScript_Implementation(const FCrowdyModScript& Script) override;

    TSharedRef<FConstructModScriptPresentation> GetPresentation() const { return Presentation.ToSharedRef(); }

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    friend struct FConstructModScriptHostTestAccess;
    static FConstructScriptLabel Label(const FCrowdyModScript& Script);
    static void BuildChunk(TFunctionRef<FCrowdyVoxel(FIntVector)> Read, FCrowdyModScriptChunk& Out);
    static bool Inside(const FCrowdyModScriptGrid& Grid, const FInt64Vector& Chunk);
    static bool IsCheckMode(const UClass* Class);
    bool ShouldCancelVisit(const FCrowdyModScriptGrid& Grid, const FInt64Vector& Chunk, bool bCurrentLookupSucceeded) const;
    void BindSource(UCrowdyModScripts* NewSource);
    void UnbindSource();
    void SetController(APlayerController* Controller);
    void UpdateVisit();
    void LeaveVisit();
    void Answer(int32 PromptId, bool bRun);
    void Revoke(const FString& ModId);
    void Forget(int64 AuthorId);
    void ShowFeedback(const FText& Message);
    void HideFeedback();
    void EndSession();

    UFUNCTION()
    void SignedOut(bool bSuccess);
    UFUNCTION()
    void AccessEnded(const FString& Reason, const FString& BlockedUntil);

    UFUNCTION()
    void Prompted(const FCrowdyModScriptPrompt& Prompt);
    UFUNCTION()
    void PromptWithdrawn(int32 PromptId);
    UFUNCTION()
    void Started(const FCrowdyModScript& Script);
    UFUNCTION()
    void Stopped(const FCrowdyModScript& Script, ECrowdyModScriptStop Reason);
    UFUNCTION()
    void SourceError(const FCrowdyModScriptError& Error);
    UFUNCTION()
    void EditDone(int64 EditId, const FCrowdyVoxelWrite& Write, ECrowdyVoxelResult Result, const FString& Message);

    UPROPERTY(Transient)
    TObjectPtr<UCrowdyModScripts> Source;
    UPROPERTY(Transient)
    TObjectPtr<UConstructModScriptPanel> Panel;
    TWeakObjectPtr<UCrowdyVoxelWorld> VoxelWorld;
    TWeakObjectPtr<UConstructClaimsSubsystem> Claims;
    TWeakObjectPtr<APlayerController> PlayerController;
    TSharedPtr<FConstructModScriptPresentation> Presentation = MakeShared<FConstructModScriptPresentation>();
    TMap<int64, FConstructScriptLabel> SubmittedEdits;
    FTimerHandle FeedbackTimer;
    FInt64Vector VisitChunk = FInt64Vector::ZeroValue;
    uint64 AppliedLookup = 0;
    int64 SessionUser = 0;
    double NextLookup = 0.0;
    bool bHasChunk = false;
    bool bEnabledForWorld = false;
    bool bEnded = false;
};
