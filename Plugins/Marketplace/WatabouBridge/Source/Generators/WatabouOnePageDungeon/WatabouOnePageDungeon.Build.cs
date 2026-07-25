// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouOnePageDungeon : ModuleRules
{
	public WatabouOnePageDungeon(ReadOnlyTargetRules Target) : base(Target)
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
			// FOnePageDungeonProcessor derives from FWatabouProcessorBase (WatabouPCG). Used only in this
			// module's .cpp via CreateProcessor(); PCG comes transitively as WatabouPCG's public dep.
			"WatabouPCG",
		});
	}
}
