// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouNeighbourhood : ModuleRules
{
	public WatabouNeighbourhood(ReadOnlyTargetRules Target) : base(Target)
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
