using UnrealBuildTool;

public class ConstructHarness : ModuleRules
{
	public ConstructHarness(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"CrowdyServices",
			"Engine",
		});
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"CrowdyNet",
			"CrowdySDK",
		});
	}
}
