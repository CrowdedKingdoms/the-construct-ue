using UnrealBuildTool;

public class ConstructWorld : ModuleRules
{
    public ConstructWorld(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "ConstructHub",
            "Core",
            "CoreUObject",
            "CrowdyServices",
            "Engine",
            "UMG"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ConstructShell",
            "CrowdyExec",
            "CrowdyNet",
            "CrowdySDK",
            "InputCore",
            "Slate",
            "SlateCore"
        });
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("AssetRegistry");
        }
    }
}
