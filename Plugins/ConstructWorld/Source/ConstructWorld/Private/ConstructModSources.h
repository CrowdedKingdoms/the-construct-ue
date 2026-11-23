#pragma once

#include "CoreMinimal.h"
#include "Subsystem/CrowdyMods.h"

namespace ConstructModSources
{
    bool Read(const FString& Directory, TArray<FCrowdyModFile>& Files, FString& Error);
    bool Decode(const TArray<uint8>& Bytes, FString& Content);
    FString Sha1(const FString& Content);
}