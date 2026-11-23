#include "ConstructWebcamLog.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogConstructWebcam);

static TAutoConsoleVariable<bool> CVarConstructWebcamTrace(TEXT("construct.webcam.trace"),
                                                          false,
                                                          TEXT("Logs webcam state changes and frame counts."));

bool IsConstructWebcamTraceOn()
{
	return CVarConstructWebcamTrace.GetValueOnAnyThread();
}

IMPLEMENT_MODULE(FDefaultModuleImpl, ConstructWebcam)
