using UnrealBuildTool;

public class PirateSeasTarget : TargetRules
{
	public PirateSeasTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PirateSeas");
	}
}
