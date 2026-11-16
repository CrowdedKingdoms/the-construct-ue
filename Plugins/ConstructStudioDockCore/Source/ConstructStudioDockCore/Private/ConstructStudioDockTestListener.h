#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDock.h"
#include "ConstructStudioDockHost.h"
#include "UObject/Object.h"
#include "ConstructStudioDockTestListener.generated.h"

/** Records every event a dock fires, one line each, and holds each CaptureView answer for the test to give. */
UCLASS(Transient, Hidden)
class UConstructStudioDockTestListener : public UObject, public IConstructStudioDockHost
{
	GENERATED_BODY()

public:
	void Watch(UConstructStudioDock& Dock)
	{
		Dock.OnStateChanged.AddDynamic(this, &UConstructStudioDockTestListener::StateChanged);
		Dock.OnDeployPrompt.AddDynamic(this, &UConstructStudioDockTestListener::Prompted);
		Dock.OnDeployPromptWithdrawn.AddDynamic(this, &UConstructStudioDockTestListener::Withdrawn);
		Dock.OnUsageChanged.AddDynamic(this, &UConstructStudioDockTestListener::UsageChanged);
		Dock.OnWarning.AddDynamic(this, &UConstructStudioDockTestListener::Warned);
		Dock.OnFocusChanged.AddDynamic(this, &UConstructStudioDockTestListener::FocusChanged);
		Dock.OnCloseRequested.AddDynamic(this, &UConstructStudioDockTestListener::CloseRequested);
	}

	virtual void CaptureView_Implementation(const FString& Label, const FOnConstructStudioCaptured& OnDone) override
	{
		Lines.Add(FString::Printf(TEXT("Capture:%s"), *Label));
		Captures.Add(OnDone);
	}

	UFUNCTION()
	void StateChanged(EConstructStudioDockState State, EConstructStudioDockReason Reason)
	{
		Lines.Add(FString::Printf(TEXT("State:%s:%s"), *StaticEnum<EConstructStudioDockState>()->GetNameStringByValue(static_cast<int64>(State)),
			*StaticEnum<EConstructStudioDockReason>()->GetNameStringByValue(static_cast<int64>(Reason))));
	}

	UFUNCTION()
	void Prompted(const FConstructStudioDeployPrompt& Prompt)
	{
		Prompts.Add(Prompt);
		Lines.Add(TEXT("Prompt"));
	}

	UFUNCTION()
	void Withdrawn(int64 PromptId)
	{
		WithdrawnIds.Add(PromptId);
		Lines.Add(TEXT("Withdrawn"));
	}

	UFUNCTION()
	void UsageChanged(const FConstructStudioUsage& Usage)
	{
		Lines.Add(FString::Printf(TEXT("Usage:%lld"), Usage.TodayRequests));
	}

	UFUNCTION()
	void Warned(const FText& Text)
	{
		Lines.Add(FString::Printf(TEXT("Warning:%s"), *Text.ToString()));
	}

	UFUNCTION()
	void FocusChanged(bool bHasFocus)
	{
		Lines.Add(FString::Printf(TEXT("Focus:%d"), bHasFocus ? 1 : 0));
	}

	UFUNCTION()
	void CloseRequested()
	{
		Lines.Add(TEXT("CloseRequested"));
	}

	bool Saw(const FString& Prefix) const
	{
		return Lines.ContainsByPredicate([&Prefix](const FString& Line) { return Line.StartsWith(Prefix, ESearchCase::CaseSensitive); });
	}

	TArray<FString> Lines;
	TArray<FConstructStudioDeployPrompt> Prompts;
	TArray<int64> WithdrawnIds;
	TArray<FOnConstructStudioCaptured> Captures;
};
