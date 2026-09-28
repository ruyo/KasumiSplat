using UnrealBuildTool;
using System.IO;

public class KasumiSplatEditor : ModuleRules
{
    public KasumiSplatEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        // Upstream SPZ local names intentionally mirror packed fields.
        CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
        CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "ThirdParty", "SPZ"));
        PrivateDefinitions.Add("SPZ_BUILD_EXTENSIONS=1");
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AssetRegistry",
            "KasumiSplatRuntime",
            "Projects",
            "PropertyEditor",
            "Slate",
            "SlateCore",
            "UnrealEd",
            "zlib"
        });
    }
}
