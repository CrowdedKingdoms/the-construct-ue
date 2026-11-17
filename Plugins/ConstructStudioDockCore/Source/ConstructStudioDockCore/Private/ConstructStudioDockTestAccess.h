#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioDock.h"
#include "ConstructStudioDockBridge.h"
#include "ConstructStudioDockPlatform.h"
#include "ConstructStudioDockTestListener.h"
#include "Dom/JsonObject.h"
#include "Subsystem/CrowdyStudioAgent.h"
#include "UObject/StrongObjectPtr.h"

/** Reaches the dock's inbound side and clock from tests, so they need neither a signed-in player nor the stand-in. */
struct FConstructStudioDockTestAccess
{
	static void SignIn(UConstructStudioDock& Dock, int64 UserId)
	{
		Dock.UserForTests = UserId;
	}

	/** Reads as the session's HasAccessEnded becoming true for the signed-in player. */
	static void EndAccess(UConstructStudioDock& Dock)
	{
		Dock.bAccessEndedForTests = true;
	}

	/** Stands in for Slate's keyboard focus on the dock's slot, which needs a window that headless tests lack. */
	static void SetSlotFocus(UConstructStudioDock& Dock, bool bFocused)
	{
		Dock.PlaceholderFocusForTests = bFocused;
	}

	static bool SlotFocused(const UConstructStudioDock& Dock)
	{
		return Dock.PlaceholderFocusForTests.Get(false);
	}

	static void Advance(UConstructStudioDock& Dock, float Seconds)
	{
		Dock.Advance(Seconds);
	}

	static int64 Generation(const UConstructStudioDock& Dock)
	{
		return Dock.Generation;
	}

	static void ClaimBoot(UConstructStudioDock& Dock)
	{
		Dock.ClaimBoot();
	}

	static void Boot(UConstructStudioDock& Dock, int64 Generation, EConstructStudioDockState State, EConstructStudioDockReason Reason = EConstructStudioDockReason::None, const FText& Text = FText::GetEmpty())
	{
		Dock.ReceiveBootOutcome(Generation, State, Reason, Text);
	}

	static void Project(UConstructStudioDock& Dock, const FString& ProjectId, const FString& Name, const FString& Revision, bool bReachesVisitors)
	{
		Dock.ReceiveProject(Dock.Generation, ProjectId, Name, Revision, bReachesVisitors);
	}

	static void FileChanged(UConstructStudioDock& Dock, const FString& Path, const FString& Revision)
	{
		Dock.ReceiveFileChanged(Dock.Generation, Path, Revision);
	}

	static void Request(UConstructStudioDock& Dock, EConstructStudioDeployKind Kind)
	{
		Dock.ReceiveDeployRequest(Dock.Generation, Kind);
	}

	static void RunAnswered(UConstructStudioDock& Dock, bool bOk)
	{
		Dock.ReceiveRunAnswer(Dock.Generation, bOk);
	}

	static void Screenshot(UConstructStudioDock& Dock, int64 Generation, const FString& Label)
	{
		Dock.ReceiveScreenshotRequest(Generation, Label);
	}

	static void Focus(UConstructStudioDock& Dock, bool bHasFocus)
	{
		Dock.ReceiveFocus(Dock.Generation, bHasFocus);
	}

	static void CloseRequest(UConstructStudioDock& Dock)
	{
		Dock.ReceiveCloseRequest(Dock.Generation);
	}

	/** Stands in for the SDK's boot material arriving for a dock whose page can run, with consent given and no project. */
	static void BootLive(UConstructStudioDock& Dock, const FCrowdyStudioAgentBoot& Boot)
	{
		if (!Dock.PlatformForTests.IsValid())
		{
			const TSharedRef<FConstructStudioDockPlatform> AtOnce = MakeShared<FConstructStudioDockPlatform>();
			AtOnce->GetProviderConsent = [](TFunction<void(const FCrowdyStudioConsent&)> OnDone)
			{
				FCrowdyStudioConsent Consent;
				Consent.Result = ECrowdyStudioResult::Success;
				Consent.bConsented = true;
				OnDone(Consent);
			};
			AtOnce->ListProjects = [](TFunction<void(const FCrowdyStudioProjectList&)> OnDone)
			{
				FCrowdyStudioProjectList List;
				List.Result = ECrowdyStudioResult::Success;
				OnDone(List);
			};
			Dock.PlatformForTests = AtOnce;
		}
		Dock.Platform = Dock.PlatformForTests;
		Dock.bLiveBoot = true;
		Dock.ReceiveAgentBoot(Dock.Generation, Boot);
	}

	/** Answers the dock's SDK calls from the test from the next Open on; the dock never reaches the real SDK while set. */
	static void UsePlatform(UConstructStudioDock& Dock, const TSharedRef<FConstructStudioDockPlatform>& Calls)
	{
		Dock.PlatformForTests = Calls;
	}

	static void WorkerEvent(UConstructStudioDock& Dock, const FString& Event, const TSharedRef<FJsonObject>& Payload)
	{
		Dock.ReceiveWorkerEvent(Dock.Generation, Event, Payload);
	}

	static int32 ChangedFileCount(const UConstructStudioDock& Dock)
	{
		return Dock.ChangedFiles.Num();
	}

	static UConstructStudioDockBridge* Bridge(const UConstructStudioDock& Dock)
	{
		return Dock.Bridge.Get();
	}

	/** Stands in for the browser's main frame URL, which needs a live browser. */
	static void SetMainFrame(UConstructStudioDock& Dock, const FString& Url)
	{
		Dock.MainFrameUrlForTests = Url;
	}

	static void SetToken(UConstructStudioDock& Dock, const FString& Token)
	{
		Dock.TokenForTests = Token;
	}

	/** Receives each script the dock would run in its page instead of the browser. */
	static void CaptureScripts(UConstructStudioDock& Dock, TFunction<void(const FString&)> Capture)
	{
		Dock.ScriptsForTests = MoveTemp(Capture);
	}

	static void TokenChanged(UConstructStudioDock& Dock, int64 UserId, const FString& Token)
	{
		Dock.AgentTokenChanged(UserId, Token);
	}

	static bool Reply(UConstructStudioDock& Dock, int64 Generation, const FString& Id, const TSharedRef<FJsonObject>& Result)
	{
		return Dock.ReplyToAgent(Generation, Id, Result);
	}

	static FDelegateHandle ListenOutbound(TFunction<void(UConstructStudioDock&, FName, const FString&)> Listener)
	{
		return UConstructStudioDock::Outbound.AddLambda([Listener = MoveTemp(Listener)](UConstructStudioDock& From, FName Message, const FString& Detail)
		{
			Listener(From, Message, Detail);
		});
	}

	static void StopListening(FDelegateHandle Handle)
	{
		UConstructStudioDock::Outbound.Remove(Handle);
	}
};

/** A signed-in dock with a recording listener as its host, and every message it sends, as "Message:Detail". */
struct FConstructStudioDockTestRig
{
	TStrongObjectPtr<UConstructStudioDock> Dock;
	TStrongObjectPtr<UConstructStudioDockTestListener> Listener;
	TArray<FString> Sent;
	FDelegateHandle Outbound;

	explicit FConstructStudioDockTestRig(bool bSignedIn = true, UObject* Outer = nullptr)
		: Dock(NewObject<UConstructStudioDock>(Outer ? Outer : GetTransientPackage()))
		, Listener(NewObject<UConstructStudioDockTestListener>(GetTransientPackage()))
	{
		if (bSignedIn)
		{
			FConstructStudioDockTestAccess::SignIn(*Dock, 42);
		}
		Listener->Watch(*Dock);
		Dock->SetHost(TScriptInterface<IConstructStudioDockHost>(Listener.Get()));
		Outbound = FConstructStudioDockTestAccess::ListenOutbound([this](UConstructStudioDock& From, FName Message, const FString& Detail)
		{
			if (&From == Dock.Get())
			{
				Sent.Add(FString::Printf(TEXT("%s:%s"), *Message.ToString(), *Detail));
			}
		});
	}

	~FConstructStudioDockTestRig()
	{
		Dock->Close();
		FConstructStudioDockTestAccess::StopListening(Outbound);
	}

	FConstructStudioDockTestRig(const FConstructStudioDockTestRig&) = delete;
	FConstructStudioDockTestRig& operator=(const FConstructStudioDockTestRig&) = delete;

	EConstructStudioDockReason Open()
	{
		return Dock->Open(100, FText::FromString(TEXT("Chunk 1, 2, 3")));
	}

	/** Opens with a replayed boot, so the inert failure stays away, and reaches Ready on a project. */
	void Ready(const FString& ProjectId = TEXT("7"), const FString& Revision = TEXT("rev-1"))
	{
		Open();
		FConstructStudioDockTestAccess::ClaimBoot(*Dock);
		FConstructStudioDockTestAccess::Boot(*Dock, FConstructStudioDockTestAccess::Generation(*Dock), EConstructStudioDockState::Ready);
		FConstructStudioDockTestAccess::Project(*Dock, ProjectId, TEXT("Project"), Revision, true);
	}

	int32 SentCount(const FString& Line) const
	{
		return Sent.FilterByPredicate([&Line](const FString& Each) { return Each.Equals(Line, ESearchCase::CaseSensitive); }).Num();
	}

	FString Lines() const
	{
		return FString::Join(Listener->Lines, TEXT(" | "));
	}
};

template <typename EnumType>
FString ConstructStudioDockName(EnumType Value)
{
	return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
}

#endif
