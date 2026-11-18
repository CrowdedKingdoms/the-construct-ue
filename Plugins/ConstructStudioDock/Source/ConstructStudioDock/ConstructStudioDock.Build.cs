using UnrealBuildTool;

public class ConstructStudioDock : ModuleRules
{
    public ConstructStudioDock(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "ConstructStudioDockCore",
            "Core",
            "CoreUObject",
            "Engine",
            "UMG",
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ApplicationCore",
            "ConstructShell",
            "ConstructWorld",
            "CrowdyServices",
            "ImageCore",
            "ImageWrapper",
            "InputCore",
            "RenderCore",
            "RHI",
            "Slate",
            "SlateCore",
        });
    }
}

