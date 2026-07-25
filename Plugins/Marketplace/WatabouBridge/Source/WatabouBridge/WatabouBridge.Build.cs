// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouBridge : ModuleRules
{
	public WatabouBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",   // for future UWatabouBridgeSettings : UDeveloperSettings
			"Json"
		});
	}
}
