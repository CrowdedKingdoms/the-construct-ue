#pragma once

#include "CoreMinimal.h"

/** Semantic marker for scene content that must never be shared in an agent screenshot. */
namespace ConstructCapturePrivacy
{
inline const FName ExcludeTag(TEXT("Construct.Capture.Exclude"));
}
