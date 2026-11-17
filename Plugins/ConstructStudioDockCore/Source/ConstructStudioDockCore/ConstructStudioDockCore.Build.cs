using System.IO;
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
            "ConstructPresence",
            "ConstructShell",
            "CrowdyNet",
            "CrowdyReplication",
            "CrowdyServices",
            "InputCore",
            "JsonUtilities",
            "Projects",
            "Slate",
            "WebBrowser"
        });
        RuntimeDependencies.Add("$(PluginDir)/Resources/StudioDock/...", StagedFileType.NonUFS);
        string HarnessBuild = Path.Combine(PluginDirectory, "Resources", "StudioDock", "dsh", "BUILD.json");
        if (File.Exists(HarnessBuild))
        {
            ExternalDependencies.Add(HarnessBuild);
        }
    }
}
