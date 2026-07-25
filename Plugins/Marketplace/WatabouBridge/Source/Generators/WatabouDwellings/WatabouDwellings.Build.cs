// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouDwellings : ModuleRules
{
	public WatabouDwellings(ReadOnlyTargetRules Target) : base(Target)
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
			// FDwellingsProcessor derives from FWatabouProcessorBase (WatabouPCG). Used only in this
			// module's .cpp via CreateProcessor(); PCG comes transitively as WatabouPCG's public dep.
			"WatabouPCG",
		});
	}
}
