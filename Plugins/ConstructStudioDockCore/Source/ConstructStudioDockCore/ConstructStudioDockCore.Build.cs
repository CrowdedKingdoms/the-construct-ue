using UnrealBuildTool;

public class ConstructStudioDockCore : ModuleRules
{
    public ConstructStudioDockCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "SlateCore",
            "UMG"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "CrowdyNet",
            "CrowdyServices",
            "InputCore",
            "JsonUtilities",
            "Slate"
        });
    }
}
