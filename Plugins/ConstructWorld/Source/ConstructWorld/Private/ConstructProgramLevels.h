#pragma once

#include "CoreMinimal.h"

namespace ConstructProgramLevels
{
    inline constexpr TCHAR Holodeck[] = TEXT("/Game/Construct/Maps/L_ConstructHolodeck.L_ConstructHolodeck");

    struct FProgramLevel
    {
        const TCHAR* SceneId;
        const TCHAR* Map;
    };

    inline constexpr FProgramLevel Programs[] = {
        {TEXT("paint"), TEXT("/Game/Construct/Maps/L_ConstructPaint.L_ConstructPaint")}
    };
}
