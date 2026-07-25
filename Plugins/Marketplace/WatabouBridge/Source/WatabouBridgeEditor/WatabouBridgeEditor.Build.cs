// Copyright 2026 Timothé Lapetite

using UnrealBuildTool;

public class WatabouBridgeEditor : ModuleRules
{
	public WatabouBridgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"WatabouBridge",         // LogWatabou
			"WatabouCore",           // FWatabouGeneratorRegistry, IWatabouImporter
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",                 // IPluginManager
			"UnrealEd",
			"Slate",
			"SlateCore",
			"InputCore",
			"ApplicationCore",          // FPlatformApplicationMisc::ClipboardCopy
			"WebBrowser",               // SWebBrowser, IWebBrowserWindow
			"HTTP",
			"HTTPServer",               // FHttpServerModule, IHttpRouter
			"Json",
			"JsonUtilities",
			"PropertyEditor",           // IStructureDetailsView for the per-generator settings panel
			"ToolMenus",                // UToolMenus
			"WorkspaceMenuStructure",   // Nomad tab category
			"MainFrame",
			"AssetRegistry",            // FAssetRegistryModule::AssetCreated (post-save notification)
			"AssetTools",               // FAssetTypeActions_Base, IAssetTools
			"ImageWrapper",             // WatabouAssetBuilderHooks: PNG decode for thumbnail blobs
		});
	}
}
