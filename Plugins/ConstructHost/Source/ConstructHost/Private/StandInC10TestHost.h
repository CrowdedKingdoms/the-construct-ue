#pragma once

#include "CoreMinimal.h"
#include "StandInC10.h"
#include "Subsystem/CrowdyModScriptHost.h"
#include "UObject/Object.h"
#include "StandInC10TestHost.generated.h"

/** Records every host call and event the stand-in replays, one line each, for the stand-in's tests. */
UCLASS(Transient, Hidden)
class UStandInC10TestHost : public UObject, public ICrowdyModScriptHost
{
	GENERATED_BODY()

public:
	virtual void DrainClicks_Implementation(FCrowdyModScriptClicks& Clicks) override
	{
		Lines.Add(TEXT("DrainClicks"));
	}

	virtual bool ReadChunk_Implementation(int64 ChunkX, int64 ChunkY, int64 ChunkZ, FCrowdyModScriptChunk& Chunk) override
	{
		Lines.Add(FString::Printf(TEXT("ReadChunk:%lld,%lld,%lld"), ChunkX, ChunkY, ChunkZ));
		return false;
	}

	virtual void ListPlayers_Implementation(const FCrowdyModScriptGrid& Box, TArray<FCrowdyModScriptPlayer>& Players) override
	{
		Lines.Add(FString::Printf(TEXT("ListPlayers:%lld"), Box.GridId));
	}

	virtual bool SetVoxel_Implementation(const FCrowdyModScript& Script, const FCrowdyVoxelWrite& Write, FString& Refusal) override
	{
		Lines.Add(FString::Printf(TEXT("SetVoxel:%s:%lld,%lld,%lld:%d"), *Script.ModId, Write.ChunkX, Write.ChunkY, Write.ChunkZ, Write.Voxel.Type));
		return true;
	}

	virtual void ShowHud_Implementation(const FCrowdyModScript& Script, const FString& Text) override
	{
		Lines.Add(FString::Printf(TEXT("ShowHud:%s:%s"), *Script.ModId, *Text));
	}

	virtual void DrawOverlay_Implementation(const FCrowdyModScript& Script, const TArray<FCrowdyModScriptShape>& Shapes) override
	{
		FString Line = FString::Printf(TEXT("DrawOverlay:%s"), *Script.ModId);
		for (const FCrowdyModScriptShape& Shape : Shapes)
		{
			Line += FString::Printf(TEXT(":%s %s %.0f,%.0f,%.0f"), *Shape.Id, *StaticEnum<ECrowdyModScriptShapeKind>()->GetNameStringByValue(static_cast<int64>(Shape.Kind)), Shape.Location.X, Shape.Location.Y, Shape.Location.Z);
		}
		Lines.Add(Line);
	}

	virtual void ClearScript_Implementation(const FCrowdyModScript& Script) override
	{
		Lines.Add(FString::Printf(TEXT("ClearScript:%s"), *Script.ModId));
	}

	UFUNCTION()
	void OnPrompt(const FCrowdyModScriptPrompt& Prompt)
	{
		Lines.Add(FString::Printf(TEXT("Prompt:%d"), Prompt.PromptId));
		if (UStandInC10* StandIn = AnswerFrom.Get())
		{
			StandIn->AnswerPrompt(Prompt.PromptId, true);
		}
	}

	UFUNCTION()
	void OnPromptWithdrawn(int32 PromptId)
	{
		Lines.Add(FString::Printf(TEXT("Withdrawn:%d"), PromptId));
	}

	UFUNCTION()
	void OnStarted(const FCrowdyModScript& Script)
	{
		Lines.Add(FString::Printf(TEXT("Started:%s#%d"), *Script.ModId, Script.Generation));
	}

	UFUNCTION()
	void OnStopped(const FCrowdyModScript& Script, ECrowdyModScriptStop Reason)
	{
		Lines.Add(FString::Printf(TEXT("Stopped:%s:%s"), *Script.ModId, *StaticEnum<ECrowdyModScriptStop>()->GetNameStringByValue(static_cast<int64>(Reason))));
	}

	UFUNCTION()
	void OnError(const FCrowdyModScriptError& Error)
	{
		Lines.Add(FString::Printf(TEXT("Error:%s:%s"), *Error.ModId, *Error.Code));
	}

	TArray<FString> Lines;

	/** When set, every prompt is answered Run from inside its own event. */
	TWeakObjectPtr<UStandInC10> AnswerFrom;
};
