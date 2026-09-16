#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "KasumiSplatRenderer.h"

class FKasumiSplatRuntimeModule : public IModuleInterface
{
    FDelegateHandle EngineInitHandle;
public:
    virtual void StartupModule() override
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("KasumiSplat"));
        check(Plugin.IsValid());
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/KasumiSplat"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
        EngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddStatic(&KasumiSplat::StartRenderer);
    }
    virtual void ShutdownModule() override
    {
        FCoreDelegates::GetOnPostEngineInit().Remove(EngineInitHandle);
        KasumiSplat::StopRenderer();
    }
    virtual bool SupportsDynamicReloading() override { return false; }
};

IMPLEMENT_MODULE(FKasumiSplatRuntimeModule, KasumiSplatRuntime)
