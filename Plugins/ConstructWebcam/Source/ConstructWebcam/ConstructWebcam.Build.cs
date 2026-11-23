using UnrealBuildTool;

public class ConstructWebcam : ModuleRules
{
    public ConstructWebcam(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "CKSharedTypes",
            "Core",
            "CoreUObject",
            "CrowdyNet",
            "CrowdyReplication",
            "CrowdyServices",
            "Engine"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ConstructPresence",
            "ConstructShell",
            "CrowdySDK",
            "ImageCore",
            "ImageWrapper",
            "Media",
            "MediaAssets",
            "MediaUtils",
            "RHI"
        });
    }
}
