using UnrealBuildTool;

public class ConstructChat : ModuleRules
{
    public ConstructChat(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "CrowdyNet",
            "CrowdyServices",
            "Engine",
            "SlateCore",
            "UMG"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ConstructPresence",
            "ConstructShell",
            "CrowdyReplication",
            "CrowdySDK",
            "InputCore",
            "Slate"
        });
    }
}
