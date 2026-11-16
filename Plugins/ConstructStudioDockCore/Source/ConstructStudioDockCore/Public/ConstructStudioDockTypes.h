#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDockTypes.generated.h"

UENUM(BlueprintType)
enum class EConstructStudioDockState : uint8
{
	Closed,
	Booting,
	NeedsConsent,
	Ready,
	Failed
};

/** Why Open refused, why the state changed, or why the dock closed itself. */
UENUM(BlueprintType)
enum class EConstructStudioDockReason : uint8
{
	None,
	NotSignedIn,
	NoGrid,
	AlreadyOpen,
	NotAvailable,
	NeedsConsent,
	NoModel,
	Unavailable,
	HarnessFailed,
	BootTimedOut,
	NotBuiltYet,
	Closed,
	SignedOut,
	UserChanged,
	AccessEnded,
	WorldEnded
};

UENUM(BlueprintType)
enum class EConstructStudioDeployKind : uint8
{
	DraftTest,
	Live
};

/** The agent's request to draft-test or deploy, fixed when it asked. */
USTRUCT(BlueprintType)
struct CONSTRUCTSTUDIODOCKCORE_API FConstructStudioDeployPrompt
{
	GENERATED_BODY()

	/** Unique in this process; 0 is never a prompt. */
	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	int64 PromptId = 0;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	EConstructStudioDeployKind Kind = EConstructStudioDeployKind::DraftTest;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	int64 GridId = 0;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FText GridLabel;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString ProjectId;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString ProjectName;

	/** The project's saved source revision when the agent asked; Run builds exactly this one. */
	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString Revision;

	/** True when the project has a CLIENT half, which runs on visitors' machines. */
	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	bool bReachesVisitors = false;

	/** The files the agent changed since this dock booted. */
	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	TArray<FString> ChangedFiles;
};

/** The player's model usage, as the platform reports it. */
USTRUCT(BlueprintType)
struct CONSTRUCTSTUDIODOCKCORE_API FConstructStudioUsage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString Payer;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	int64 TodayRequests = 0;

	/** Today's charge as a decimal string. */
	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString TodayCharge;

	/** The day's limit as a decimal string; empty when there is none. */
	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString DayLimit;
};

/** A host's answer to CaptureView. */
USTRUCT(BlueprintType)
struct CONSTRUCTSTUDIODOCKCORE_API FConstructStudioCapture
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	bool bCaptured = false;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString PngBase64;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	int32 Width = 0;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	int32 Height = 0;

	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString Caption;

	/** Why nothing was captured; empty when bCaptured. */
	UPROPERTY(BlueprintReadWrite, Category="Construct|Studio Dock")
	FString Refusal;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnConstructStudioCaptured, const FConstructStudioCapture&, Capture);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnConstructStudioDockStateChanged, EConstructStudioDockState, State, EConstructStudioDockReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConstructStudioDeployPrompt, const FConstructStudioDeployPrompt&, Prompt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConstructStudioDeployPromptWithdrawn, int64, PromptId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConstructStudioUsageChanged, const FConstructStudioUsage&, Usage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConstructStudioWarning, const FText&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConstructStudioFocusChanged, bool, bHasFocus);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnConstructStudioCloseRequested);
