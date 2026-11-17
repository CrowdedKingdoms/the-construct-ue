#include "ConstructStudioDockFrames.h"

#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystem/CrowdyStudioAgent.h"

namespace
{
	using FFrameWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FFrameWriterFactory = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	FString WriteObject(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		const TSharedRef<FFrameWriter> Writer = FFrameWriterFactory::Create(&Json);
		if (!FJsonSerializer::Serialize(Object, Writer))
		{
			return FString();
		}
		return Json;
	}

	/** A string as a JSON literal, which is also a valid YAML double-quoted scalar. */
	FString Quoted(const FString& Value)
	{
		FString Json;
		const TSharedRef<FFrameWriter> Writer = FFrameWriterFactory::Create(&Json);
		Writer->WriteValue(Value);
		Writer->Close();
		return Json;
	}

	/** Only a real JSON string counts: FJsonObject's own getters turn numbers and booleans into text. */
	bool StringField(const FJsonObject& Object, const TCHAR* Name, FString& Out)
	{
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
		if (!Value.IsValid() || Value->Type != EJson::String)
		{
			return false;
		}
		Out = Value->AsString();
		return true;
	}

	TSharedPtr<FJsonObject> ObjectField(const FJsonObject& Object, const TCHAR* Name)
	{
		const TSharedPtr<FJsonValue> Value = Object.TryGetField(Name);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	/** Counts brackets outside strings, so a deeply nested frame is dropped before the recursive parser sees it. */
	bool IsShallow(const FString& Json)
	{
		int32 Depth = 0;
		bool bInString = false;
		bool bEscaped = false;
		for (const TCHAR C : Json)
		{
			if (bEscaped)
			{
				bEscaped = false;
				continue;
			}
			if (bInString)
			{
				bEscaped = C == TEXT('\\');
				bInString = C != TEXT('"');
				continue;
			}
			bInString = C == TEXT('"');
			Depth += (C == TEXT('{') || C == TEXT('[')) ? 1 : (C == TEXT('}') || C == TEXT(']')) ? -1 : 0;
			if (Depth > ConstructStudioDockFrames::MaxDepth)
			{
				return false;
			}
		}
		return true;
	}
}

bool ConstructStudioDockFrames::IsAnsweredInUnreal(const FString& Method)
{
	static const TCHAR* const Answered[] =
	{
		TEXT("studio.screenshot"), TEXT("studio.draftTest"), TEXT("studio.deployLive"), TEXT("studio.runtimeStatus"),
		TEXT("studio.runtimeLogs"), TEXT("studio.clientLogs"), TEXT("studio.projectList"), TEXT("studio.projectOpen"),
		TEXT("studio.projectCreate"), TEXT("game.observe"), TEXT("grid.context")
	};
	for (const TCHAR* Each : Answered)
	{
		if (Method.Equals(Each, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}
	return false;
}

const TCHAR* ConstructStudioDockFrames::RefusalCode(EConstructStudioDockRefusal Why)
{
	return Why == EConstructStudioDockRefusal::Failed ? TEXT("PAGE_ERROR") : TEXT("BRIDGE_REFUSED");
}

const TCHAR* ConstructStudioDockFrames::RefusalMessage(EConstructStudioDockRefusal Why)
{
	return Why == EConstructStudioDockRefusal::Failed ? TEXT("The request could not be done.") : TEXT("Not offered in Unreal.");
}

FString ConstructStudioDockFrames::PersistScope(int64 AppId, int64 UserId)
{
	const FTCHARToUTF8 Key(*FString::Printf(TEXT("%lld:%lld"), AppId, UserId));
	const FSHAHash Hash = FSHA1::HashBuffer(Key.Get(), Key.Length());
	return FString::Printf(TEXT("%lld/%s"), AppId, *Hash.ToString().Left(16).ToLower());
}

FString ConstructStudioDockFrames::NewSecret()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsLower) + FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
}

FString ConstructStudioDockFrames::BootReply(const FCrowdyStudioAgentBoot& Boot, const FString& ProjectId, const FString& Scope, const FString& Channel, const FString& Nonce)
{
	const TSharedRef<FJsonObject> Crowdy = MakeShared<FJsonObject>();
	Crowdy->SetStringField(TEXT("graphqlUrl"), Boot.GraphQLUrl);
	Crowdy->SetStringField(TEXT("appId"), LexToString(Boot.AppId));
	Crowdy->SetStringField(TEXT("projectId"), ProjectId);
	Crowdy->SetStringField(TEXT("bridgeChannel"), Channel);
	Crowdy->SetStringField(TEXT("bridgeNonce"), Nonce);
	Crowdy->SetStringField(TEXT("root"), Mount);
	Crowdy->SetStringField(TEXT("persistScope"), Scope);
	Crowdy->SetStringField(TEXT("studioMode"), TEXT("mod"));

	const TSharedRef<FJsonObject> Files = MakeShared<FJsonObject>();
	Files->SetStringField(TEXT("crowdy.json"), WriteObject(Crowdy));
	Files->SetStringField(TEXT("settings.yaml"), SettingsYaml(Boot));

	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetObjectField(TEXT("files"), Files);
	Reply->SetStringField(TEXT("channel"), Channel);
	Reply->SetStringField(TEXT("nonce"), Nonce);
	Reply->SetStringField(TEXT("persistScope"), Scope);
	Reply->SetStringField(TEXT("mount"), Mount);
	return WriteObject(Reply);
}

FString ConstructStudioDockFrames::SettingsYaml(const FCrowdyStudioAgentBoot& Boot)
{
	FString Models;
	const TSharedRef<FFrameWriter> Writer = FFrameWriterFactory::Create(&Models);
	Writer->WriteArrayStart();
	for (const FCrowdyStudioModel& Model : Boot.Models)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), Model.ModelId);
		Writer->WriteValue(TEXT("name"), Model.Name.IsEmpty() ? Model.ModelId : Model.Name);
		if (Model.ContextWindow > 0)
		{
			Writer->WriteValue(TEXT("contextWindow"), Model.ContextWindow);
		}
		TArray<FString> Modalities;
		for (const FString& Modality : Model.InputModalities)
		{
			if (Modality == TEXT("text") || Modality == TEXT("image"))
			{
				Modalities.AddUnique(Modality.ToLower());
			}
		}
		if (!Modalities.IsEmpty())
		{
			Writer->WriteValue(TEXT("inputModalities"), Modalities);
		}
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->Close();

	// Every value is a JSON literal, which YAML reads as flow syntax.
	FString Yaml = FString::Printf(TEXT("llm-deepseek:\n  apiKeyEnv: CROWDY_APP_TOKEN\n  baseURL: %s\n  thinking: disabled\n  models: %s\n"),
		*Quoted(Boot.ModelBaseUrl + TEXT("/v1/model")), *Models);
	if (!Boot.Models.IsEmpty())
	{
		Yaml += FString::Printf(TEXT("agent-default-model:\n  provider: deepseek-official\n  model: %s\n"), *Quoted(Boot.Models[0].ModelId));
	}
	return Yaml;
}

FString ConstructStudioDockFrames::OkReply()
{
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetBoolField(TEXT("ok"), true);
	return WriteObject(Reply);
}

FString ConstructStudioDockFrames::ErrorReply(const TCHAR* Message)
{
	const TSharedRef<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetStringField(TEXT("error"), Message);
	return WriteObject(Reply);
}

FConstructStudioDockChannel::FConstructStudioDockChannel(TFunction<void(const FString&)> InDeliver)
	: Sink(MoveTemp(InDeliver))
{
}

void FConstructStudioDockChannel::Begin(int64 InGeneration, const FString& InNonce)
{
	Generation = InGeneration;
	Nonce = InNonce;
	bPageJoined = false;
	bReadyGreeted = false;
}

void FConstructStudioDockChannel::End()
{
	Generation = 0;
	Nonce.Reset();
	bPageJoined = false;
	bReadyGreeted = false;
}

void FConstructStudioDockChannel::SetPageJoined(int64 InGeneration)
{
	if (InGeneration != 0 && InGeneration == Generation && !Nonce.IsEmpty())
	{
		bPageJoined = true;
	}
}

bool FConstructStudioDockChannel::HasPageJoined() const
{
	return bPageJoined;
}

bool FConstructStudioDockChannel::GreetReady(int64 InGeneration)
{
	// The worker answers every page.hello with worker.ready, so greeting each one would never end.
	if (!bPageJoined || InGeneration == 0 || InGeneration != Generation)
	{
		return false;
	}
	bReadyGreeted = !bReadyGreeted;
	return bReadyGreeted;
}

int64 FConstructStudioDockChannel::GetGeneration() const
{
	return Generation;
}

const FString& FConstructStudioDockChannel::GetNonce() const
{
	return Nonce;
}

TOptional<FConstructStudioDockFrame> FConstructStudioDockChannel::Parse(const FString& Json) const
{
	if (Nonce.IsEmpty() || Json.Len() > ConstructStudioDockFrames::MaxFrameChars || !IsShallow(Json))
	{
		return {};
	}
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Json), Object) || !Object.IsValid())
	{
		return {};
	}
	const TSharedPtr<FJsonValue> Version = Object->TryGetField(TEXT("v"));
	FString FrameNonce;
	FString From;
	FString Type;
	if (!Version.IsValid() || Version->Type != EJson::Number || Version->AsNumber() != ConstructStudioDockFrames::Version
		|| !StringField(*Object, TEXT("n"), FrameNonce) || !FrameNonce.Equals(Nonce, ESearchCase::CaseSensitive)
		|| !StringField(*Object, TEXT("from"), From) || !StringField(*Object, TEXT("t"), Type))
	{
		return {};
	}

	FConstructStudioDockFrame Frame;
	const bool bWorker = From.Equals(TEXT("worker"), ESearchCase::CaseSensitive);
	if (bWorker && Type.Equals(TEXT("req"), ESearchCase::CaseSensitive))
	{
		Frame.Kind = FConstructStudioDockFrame::EKind::Request;
		StringField(*Object, TEXT("method"), Frame.Name);
		StringField(*Object, TEXT("id"), Frame.Id);
		Frame.Body = ObjectField(*Object, TEXT("params"));
	}
	else if (bWorker && Type.Equals(TEXT("event"), ESearchCase::CaseSensitive))
	{
		Frame.Kind = FConstructStudioDockFrame::EKind::WorkerEvent;
		StringField(*Object, TEXT("event"), Frame.Name);
		Frame.Body = ObjectField(*Object, TEXT("payload"));
	}
	else if (From.Equals(TEXT("page"), ESearchCase::CaseSensitive) && Type.Equals(TEXT("dock"), ESearchCase::CaseSensitive))
	{
		Frame.Kind = FConstructStudioDockFrame::EKind::PageNotice;
		StringField(*Object, TEXT("event"), Frame.Name);
		Frame.Body = ObjectField(*Object, TEXT("payload"));
	}
	else
	{
		return {};
	}
	if (Frame.Name.IsEmpty() || (Frame.Kind == FConstructStudioDockFrame::EKind::Request && Frame.Id.IsEmpty()))
	{
		return {};
	}
	return Frame;
}

bool FConstructStudioDockChannel::Reply(int64 InGeneration, const FString& Id, const TSharedRef<FJsonObject>& Result) const
{
	const TSharedRef<FJsonObject> Frame = Envelope(TEXT("res"));
	Frame->SetStringField(TEXT("id"), Id);
	Frame->SetObjectField(TEXT("result"), Result);
	return Deliver(InGeneration, Frame);
}

bool FConstructStudioDockChannel::Refuse(int64 InGeneration, const FString& Id, EConstructStudioDockRefusal Why, const FString& Message) const
{
	const TSharedRef<FJsonObject> Frame = Envelope(TEXT("err"));
	Frame->SetStringField(TEXT("id"), Id);
	Frame->SetStringField(TEXT("code"), ConstructStudioDockFrames::RefusalCode(Why));
	Frame->SetStringField(TEXT("message"), Message.IsEmpty() ? FString(ConstructStudioDockFrames::RefusalMessage(Why)) : Message.Left(500));
	return Deliver(InGeneration, Frame);
}

bool FConstructStudioDockChannel::Tell(int64 InGeneration, const FString& Event, const TSharedRef<FJsonObject>& Payload) const
{
	if (Event.Equals(TEXT("page.hello"), ESearchCase::IgnoreCase) || Event.Equals(TEXT("page.token"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	const TSharedRef<FJsonObject> Frame = Envelope(TEXT("event"));
	Frame->SetStringField(TEXT("event"), Event);
	Frame->SetObjectField(TEXT("payload"), Payload);
	return Deliver(InGeneration, Frame);
}

bool FConstructStudioDockChannel::Hello(int64 InGeneration, const FString& AppId, const FString& ProjectId, const FString& Token) const
{
	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("appId"), AppId);
	if (ProjectId.IsEmpty())
	{
		Payload->SetField(TEXT("projectId"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Payload->SetStringField(TEXT("projectId"), ProjectId);
	}
	if (!Token.IsEmpty())
	{
		Payload->SetStringField(TEXT("appToken"), Token);
	}
	const TSharedRef<FJsonObject> Frame = Envelope(TEXT("event"));
	Frame->SetStringField(TEXT("event"), TEXT("page.hello"));
	Frame->SetObjectField(TEXT("payload"), Payload);
	return Deliver(InGeneration, Frame);
}

bool FConstructStudioDockChannel::Token(int64 InGeneration, const FString& NewToken) const
{
	if (NewToken.IsEmpty())
	{
		return false;
	}
	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("appToken"), NewToken);
	const TSharedRef<FJsonObject> Frame = Envelope(TEXT("event"));
	Frame->SetStringField(TEXT("event"), TEXT("page.token"));
	Frame->SetObjectField(TEXT("payload"), Payload);
	return Deliver(InGeneration, Frame);
}

TSharedRef<FJsonObject> FConstructStudioDockChannel::Envelope(const TCHAR* Type) const
{
	const TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
	Frame->SetNumberField(TEXT("v"), ConstructStudioDockFrames::Version);
	Frame->SetStringField(TEXT("n"), Nonce);
	Frame->SetStringField(TEXT("from"), TEXT("page"));
	Frame->SetStringField(TEXT("t"), Type);
	return Frame;
}

bool FConstructStudioDockChannel::Deliver(int64 InGeneration, const TSharedRef<FJsonObject>& Frame) const
{
	if (!bPageJoined || InGeneration == 0 || InGeneration != Generation || !Sink)
	{
		return false;
	}
	const FString Json = WriteObject(Frame);
	if (Json.IsEmpty())
	{
		return false;
	}
	Sink(Json);
	return true;
}
