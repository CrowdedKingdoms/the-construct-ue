#include "ConstructLayer4Check.h"

#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TSharedRef<FJsonObject> ChunkToJson(const FConstructHubChunk& Chunk)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("X"), LexToString(Chunk.X));
		Object->SetStringField(TEXT("Y"), LexToString(Chunk.Y));
		Object->SetStringField(TEXT("Z"), LexToString(Chunk.Z));
		return Object;
	}
}

TSharedRef<FJsonObject> AConstructLayer4CheckProbe::ClaimToJson(const FConstructHubClaim& Claim)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("GridId"), LexToString(Claim.GridId));
	Object->SetObjectField(TEXT("Chunk"), ChunkToJson(Claim.Chunk));
	Object->SetStringField(TEXT("OwnerUserId"), LexToString(Claim.OwnerUserId));
	Object->SetStringField(TEXT("ClaimedAt"), LexToString(Claim.ClaimedAt));
	return Object;
}

FString AConstructLayer4CheckProbe::AnswerToJson(ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TSharedRef<FJsonObject>& Reply)
{
	const TSharedRef<FJsonObject> Answer = MakeShared<FJsonObject>();
	Answer->SetStringField(TEXT("outcome"), StaticEnum<ECrowdyServerCallOutcome>()->GetNameStringByValue(static_cast<int64>(Outcome)));
	Answer->SetStringField(TEXT("reason"), Reason);
	Answer->SetBoolField(TEXT("retryable"), bRetryable);
	Answer->SetObjectField(TEXT("reply"), Outcome == ECrowdyServerCallOutcome::Success ? Reply : MakeShared<FJsonObject>());

	FString Line;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Answer, Writer);
	return Line;
}

void AConstructLayer4CheckProbe::Emit(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TSharedRef<FJsonObject>& Reply)
{
	OnAnswer.Broadcast(Step, AnswerToJson(Outcome, Reason, bRetryable, Reply));
}

void AConstructLayer4CheckProbe::ReportWorld(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, int64 Pulses, int64 LastPulseAt)
{
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetStringField(TEXT("Pulses"), LexToString(Pulses));
	Reply->SetStringField(TEXT("LastPulseAt"), LexToString(LastPulseAt));
	Emit(Step, Outcome, Reason, bRetryable, Reply);
}

void AConstructLayer4CheckProbe::ReportProgress(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, int64 Xp, int64 Level, int64 SkillPoints)
{
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetStringField(TEXT("Xp"), LexToString(Xp));
	Reply->SetStringField(TEXT("Level"), LexToString(Level));
	Reply->SetStringField(TEXT("SkillPoints"), LexToString(SkillPoints));
	Emit(Step, Outcome, Reason, bRetryable, Reply);
}

void AConstructLayer4CheckProbe::ReportReadRefused(int32 Step, const FString& FailureReason)
{
	Emit(Step, ECrowdyServerCallOutcome::Denied, FailureReason, false, MakeShared<FJsonObject>());
}

void AConstructLayer4CheckProbe::ReportPrograms(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TArray<FConstructHubProgram>& Programs)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Reserve(Programs.Num());
	for (const FConstructHubProgram& Program : Programs)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("ProgramId"), Program.ProgramId);
		Item->SetStringField(TEXT("SceneId"), Program.SceneId);
		Item->SetStringField(TEXT("Name"), Program.Name);
		Item->SetStringField(TEXT("Description"), Program.Description);
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetArrayField(TEXT("Programs"), Items);
	Emit(Step, Outcome, Reason, bRetryable, Reply);
}

void AConstructLayer4CheckProbe::ReportClaims(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, const TArray<FConstructHubClaim>& Claims, int32 Total)
{
	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Reserve(Claims.Num());
	for (const FConstructHubClaim& Claim : Claims)
	{
		Items.Add(MakeShared<FJsonValueObject>(ClaimToJson(Claim)));
	}
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetArrayField(TEXT("Claims"), Items);
	Reply->SetNumberField(TEXT("Total"), Total);
	Emit(Step, Outcome, Reason, bRetryable, Reply);
}

void AConstructLayer4CheckProbe::ReportRecord(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, bool bRecorded, const FConstructHubClaim& Claim)
{
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetBoolField(TEXT("bRecorded"), bRecorded);
	Reply->SetObjectField(TEXT("Claim"), ClaimToJson(Claim));
	Emit(Step, Outcome, Reason, bRetryable, Reply);
}

void AConstructLayer4CheckProbe::ReportRelease(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, bool bReleased)
{
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetBoolField(TEXT("bReleased"), bReleased);
	Emit(Step, Outcome, Reason, bRetryable, Reply);
}

void AConstructLayer4CheckProbe::ReportForget(int32 Step, ECrowdyServerCallOutcome Outcome, const FString& Reason, bool bRetryable, bool bForgotten)
{
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetBoolField(TEXT("bForgotten"), bForgotten);
	Emit(Step, Outcome, Reason, bRetryable, Reply);
}
