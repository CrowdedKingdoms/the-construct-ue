using UnrealBuildTool;

public class ConstructWebcam : ModuleRules
{
    public ConstructWebcam(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ImageWrapper"
        });
    }
}
