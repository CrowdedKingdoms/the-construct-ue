using UnrealBuildTool;

public class ConstructHub : ModuleRules
{
    public ConstructHub(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UMG"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ConstructShell",
            "CrowdyExec",
            "CrowdyNet",
            "CrowdySDK",
            "Projects",
            "Slate",
            "SlateCore"
        });
    }
}
