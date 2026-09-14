using UnrealBuildTool;

public class PirateSeasEditorTarget : TargetRules
{
	public PirateSeasEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PirateSeas");
	}
}
