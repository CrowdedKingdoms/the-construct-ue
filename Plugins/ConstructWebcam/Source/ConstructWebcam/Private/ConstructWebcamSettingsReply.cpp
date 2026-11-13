#include "ConstructWebcamSettingsReply.h"

UConstructWebcamSettingsReply* UConstructWebcamSettingsReply::Read(UCrowdySafety& Safety, UObject* Outer, FAnswer InAnswer)
{
	UConstructWebcamSettingsReply* Reply = NewObject<UConstructWebcamSettingsReply>(Outer);
	Reply->Answer = MoveTemp(InAnswer);
	FOnCommunicationSettings OnSuccess;
	OnSuccess.BindUFunction(Reply, GET_FUNCTION_NAME_CHECKED(UConstructWebcamSettingsReply, Succeeded));
	FOnSafetyError OnError;
	OnError.BindUFunction(Reply, GET_FUNCTION_NAME_CHECKED(UConstructWebcamSettingsReply, Failed));
	Safety.GetCommunicationSettings(OnSuccess, OnError);
	return Reply;
}

void UConstructWebcamSettingsReply::Succeeded(const FCrowdyCommunicationSettings& Settings)
{
	Finish(&Settings, nullptr);
}

void UConstructWebcamSettingsReply::Failed(const FCrowdySafetyError& Error)
{
	Finish(nullptr, &Error);
}

void UConstructWebcamSettingsReply::Finish(const FCrowdyCommunicationSettings* Settings, const FCrowdySafetyError* Error)
{
	FAnswer Callback = MoveTemp(Answer);
	Answer = nullptr;
	if (Callback)
	{
		Callback(Settings, Error);
	}
}
