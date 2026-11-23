#pragma once

#include "CoreMinimal.h"
#include "ConstructModPanel.h"
#include "ConstructModsTestReceiver.generated.h"

UCLASS(Transient, NotBlueprintable)
class UConstructModsTestReceiver : public UObject
{
    GENERATED_BODY()
public:
    TArray<FString> Events;
    FString BuildId;
    TArray<FConstructModFileHash> Hashes;
    TWeakObjectPtr<UConstructModPanel> Panel;
    TFunction<void()> OnEnabled;
    int32 Lines = 0;
    UFUNCTION()
    void Built(ECrowdyModResult Result, ECrowdyModBuildStatus Status, const FString& Id)
    {
        Events.Add(TEXT("build"));
        BuildId = Id;
        if (Panel.IsValid())
        {
            Panel->GetLastBuildFiles(BuildId, Hashes);
        }
    }
    UFUNCTION()
    void Enabled(ECrowdyModResult Result, bool bEnabled, bool bRunning)
    {
        Events.Add(TEXT("enable"));
        if (OnEnabled)
        {
            OnEnabled();
        }
    }
    UFUNCTION()
    void Woke(ECrowdyModResult Result, const FString& Message)
    {
        Events.Add(TEXT("wake"));
    }
    UFUNCTION()
    void Logs(ECrowdyModResult Result, int32 Count)
    {
        Events.Add(TEXT("log"));
        Lines = Count;
    }
    UFUNCTION()
    void Deleted(ECrowdyModResult Result)
    {
        Events.Add(TEXT("delete"));
    }
};