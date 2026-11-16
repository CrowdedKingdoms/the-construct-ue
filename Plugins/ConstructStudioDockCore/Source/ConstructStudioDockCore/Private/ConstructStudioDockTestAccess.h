#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioDock.h"
#include "ConstructStudioDockTestListener.h"
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
