using UnrealBuildTool;

public class TheConstructTarget : TargetRules
{
	public TheConstructTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TheConstruct");
	}
}
