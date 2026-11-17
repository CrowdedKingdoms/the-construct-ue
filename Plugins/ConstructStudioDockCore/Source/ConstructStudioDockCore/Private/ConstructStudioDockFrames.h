#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FCrowdyStudioAgentBoot;

/** Why the dock refuses one of the agent's requests; each maps to the harness's own error code. */
enum class EConstructStudioDockRefusal : uint8
{
	/** A request the dock answers in Unreal, but not in this build. */
	NotOfferedYet,
	/** A request the dock never answers in Unreal. */
	NotOffered
};

/** One frame from the dock's page that passed the version, nonce and sender checks. These checks authenticate nothing. */
struct FConstructStudioDockFrame
{
	enum class EKind : uint8
	{
		/** The worker asks the page something; answer it once, by Id. */
		Request,
		WorkerEvent,
		/** The page's own report on the harness: joined, booted or failed. */
		PageNotice
	};

	EKind Kind = EKind::Request;

	/** The request's method, or the event's or notice's name. */
	FString Name;

	/** The request's id; empty for events and notices. */
	FString Id;

	/** The request's params, or the event's or notice's payload, when it is a JSON object. */
	TSharedPtr<FJsonObject> Body;
};

namespace ConstructStudioDockFrames
{
	/** The harness's bridge protocol version; a frame with any other is dropped. */
	inline constexpr int32 Version = 5;

	/** The page reaches the bridge object as window.ue.constructstudiodock; the browser lowercases bound names. */
	inline constexpr const TCHAR* BridgeName = TEXT("constructstudiodock");

	inline constexpr const TCHAR* Mount = TEXT("/dsh/workspace");

	/** Frames longer than this, or nested deeper than MaxDepth, are dropped unparsed. */
	inline constexpr int32 MaxFrameChars = 1 << 20;
	inline constexpr int32 MaxDepth = 32;

	/** Whether the dock answers Method in Unreal; the rest are refused as not offered. */
	bool IsAnsweredInUnreal(const FString& Method);

	const TCHAR* RefusalCode(EConstructStudioDockRefusal Why);
	const TCHAR* RefusalMessage(EConstructStudioDockRefusal Why);

	/** Where the harness keeps this player's sessions: the app id and a hash of the user id. A folder name, not a lock. */
	FString PersistScope(int64 AppId, int64 UserId);

	/** A fresh random value for a nonce or channel name. */
	FString NewSecret();

	/** What the page's boot() answers: the crowdy.json and settings.yaml files, the channel, nonce, persist scope and mount. */
	FString BootReply(const FCrowdyStudioAgentBoot& Boot, const FString& ProjectId, const FString& Scope, const FString& Channel, const FString& Nonce);

	/** The harness's settings.yaml for the boot's model endpoint and models; the first model is the default. */
	FString SettingsYaml(const FCrowdyStudioAgentBoot& Boot);

	/** The bridge's answers to the page: {"ok":true}, or {"error":Message}. */
	FString OkReply();
	FString ErrorReply(const TCHAR* Message);
}

/**
 * One boot's end of the harness's channel. A frame goes out only for the current generation once the page has joined,
 * and the token only in page.hello and page.token, which only Hello and Token write.
 */
class FConstructStudioDockChannel
{
public:
	/** Deliver receives each outgoing frame as JSON text. */
	explicit FConstructStudioDockChannel(TFunction<void(const FString&)> InDeliver);

	/** Starts a channel for a boot, with the nonce the page hands the harness; the page has not joined yet. */
	void Begin(int64 InGeneration, const FString& InNonce);

	/** Drops everything: no frame is read or sent until the next Begin. */
	void End();

	/** The page joined the channel for InGeneration; frames may now go out. */
	void SetPageJoined(int64 InGeneration);

	bool HasPageJoined() const;
	int64 GetGeneration() const;
	const FString& GetNonce() const;

	/** The frame in Json, or unset when it is malformed, too large, or has the wrong version, nonce or sender. */
	TOptional<FConstructStudioDockFrame> Parse(const FString& Json) const;

	bool Reply(int64 InGeneration, const FString& Id, const TSharedRef<FJsonObject>& Result) const;
	bool Refuse(int64 InGeneration, const FString& Id, EConstructStudioDockRefusal Why) const;

	/** Sends a page event; refuses page.hello and page.token, which carry the token. */
	bool Tell(int64 InGeneration, const FString& Event, const TSharedRef<FJsonObject>& Payload) const;

	/** page.hello: the app, the open project (null when none) and, when not empty, the app token. */
	bool Hello(int64 InGeneration, const FString& AppId, const FString& ProjectId, const FString& Token) const;

	/** page.token with a new app token; an empty token sends nothing. */
	bool Token(int64 InGeneration, const FString& NewToken) const;

private:
	TSharedRef<FJsonObject> Envelope(const TCHAR* Type) const;
	bool Deliver(int64 InGeneration, const TSharedRef<FJsonObject>& Frame) const;

	TFunction<void(const FString&)> Sink;
	FString Nonce;
	int64 Generation = 0;
	bool bPageJoined = false;
};
