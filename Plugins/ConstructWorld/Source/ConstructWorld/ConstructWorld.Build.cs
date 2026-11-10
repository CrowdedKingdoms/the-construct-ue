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
            "Engine"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CrowdyExec",
            "CrowdyNet",
            "CrowdySDK"
        });
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("AssetRegistry");
        }
    }
}
