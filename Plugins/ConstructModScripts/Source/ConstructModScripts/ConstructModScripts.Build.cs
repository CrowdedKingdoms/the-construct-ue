using UnrealBuildTool;

public class ConstructModScripts : ModuleRules
{
    public ConstructModScripts(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Public host signatures and runtime binding use the SDK service module.
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "CrowdyServices",
            "Engine",
            "InputCore",
            "SlateCore",
            "UMG",
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ApplicationCore",
            "ConstructPresence",
            "ConstructShell",
            "ConstructWorld",
            "CrowdyNet",
            "CrowdyReplication",
            "CrowdySDK",
            "RenderCore",
            "RHI",
            "Slate",
        });
    }
}
