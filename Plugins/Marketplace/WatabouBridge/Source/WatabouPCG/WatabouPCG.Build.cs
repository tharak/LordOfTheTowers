// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouPCG : ModuleRules
{
	public WatabouPCG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PCG",            // Consumption target: raw PCG framework (NOT PCGEx)
			"WatabouBridge",  // LogWatabou
			"WatabouCore",    // Data model, generator registry, seed cache
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// UE GeometryProcessing (NOT PCGEx): minimum-volume oriented box for footprint reduction.
			// Replicates the algorithm PCGEx FBestFitPlane uses in precise mode without linking PCGEx.
			"GeometryCore",       // UE::Geometry::FOrientedBox3d / TFrame3d (OrientedBoxTypes.h)
			"GeometryAlgorithms", // UE::Geometry::TMinVolumeBox3 (MinVolumeBox3.h)
		});

		// NB: This module deliberately does NOT depend on PCGExtendedToolkit. The only
		// "PCGEx-friendly" surface we need downstream (closed-loop / hole @Data attributes,
		// one-data-per-path layout) is reproduced locally in WatabouPCGData so paths emitted
		// here are recognized by PCGEx path nodes without linking PCGEx.
	}
}
