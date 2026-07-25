// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouVillageGenerator : ModuleRules
{
	public WatabouVillageGenerator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"WatabouBridge",
			"WatabouCore",
			"WatabouDwellings",
			"Json",
		});
	}
}
