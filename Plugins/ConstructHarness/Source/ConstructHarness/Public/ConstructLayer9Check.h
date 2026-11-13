#pragma once

#include "CoreMinimal.h"
#include "ConstructPresenceGameMode.h"
#include "ConstructPresencePawn.h"
#include "Containers/Ticker.h"
#include "Replication/Subsystems/CrowdyVideoFrameReceiver.h"
#include "Subsystem/CrowdyVideoSender.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer9Check.generated.h"

class UConstructWebcamFeeds;
class UConstructWebcamSubsystem;

/** The layer 9 check's pawn: the presence pawn with no local movement of its own, so the check places it. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer9CheckPawn : public AConstructPresencePawn
{
	GENERATED_BODY()

protected:
	// The check sets its transform itself, so Character Movement must never write it too.
	virtual bool ShouldUseLocalCharacterMovement() const override;
};

/** The presence GameMode with the check pawn. */
UCLASS()
class CONSTRUCTHARNESS_API AConstructLayer9CheckGameMode : public AConstructPresenceGameMode
{
	GENERATED_BODY()

public:
	AConstructLayer9CheckGameMode();
};

/** On L_Layer9Check: A sends numbered test frames, then its real camera; B moves away and back and reads what it decodes. */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer9CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	static constexpr int32 FrameWidth = 128;
	static constexpr int32 FrameHeight = 96;
	static constexpr double LumaVariedThreshold = 8.0;

	/** A 128x96 frame of black and white blocks that carry Sequence (1 to 65535) and a fixed marker. */
	static void DrawSequence(int32 Sequence, TArray<FColor>& OutPixels);

	/** The sequence a frame's blocks carry, or 0 when the frame is not a readable pattern. */
	static int32 ReadSequence(TConstArrayView<FColor> Pixels, FIntPoint Size);

	/** The standard deviation of the frame's luma, on a 0 to 255 scale. */
	static double LumaDeviation(TConstArrayView<FColor> Pixels);

	/** A at the centre of chunk (6, 0, 0); B 400 units from A when near, and at the centre of chunk (9, 0, 0) when far. */
	static FVector SpotFor(bool bInSender, bool bFar, double ChunkSize);

private:
	enum class EStage : uint8
	{
		WaitEntry,
		WaitPeer,
		Settle,
		WaitGo,
		Run,
		Done,
		Failed
	};

	enum class ECameraLeg : uint8
	{
		TestSource,
		Opening,
		Opened,
		NotRun,
		Failed
	};

	bool Tick(float DeltaTime);
	bool ReadControl(double Now, FString& OutControl);
	bool InstallHooks();
	void RemoveHooks();
	bool IsSenderCameraOn(double Now);
	bool TickSettle(double Now);
	void TickRun(double Now);
	void BeginCameraLeg(double Now);
	void TickCameraLeg(double Now);
	bool DrawNextFrame(TArray<FColor>& OutPixels);
	void HandleSent(const FCrowdyVideoFrameSend& Send);
	void HandleDecoded(APawn* Pawn, const TArray<FColor>& Pixels, FIntPoint Size);
	bool MoveTo(const FVector& Spot, const TCHAR* Name);
	AConstructLayer9CheckPawn* FindLocalPawn() const;
	FGuid FindVisiblePeer() const;
	void LogDone();
	void Fail(const FString& Why);

	bool IsViewerReceiving(double Now);

	UFUNCTION()
	void HandleVideoRefused(const FCrowdySendRefusal& Refusal);
	UFUNCTION()
	void HandleFrameArrived(const FCrowdyVideoFrame& Frame);

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<UConstructWebcamSubsystem> Webcam;
	TWeakObjectPtr<UConstructWebcamFeeds> Feeds;
	TWeakObjectPtr<UCrowdyVideoSender> Sender;
	TWeakObjectPtr<UCrowdyVideoFrameReceiver> Receiver;
	FString ControlPath;
	FGuid PeerId;
	FVector NearSpot = FVector::ZeroVector;
	FVector FarSpot = FVector::ZeroVector;
	EStage Stage = EStage::WaitEntry;
	ECameraLeg CameraLeg = ECameraLeg::TestSource;
	bool bSender = false;
	bool bSourceInstalled = false;
	bool bObserverInstalled = false;
	bool bWindowOpen = false;
	bool bCameraLeftOn = false;
	double StartedAt = 0.0;
	double GoAt = 0.0;
	double LastPollAt = 0.0;
	double SettleStartedAt = 0.0;
	double CameraLegStartedAt = 0.0;
	double TimeoutSeconds = 900.0;
	int32 PatternDrawn = 0;
	int32 StreamDrawn = 0;
	int32 LastDrawn = 0;
	int32 WindowIndex = 0;
	int32 MoveIndex = 0;
	int32 WindowArrivals = 0;
};
