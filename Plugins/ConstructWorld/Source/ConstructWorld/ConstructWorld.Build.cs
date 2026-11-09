using UnrealBuildTool;

public class ConstructWorld : ModuleRules
{
    public ConstructWorld(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "CrowdyServices"
        });
    }
}
