#include "ConstructWebcamSafetyRequest.h"

#include "ConstructWebcamPanel.h"
#include "ConstructWebcamSubsystem.h"
#include "ConstructWebcamFeeds.h"

void UConstructWebcamSafetyRequest::SettingsReceived(const FCrowdyCommunicationSettings& Value)
{
    UConstructWebcamPanel* Owner = Panel.Get();
    if (!Owner || !Owner->Consume(this))
    {
        return;
    }
    Owner->Settings = Value;
    Owner->bSettingsKnown = true;
    Owner->Message = FText::FromString(TEXT("Video settings updated."));
    if (Action == EConstructWebcamSafetyAction::ChangeScope)
    {
        Owner->Camera->RefreshSettings();
        Owner->Feeds->RefreshSettings();
    }
    if (Value.VideoSend == ECrowdyCommunicationScope::Off)
    {
        Owner->Camera->SetCameraOn(false);
    }
    if (Value.VideoReceive == ECrowdyCommunicationScope::Off)
    {
        Owner->bShowVideo = false;
        Owner->Feeds->SetShowVideo(false);
        Owner->ReleaseRemotes();
    }
}

void UConstructWebcamSafetyRequest::Done()
{
    UConstructWebcamPanel* Owner = Panel.Get();
    if (!Owner || !Owner->Consume(this))
    {
        return;
    }
    if (Action == EConstructWebcamSafetyAction::Report)
    {
        Owner->CloseDialog();
        Owner->Message = FText::FromString(TEXT("Report sent."));
    }
    else
    {
        Owner->Message = FText::FromString(TEXT("Player blocked."));
    }
}

void UConstructWebcamSafetyRequest::ConsentReceived(ECrowdyConsentStatus Status)
{
    UConstructWebcamPanel* Owner = Panel.Get();
    if (!Owner || !Owner->Consume(this))
    {
        return;
    }
    Owner->Message = FText::FromString(Status == ECrowdyConsentStatus::Requested
        ? TEXT("Request sent. Your parent needs to complete the consent process.")
        : TEXT("Consent checked. Refresh video settings to see what is available."));
}

void UConstructWebcamSafetyRequest::Failed(const FCrowdySafetyError& Error)
{
    UConstructWebcamPanel* Owner = Panel.Get();
    if (!Owner || !Owner->Consume(this))
    {
        return;
    }
    if (Error.Code == ECrowdySafetyErrorCode::ConsentUnavailable)
    {
        Owner->Message = FText::FromString(TEXT("Parental consent is not available yet."));
    }
    else if (Error.Code == ECrowdySafetyErrorCode::RateLimited)
    {
        Owner->Message = FText::FromString(TEXT("Try again later."));
    }
    else
    {
        Owner->Message = Error.Message.IsEmpty() ? FText::FromString(TEXT("The request failed. Try again.")) : FText::FromString(Error.Message);
    }
}
