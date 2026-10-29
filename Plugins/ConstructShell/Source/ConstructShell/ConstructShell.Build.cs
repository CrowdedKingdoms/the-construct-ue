using UnrealBuildTool;

public class ConstructShell : ModuleRules
{
    public ConstructShell(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "CrowdyServices", "SlateCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "Projects", "EnhancedInput", "InputCore", "CrowdySDK", "CrowdyNet", "CrowdyReplication" });
        RuntimeDependencies.Add("$(PluginDir)/Resources/Icons/*.svg", StagedFileType.UFS);
    }
}
