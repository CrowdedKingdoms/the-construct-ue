using UnrealBuildTool;

public class ConstructHarness : ModuleRules
{
	public ConstructHarness(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[]
		{
			"ConstructHub",
			"ConstructPresence",
			"ConstructShell",
			"Core",
			"CoreUObject",
			"CrowdyExec",
			"CrowdyReplication",
			"CrowdyServices",
			"Engine",
		});
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CrowdyNet",
			"CrowdySDK",
			"CrowdyVoice",
			"Json",
		});
	}
}
