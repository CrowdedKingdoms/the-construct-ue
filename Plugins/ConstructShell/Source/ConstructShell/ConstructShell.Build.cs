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
            "SlateCore",
            "UMG"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "Projects", "EnhancedInput", "InputCore", "CrowdySDK", "CrowdyNet", "CrowdyReplication" });
        RuntimeDependencies.Add("$(PluginDir)/Resources/Icons/*.svg", StagedFileType.UFS);
    }
}
