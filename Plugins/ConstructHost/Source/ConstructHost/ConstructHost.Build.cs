using UnrealBuildTool;

public class ConstructHost : ModuleRules
{
    public ConstructHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "CrowdyServices",
            "Engine",
            "Json"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "JsonUtilities"
        });
    }
}
