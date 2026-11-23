#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "ConstructProgramLevels.h"
#include "GameDelegates.h"
#include "UObject/SoftObjectPath.h"
#endif

class FConstructWorldModule final : public IModuleInterface
{
#if WITH_EDITOR
public:
    virtual void StartupModule() override
    {
        CookHandle = FGameDelegates::Get().GetModifyCookDelegate().AddStatic(&AddProgramMaps);
    }

    virtual void ShutdownModule() override
    {
        FGameDelegates::Get().GetModifyCookDelegate().Remove(CookHandle);
    }

private:
    static void AddProgramMaps(TConstArrayView<const ITargetPlatform*>, TArray<FName>& Packages, TArray<FName>&)
    {
        // Local travel uses a fixed program table, so the cooker needs its maps as explicit roots.
        Packages.AddUnique(FSoftObjectPath(ConstructProgramLevels::Holodeck).GetLongPackageFName());
        for (const ConstructProgramLevels::FProgramLevel& Program : ConstructProgramLevels::Programs)
        {
            Packages.AddUnique(FSoftObjectPath(Program.Map).GetLongPackageFName());
        }
    }

    FDelegateHandle CookHandle;
#endif
};

IMPLEMENT_MODULE(FConstructWorldModule, ConstructWorld)
