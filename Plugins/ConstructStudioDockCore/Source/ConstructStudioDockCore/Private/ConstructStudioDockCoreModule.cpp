#include "ConstructStudioDockLog.h"
#include "ConstructStudioDockScheme.h"
#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogConstructStudioDock);

class FConstructStudioDockCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The browser starts only once the engine is up; the dock's origin is then served until shutdown.
		if (GEngine)
		{
			ConstructStudioDockScheme::Register();
			return;
		}
		PostEngineInit = FCoreDelegates::GetOnPostEngineInit().AddLambda([]() { ConstructStudioDockScheme::Register(); });
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInit);
		ConstructStudioDockScheme::Unregister();
	}

private:
	FDelegateHandle PostEngineInit;
};

IMPLEMENT_MODULE(FConstructStudioDockCoreModule, ConstructStudioDockCore)
