#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystem/CrowdyMods.h"
#include "ConstructModPanel.generated.h"

class AConstructModChunkAnchor;
class AConstructChunkView;
class UConstructModMenu;

UENUM(BlueprintType)
enum class EConstructModPanelState : uint8
{
    Loading,
    NoMod,
    Building,
    BuildFailed,
    Built,
    Off,
    On,
    Blocked,
    Error
};

USTRUCT(BlueprintType)
struct CONSTRUCTWORLD_API FConstructModFileHash
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Construct|Mods")
    FString Path;
    UPROPERTY(BlueprintReadOnly, Category = "Construct|Mods")
    FString Sha1;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FConstructModBuildProgress, ECrowdyModBuildStatus, Status);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FConstructModBuildDone, ECrowdyModResult, Result, ECrowdyModBuildStatus, Status, const FString&, BuildId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FConstructModDeployed, ECrowdyModResult, Result, int64, Version, bool, bEnabled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FConstructModEnabled, ECrowdyModResult, Result, bool, bEnabled, bool, bRunning);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FConstructModLogShown, ECrowdyModResult, Result, int32, LineCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FConstructModDeleted, ECrowdyModResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FConstructModStateChanged, EConstructModPanelState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FConstructModWakeDone, ECrowdyModResult, Result, const FString&, Message);

/** One owned grid's mod controls. Server replies own state; this widget only presents them. */
UCLASS()
class CONSTRUCTWORLD_API UConstructModPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Construct|Mods")
    void Build(const FString& StarterName);
    UFUNCTION(BlueprintCallable, Category = "Construct|Mods")
    void Deploy();
    UFUNCTION(BlueprintCallable, Category = "Construct|Mods")
    void SetEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Construct|Mods")
    void ShowLog();
    UFUNCTION(BlueprintCallable, Category = "Construct|Mods")
    void Delete();
    UFUNCTION(BlueprintCallable, Category = "Construct|Mods")
    void Refresh();
    UFUNCTION(BlueprintPure, Category = "Construct|Mods")
    EConstructModPanelState GetState() const { return State; }
    UFUNCTION(BlueprintPure, Category = "Construct|Mods")
    void GetLastBuildFiles(FString& BuildId, TArray<FConstructModFileHash>& Files) const;

    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModBuildProgress OnBuildProgress;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModBuildDone OnBuildDone;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModDeployed OnDeployed;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModEnabled OnEnabledChanged;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModLogShown OnLogShown;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModDeleted OnDeleted;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModStateChanged OnStateChanged;
    UPROPERTY(BlueprintAssignable, Category = "Construct|Mods")
    FConstructModWakeDone OnWakeDone;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;

private:
    friend struct FConstructModsLifecycleTestAccess;
    friend class UConstructModMenu;
    friend struct FConstructModsTestAccess;

    enum class EMutation : uint8 { None, Build, Deploy, Enable, Disable, Delete };
    void BindGrid(int64 InGridId, FInt64Vector InChunk);
    void CloseBinding();
    bool IsCurrent(uint64 ForBinding) const;
    bool IsMutationCurrent(uint64 ForBinding, uint64 ForMutation) const;
    bool CanUseSession() const;
    UCrowdyMods* Mods() const;
    bool BeginMutation(EMutation Kind);
    bool CanBuild() const;
    bool CanDeploy() const;
    bool CanToggle(bool bEnabled) const;
    bool CanLog() const;
    bool CanDelete() const;
    bool CanRefresh() const;
    void SelectStarter(const FString& Key);
    void FetchStarter();
    void StarterReturned(uint64 ForBinding, const FCrowdyModStarter& Reply);
    void ReadStatus(bool bManual = false);
    void StatusReturned(uint64 ForBinding, uint64 Request, uint64 Revision, const FCrowdyModList& Reply);
    void DeriveState();
    void ApplyMod(const FCrowdyMod& Mod);
    void ChangeState(EConstructModPanelState Next);
    void ShowError(ECrowdyModResult Result, const FString& Message, bool bRestore);
    void BuildProgress(uint64 ForBinding, uint64 ForMutation, const FCrowdyModBuild& Reply);
    void BuildReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModBuild& Reply);
    void DeployReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModInfo& Reply);
    bool EnabledReturned(uint64 ForBinding, uint64 ForMutation, bool bEnabled, const FCrowdyModInfo& Reply);
    void FreezeSources(const TArray<FCrowdyModFile>& Files);
    FString WakeMethod() const;
    void SendWake(uint64 ForBinding, uint64 ForMutation);
    void WakeReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModCall& Reply);
    void DeleteReturned(uint64 ForBinding, uint64 ForMutation, const FCrowdyModDelete& Reply);
    void LogReturned(uint64 ForBinding, uint64 Request, const FCrowdyModLogs& Reply);
    void ReloadReturned(uint64 ForBinding, uint64 Request, bool bSuccess, const FString& Message);
    void SpawnView();
    FText Caption() const;
    FText RefreshHint() const;

    UPROPERTY(Transient)
    TObjectPtr<AConstructModChunkAnchor> Anchor;
    UPROPERTY(Transient)
    TObjectPtr<AConstructChunkView> View;

    EConstructModPanelState State = EConstructModPanelState::Loading;
    EConstructModPanelState StableBefore = EConstructModPanelState::Loading;
    EMutation MutationKind = EMutation::None;
    TWeakObjectPtr<UWorld> BoundWorld;
    int64 GridId = 0;
    int64 UserId = 0;
    FInt64Vector Chunk = FInt64Vector::ZeroValue;
    uint64 Binding = 0;
    uint64 Mutation = 0;
    uint64 StateRevision = 0;
    uint64 StatusRequest = 0;
    uint64 LogRequest = 0;
    uint64 ReloadRequest = 0;
    uint64 ErrorRequest = 0;
    bool bBound = false;
    bool bStatusPending = false;
    bool bLogPending = false;
    bool bReloadPending = false;
    bool bStarterPending = false;
    bool bListKnown = false;
    bool bHasMod = false;
    bool bStarterReady = false;
    bool bWakePending = false;
    bool bEnableAnswered = false;
    bool bRunningFromList = false;
    FString SelectedStarter = TEXT("construct-spinning-child");
    FString ModName = TEXT("construct-spinning-child");
    FString LastBuildId;
    FString BuildLog;
    FString LogText;
    FText CachedLog;
    FText LandLabel;
    FString Notice;
    FString StarterError;
    ECrowdyModResult StarterResult = ECrowdyModResult::Failed;
    FCrowdyMod ShownMod;
    FCrowdyModStarter PlatformStarter;
    TArray<FCrowdyMod> ListedMods;
    TArray<FCrowdyModFile> SubmittedFiles;
    TArray<FConstructModFileHash> SubmittedHashes;
    ECrowdyModBuildStatus BuildStatus = ECrowdyModBuildStatus::Queued;
    double BuildStartedAt = 0.0;
    FTimerHandle StatusTimer;
    FTimerHandle ErrorTimer;
};