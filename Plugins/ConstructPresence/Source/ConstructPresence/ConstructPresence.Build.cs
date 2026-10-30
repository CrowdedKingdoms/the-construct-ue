using UnrealBuildTool;

public class ConstructPresence : ModuleRules
{
    public ConstructPresence(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "ConstructShell",
            "Core",
            "CoreUObject",
            "CrowdyNet",
            "CrowdyReplication",
            "CrowdyServices",
            "Engine",
            "UMG",
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CrowdySDK",
            "EnhancedInput",
            "InputCore",
            "Slate",
            "SlateCore",
        });
    }
}
