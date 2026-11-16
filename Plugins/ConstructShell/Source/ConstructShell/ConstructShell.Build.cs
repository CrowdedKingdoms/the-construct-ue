using UnrealBuildTool;

public class ConstructShell : ModuleRules
{
    public ConstructShell(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "CrowdyServices",
            "DeveloperSettings",
            "Engine",
            "Slate",
            "SlateCore",
            "UMG"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "SlateCore", "Projects", "EnhancedInput", "InputCore", "CrowdySDK", "CrowdyNet", "CrowdyReplication" });
        RuntimeDependencies.Add("$(PluginDir)/Resources/Icons/*.svg", StagedFileType.UFS);
    }
}
