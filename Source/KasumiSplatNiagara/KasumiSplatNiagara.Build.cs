using UnrealBuildTool;

public class KasumiSplatNiagara : ModuleRules
{
    public KasumiSplatNiagara(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "KasumiSplatRuntime",
            "Niagara"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "RenderCore",
            "RHI",
            "Renderer"
        });
    }
}
