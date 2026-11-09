#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "ConstructLayer5Check.generated.h"

class AConstructChunkView;
struct FCrowdyChunkFetch;

/**
 * On L_Layer5Check: once signed in and connected, waits for Tools/Layer5Check's "go", fetches the chunks around the seed
 * chunk, draws them with the placed AConstructChunkView and logs what it read and drew for the Node side to judge.
 */
UCLASS()
class CONSTRUCTHARNESS_API UConstructLayer5CheckSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	enum class EStage : uint8
	{
		WaitEntry,
		WaitGo,
		Fetching,
		Done,
		Failed
	};

	bool Tick(float DeltaTime);
	bool TickEntry();
	bool ReadControl(const FString& Expected) const;
	void Fetch();
	void HandleFetch(const FCrowdyChunkFetch& Fetch);
	void Fail(const FString& Why);

	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<AConstructChunkView> View;
	FString ControlPath;
	FString WriterPath;
	EStage Stage = EStage::WaitEntry;
	double StartedAt = 0.0;
	double LastPollAt = 0.0;
};
