// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"

class SDockTab;
class SWatabouImportPanel;
class FSpawnTabArgs;
class FEditorWatabouImporter;
class FAssetTypeActions_WatabouAsset;
class FWatabouBrowserPool;
class FWatabouRecursiveImporter;
class FWatabouBatchNotifier;
struct FWatabouSeedRef;

/**
 * Editor-side module for the Watabou bridge.
 *
 * On startup:
 *   1. Spins up FWatabouBundleHttpServer on 127.0.0.1:<random>, serving each generator
 *      from its resolved source -- downloaded cache or the shipped Resources/Bundles/
 *      snapshots (see FWatabouBundleHttpServer for the priority rules).
 *   2. Registers a Nomad tab spawner with the global tab manager.
 *   3. Adds "Watabou Import Window" under the editor's Tools menu.
 *   4. Registers FEditorWatabouImporter as the active IWatabouImporter so
 *      PCG nodes / cache-miss resolutions can drive the bridge.
 *
 * Console commands:
 *   WatabouBridge.Status
 *   WatabouBridge.Restart
 *   WatabouBridge.Open
 */
class FWatabouBridgeEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	uint16 GetServerPort() const;
	bool IsServerRunning() const;

	/** True if at least one route was bound for the given generator id on the active server. */
	bool HasRoutesForGenerator(const FString& GeneratorId) const;

	/** Stop and re-start the HTTP server. Used by the panel after the resource updater finishes
	 *  so the freshly cached bundles are picked up automatically, and as a recovery step in
	 *  the panel's ApplyAndLoad when routes for the active generator are missing. */
	void Restart();

	static FWatabouBridgeEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FWatabouBridgeEditorModule>("WatabouBridgeEditor");
	}

	static const FName ImportTabId;

	/** The currently-spawned import panel, set by SpawnImportTab. The panel is a thin
	 *  observer over the module-owned pool + recursive importer; asset right-click
	 *  actions no longer go through it. */
	TWeakPtr<SWatabouImportPanel> GetActivePanel() const { return ActivePanel; }

	/** Module-lifetime browser pool. Hidden host window keeps slots ticking while
	 *  the panel is closed. Created in OnPostEngineInit (after Slate is up). */
	TSharedPtr<FWatabouBrowserPool> GetBrowserPool() const { return BrowserPool; }

	/** Module-lifetime recursive importer. Feeds the pool; survives panel close so
	 *  batches keep running when the user closes the convenience window. */
	TSharedPtr<FWatabouRecursiveImporter> GetRecursiveImporter() const { return RecursiveImporter; }

private:
	void StartServer();
	void StopServer();

	void RegisterConsoleCommands();
	void UnregisterConsoleCommands();

	void RegisterTab();
	void UnregisterTab();

	void RegisterToolMenus();
	void UnregisterToolMenus();

	TSharedRef<SDockTab> SpawnImportTab(const FSpawnTabArgs& Args);

	/** Bound to FWatabouEditorBridge::OnOpenInImporter: spawn-or-focus the import tab and drive its
	 *  panel to the asset's generator + settings (restored for the session, not persisted). */
	void OpenInImporter(const FWatabouSeedRef& Seed);

	TSharedPtr<class FWatabouBundleHttpServer> Server;
	TSharedPtr<FEditorWatabouImporter> Importer;
	TSharedPtr<FAssetTypeActions_WatabouAsset> WatabouAssetActions;
	TSharedPtr<FWatabouBrowserPool> BrowserPool;
	TSharedPtr<FWatabouRecursiveImporter> RecursiveImporter;
	TSharedPtr<FWatabouBatchNotifier> BatchNotifier;
	TWeakPtr<SWatabouImportPanel> ActivePanel;
	TArray<IConsoleObject*> ConsoleCommands;
};
