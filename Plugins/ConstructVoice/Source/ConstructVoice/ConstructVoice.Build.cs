using UnrealBuildTool;

public class ConstructVoice : ModuleRules
{
    public ConstructVoice(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "CrowdyVoice",
            "Engine",
            "InputCore",
            "UMG"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ConstructShell",
            "CrowdyNet",
            "CrowdySDK",
            "CrowdyServices",
            "EnhancedInput",
            "Slate",
            "SlateCore"
        });
    }
}
