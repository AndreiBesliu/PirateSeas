using UnrealBuildTool;

public class PirateSeas : ModuleRules
{
	public PirateSeas(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AIModule",
			"NavigationSystem",
			"PhysicsCore",
			"Water",
			"ProceduralMeshComponent"
		});
	}
}
