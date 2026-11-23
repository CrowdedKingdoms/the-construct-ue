#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConstructWebcamTypes.h"
#include "Styling/SlateBrush.h"
#include "Subsystem/CrowdySafety.h"
#include "ConstructWebcamPanel.generated.h"

class APawn;
class AConstructPresencePawn;
class UConstructWebcamFeedWidget;
class UConstructWebcamSafetyRequest;
class UConstructWebcamSubsystem;
class UConstructWebcamFeeds;
class UCrowdyGameSession;
class UInputComponent;
class UWidgetComponent;
class SEditableTextBox;
struct FConstructWebcamUITestAccess;

USTRUCT()
struct FConstructWebcamRemoteUI
{
    GENERATED_BODY()
    UPROPERTY(Transient)
    TWeakObjectPtr<AConstructPresencePawn> Pawn;
    UPROPERTY(Transient)
    TObjectPtr<UWidgetComponent> Component;
    UPROPERTY(Transient)
    TObjectPtr<UConstructWebcamFeedWidget> Widget;
    FGuid EntityId;
};

USTRUCT()
struct FConstructWebcamReportTarget
{
    GENERATED_BODY()
    int64 UserId = 0;
    FString SenderId;
    FDateTime OccurredAt;
    int32 Generation = 0;
    FString DisplayName;
};

/** Thin camera and safety presentation. Capture and feed state belong to the webcam source. */
UCLASS()
class CONSTRUCTPANELS_API UConstructWebcamPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void BindSources(UConstructWebcamSubsystem* CameraSource, UConstructWebcamFeeds* FeedSource);
    void Shutdown();
    UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
    void ToggleCamera();
    UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
    void ToggleVideo();
    UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
    void ToggleSettings();
    UFUNCTION(BlueprintCallable, Category = "Construct|Webcam")
    void CloseDialog();

    bool IsAvailable() const;
    bool IsShowingVideo() const { return bShowVideo; }
    bool IsCameraOn() const;
    bool IsSettingsOpen() const { return bSettingsOpen; }
    bool IsReportOpen() const { return bReportOpen; }
    bool IsBusy() const { return Requests.Num() != 0; }
    bool AreSettingsKnown() const { return bSettingsKnown; }
    bool CanInteract() const;
    bool CanModerate(AConstructPresencePawn* Pawn) const;
    FText GetCameraCaption() const;
    FText GetReasonText() const;
    FText GetRecordingText() const;
    FText GetMessage() const { return Message; }
    FText GetReportHeading() const;
    FText GetScopeText(ECrowdyCommunicationSetting Setting) const;
    const FSlateBrush* GetPreviewBrush() const { return &PreviewBrush; }

    void ReadSettings();
    void SetScope(ECrowdyCommunicationSetting Setting, ECrowdyCommunicationScope Scope);
    void AskParent(const FString& Email);
    void BlockPlayer(AConstructPresencePawn* Pawn);
    void OpenReport(AConstructPresencePawn* Pawn);
    void SubmitReport(ECrowdySafetyReportReason Reason, const FString& Details);
    static FText ReasonText(EConstructCameraBlockedReason Reason);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
    friend class UConstructWebcamSafetyRequest;
    friend struct FConstructWebcamUITestAccess;
    UFUNCTION()
    void CameraChanged(EConstructCameraState State, EConstructCameraBlockedReason Reason);
    UFUNCTION()
    void FeedChanged(APawn* Pawn);
    UFUNCTION()
    void RecordingChanged(const FCrowdyAppRecording& Recording);

    void Refresh();
    void RefreshRemote(AConstructPresencePawn* Pawn);
    void PresentFeed(AConstructPresencePawn* Pawn, const FConstructWebcamFeed& Info);
    void RemoveRemote(int32 Index);
    void ReleaseRemotes();
    bool IsCurrent(uint64 ExpectedGeneration) const;
    UCrowdySafety* Safety() const;
    UConstructWebcamSafetyRequest* BeginRequest(uint8 Kind);
    bool Consume(UConstructWebcamSafetyRequest* Request);
    void SetDialog(bool bSettings, bool bReport);
    void SetNotice();
    void StopInput();

    UPROPERTY(Transient)
    TObjectPtr<UConstructWebcamSubsystem> Camera;
    UPROPERTY(Transient)
    TObjectPtr<UConstructWebcamFeeds> Feeds;
    UPROPERTY(Transient)
    TArray<FConstructWebcamRemoteUI> Remotes;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UConstructWebcamSafetyRequest>> Requests;
    UPROPERTY(Transient)
    TObjectPtr<UInputComponent> CameraInput;
    UPROPERTY(Transient)
    FSlateBrush PreviewBrush;
    TWeakObjectPtr<UWorld> BoundWorld;
    TWeakObjectPtr<ULocalPlayer> InputOwner;
    TWeakObjectPtr<UCrowdyGameSession> Session;
    int64 SessionUser = 0;
    int64 SessionTokenId = 0;
    FTimerHandle RefreshTimer;
    uint64 Generation = 0;
    double NextDiscovery = 0.0;
    FConstructWebcamReportTarget ReportTarget;
    FCrowdyCommunicationSettings Settings;
    FText Message;
    FText RecordingNotice;
    TSharedPtr<SEditableTextBox> ParentEmail;
    TSharedPtr<SEditableTextBox> ReportDetails;
    bool bEnabledForWorld = false;
    bool bActive = false;
    bool bShowVideo = false;
    bool bSettingsOpen = false;
    bool bReportOpen = false;
    bool bSettingsKnown = false;
    bool bSafetyRecorded = false;
    bool bNoticeVisible = false;
    bool bShuttingDown = false;
};
