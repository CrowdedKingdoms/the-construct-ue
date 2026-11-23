#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/** Shared runtime visual language for the shell and its feature panels. */
class CONSTRUCTSHELL_API FConstructShellStyle
{
public:
	static const ISlateStyle& Get();
	static FLinearColor Gold();
	static FLinearColor Ink();
	static FLinearColor Muted();
};
