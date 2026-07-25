// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;
using System.IO;

public class WatabouCore : ModuleRules
{
	public WatabouCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"WatabouBridge",       // LogWatabou
			"HTTP",
			"DeveloperSettings"    // UWatabouBridgeSettings (Project Settings -> Plugins -> Watabou Bridge)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
			"JsonUtilities",
			"AssetRegistry",       // FWatabouSeedCache uses asset registry queries
			"Projects",            // IPluginManager -- resolve <Plugin>/Content/Bundles cache root
		});

		// NB: Core deliberately does NOT depend on UnrealEd / ImageWrapper / Slate / etc.
		// Editor-only operations (thumbnail caching, AssetRegistry::AssetCreated, deferred
		// SavePackage) go through WatabouAssetBuilder::FHostHooks -- the editor module
		// registers TFunctions at StartupModule. This keeps Core runtime-clean for the
		// future UWebBrowser-driven runtime path.

		// Stage the versioned bundle cache with packaged builds so runtime JIT (when added)
		// has access to the same data the editor uses. NonUFS = raw file copy at cook time.
		// Wildcard "..." matches all files recursively under Content/Bundles.
		//
		// Path is computed from ModuleDirectory (<Plugin>/Source/WatabouCore) -> two levels up
		// is the plugin root. Content/Bundles/ may not exist at cook time (user hasn't run
		// Update Generators yet) -- UBT tolerates missing wildcards gracefully.
		string PluginRootPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		RuntimeDependencies.Add(
			Path.Combine(PluginRootPath, "Content", "Bundles", "..."),
			StagedFileType.NonUFS);

		// Also stage the shipped bundle snapshots (pre-patched copies redistributed with watabou's
		// permission -- see Resources/Bundles/NOTICE.md, which ships along via this wildcard) so
		// the resolver can reach them at runtime. Resources/ is NOT staged by default; this
		// mirrors the Content/Bundles entry so the shipped tier works in packaged builds without
		// copying the tree into Content.
		RuntimeDependencies.Add(
			Path.Combine(PluginRootPath, "Resources", "Bundles", "..."),
			StagedFileType.NonUFS);
	}
}
