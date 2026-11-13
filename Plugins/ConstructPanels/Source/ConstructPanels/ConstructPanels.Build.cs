using UnrealBuildTool;

public class ConstructPanels : ModuleRules
{
    public ConstructPanels(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "ConstructPresence",
            "ConstructShell",
            "ConstructWebcam",
            "Core",
            "CoreUObject",
            "CrowdyServices",
            "Engine",
            "SlateCore",
            "UMG",
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CrowdyNet",
            "CrowdyReplication",
            "CrowdySDK",
            "ImageCore",
            "InputCore",
            "RenderCore",
            "RHI",
            "Slate",
        });
    }
}
