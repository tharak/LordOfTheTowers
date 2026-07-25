// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouPerilousShores : ModuleRules
{
	public WatabouPerilousShores(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"WatabouBridge",
			"WatabouCore",
			"Json",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"WatabouPCG",
		});
	}
}
