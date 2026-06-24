#include "Modules/ModuleManager.h"

class FVRPerfCaptureModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FVRPerfCaptureModule, VRPerfCapture)