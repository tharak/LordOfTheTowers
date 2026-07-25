// Copyright 2026 Timothé Lapetite

#include "WatabouBridgeEditorModule.h"
#include "WatabouBridge.h"
#include "WatabouBundleHttpServer.h"
#include "EditorWatabouImporter.h"
#include "SWatabouImportPanel.h"
#include "IWatabouImporter.h"
#include "WatabouEditorBridge.h"
#include "WatabouSeedRef.h"
#include "AssetTypeActions_WatabouAsset.h"
#include "WatabouAssetBuilderHooks.h"
#include "WatabouBatchNotifier.h"
#include "WatabouBrowserPool.h"
#include "WatabouRecursiveImporter.h"
#include "WatabouBridgeEditorStyle.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "ToolMenus.h"
#include "Textures/SlateIcon.h"
#include "Styling/AppStyle.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

#define LOCTEXT_NAMESPACE "WatabouBridgeEditor"

const FName FWatabouBridgeEditorModule::ImportTabId(TEXT("WatabouBridgeImport"));

void FWatabouBridgeEditorModule::StartupModule()
{
	// Style set first: a restored editor layout can spawn the import tab during startup, before
	// OnPostEngineInit, so the rail/icon brushes must already be registered by then.
	FWatabouBridgeEditorStyle::Register();

	// Register editor-side hooks first -- Core's BuildAssetFromExport calls these
	// when the panel/pool dispatch through the bridge. Safe to register before
	// the pool exists; the pool is constructed in OnPostEngineInit below.
	WatabouAssetBuilderHooks::Register();

	RegisterConsoleCommands();
	RegisterTab();
	RegisterToolMenus();

	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		WatabouAssetActions = MakeShared<FAssetTypeActions_WatabouAsset>();
		AssetTools.RegisterAssetTypeActions(WatabouAssetActions.ToSharedRef());
	}

	Importer = MakeShared<FEditorWatabouImporter>();
	IWatabouImporter::SetActive(Importer.Get());

	// Editor-UI hook fired by UWatabouAssetBase::OpenInImportWindow (CallInEditor button).
	FWatabouEditorBridge::OnOpenInImporter.BindRaw(this, &FWatabouBridgeEditorModule::OpenInImporter);

	// Defer everything that depends on Slate or other modules to OnPostEngineInit:
	//   - SWindow construction (pool's hidden host) needs Slate -- though the
	//     pool now lazy-creates the host window on first Enqueue, so this is
	//     only about the pool's recursive-importer wiring below
	//   - Per-generator modules also load at LoadingPhase::Default and register their
	//     FXxxInfo in their own StartupModule. Starting the server now would race them
	//     -- generators whose modules haven't registered yet end up with zero bound
	//     routes, manifesting as window.__hx not exposed (their bundle JS silently
	//     404s, the shim fires against the 404 page, never sees the patch). Auto-
	//     restart from Update Generators recovers for the rest of the session.
	FCoreDelegates::GetOnPostEngineInit().AddLambda([this]()
	{
		// Pool is constructed dormant. EnsureInitialized() is deferred to the
		// first Enqueue() call -- we don't want a ghost "Watabou Browser Pool"
		// window hanging off the editor for the whole session when most users
		// won't import anything in a given run.
		BrowserPool = MakeShared<FWatabouBrowserPool>();

		RecursiveImporter = MakeShared<FWatabouRecursiveImporter>(BrowserPool.ToSharedRef());
		RecursiveImporter->Initialize();

		// Back-wire: pool's MaybeAutoShutdown predicate needs to read the importer's
		// IsRunning() to avoid tearing down between batch waves. Done here (not in
		// the pool ctor) because the importer holds a TWeakPtr to the pool, so the
		// pool has to exist first.
		BrowserPool->SetRecursiveImporter(RecursiveImporter.ToSharedRef());

		BatchNotifier = MakeShared<FWatabouBatchNotifier>();
		BatchNotifier->Initialize(RecursiveImporter.ToSharedRef());

		StartServer();
	});
}

void FWatabouBridgeEditorModule::ShutdownModule()
{
	if (BatchNotifier.IsValid())  { BatchNotifier->Shutdown(); BatchNotifier.Reset(); }
	RecursiveImporter.Reset();
	if (BrowserPool.IsValid())    { BrowserPool->Shutdown();   BrowserPool.Reset(); }

	FWatabouEditorBridge::OnOpenInImporter.Unbind();

	IWatabouImporter::SetActive(nullptr);
	Importer.Reset();

	if (WatabouAssetActions.IsValid() && FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.UnregisterAssetTypeActions(WatabouAssetActions.ToSharedRef());
		WatabouAssetActions.Reset();
	}

	UnregisterToolMenus();
	UnregisterTab();
	UnregisterConsoleCommands();
	WatabouAssetBuilderHooks::Unregister();
	StopServer();

	FWatabouBridgeEditorStyle::Unregister();
}

uint16 FWatabouBridgeEditorModule::GetServerPort() const
{
	return Server.IsValid() ? Server->GetPort() : 0;
}

bool FWatabouBridgeEditorModule::IsServerRunning() const
{
	return Server.IsValid() && Server->IsRunning();
}

void FWatabouBridgeEditorModule::StartServer()
{
	if (Server.IsValid() && Server->IsRunning())
	{
		UE_LOG(LogWatabou, Warning, TEXT("Server already running on port %u"), Server->GetPort());
		return;
	}

	Server = MakeShared<FWatabouBundleHttpServer>();
	if (Server->Start(0))
	{
		UE_LOG(LogWatabou, Log, TEXT("Local bundle server started on http://127.0.0.1:%u"), Server->GetPort());
	}
	else
	{
		UE_LOG(LogWatabou, Error, TEXT("Failed to start local bundle server"));
		Server.Reset();
	}
}

void FWatabouBridgeEditorModule::StopServer()
{
	if (Server.IsValid())
	{
		Server->Stop();
		Server.Reset();
	}
}

void FWatabouBridgeEditorModule::Restart()
{
	StopServer();
	StartServer();
}

bool FWatabouBridgeEditorModule::HasRoutesForGenerator(const FString& GeneratorId) const
{
	return Server.IsValid() && Server->HasRoutesForGenerator(GeneratorId);
}

// ----------------
// Console commands
// ----------------

void FWatabouBridgeEditorModule::RegisterConsoleCommands()
{
	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("WatabouBridge.Status"),
		TEXT("Print the WatabouBridge local HTTP server status"),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			if (IsServerRunning())
			{
				UE_LOG(LogWatabou, Log, TEXT("Running on http://127.0.0.1:%u"), Server->GetPort());
			}
			else
			{
				UE_LOG(LogWatabou, Log, TEXT("Stopped"));
			}
		}),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("WatabouBridge.Restart"),
		TEXT("Restart the WatabouBridge local HTTP server"),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			StopServer();
			StartServer();
		}),
		ECVF_Default));

	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("WatabouBridge.Open"),
		TEXT("Open the Watabou import window"),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FWatabouBridgeEditorModule::ImportTabId);
		}),
		ECVF_Default));
}

void FWatabouBridgeEditorModule::UnregisterConsoleCommands()
{
	for (IConsoleObject* Cmd : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Cmd);
	}
	ConsoleCommands.Empty();
}

// ----------------
// Tab spawner
// ----------------

void FWatabouBridgeEditorModule::RegisterTab()
{
	const TSharedRef<FWorkspaceItem> MenuStructure = WorkspaceMenu::GetMenuStructure().GetToolsCategory();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ImportTabId,
		FOnSpawnTab::CreateRaw(this, &FWatabouBridgeEditorModule::SpawnImportTab))
		.SetDisplayName(LOCTEXT("ImportTabTitle", "Watabou Import"))
		.SetTooltipText(LOCTEXT("ImportTabTooltip", "Embedded Watabou generator with one-click JSON import."))
		.SetGroup(MenuStructure)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"));
}

void FWatabouBridgeEditorModule::UnregisterTab()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ImportTabId);
	}
}

TSharedRef<SDockTab> FWatabouBridgeEditorModule::SpawnImportTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SWatabouImportPanel> Panel = SNew(SWatabouImportPanel);
	ActivePanel = Panel;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("ImportTabTitle", "Watabou Import"))
		[ Panel ];
}

void FWatabouBridgeEditorModule::OpenInImporter(const FWatabouSeedRef& Seed)
{
	// Spawn-or-focus the import tab. A fresh spawn runs SpawnImportTab (which sets ActivePanel); an
	// already-open tab keeps its existing panel. Either way ActivePanel is valid afterward.
	FGlobalTabmanager::Get()->TryInvokeTab(ImportTabId);

	if (TSharedPtr<SWatabouImportPanel> Panel = ActivePanel.Pin())
	{
		// Restore into the tab for the session only (bPersist=false leaves the generator's saved
		// last-used settings untouched). OpenSeedRef handles the freshly-spawned-vs-already-open
		// browser-realization timing.
		Panel->OpenSeedRef(Seed, /*bPersist=*/false);
	}
	else
	{
		UE_LOG(LogWatabou, Warning,
			TEXT("OpenInImporter: import tab did not yield a panel -- generator '%s' not shown."),
			*Seed.GeneratorId);
	}
}

// ----------------
// Tools menu entry
// ----------------

void FWatabouBridgeEditorModule::RegisterToolMenus()
{
	FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
	{
		UToolMenus* ToolMenus = UToolMenus::Get();
		if (!ToolMenus) { return; }

		UToolMenu* Menu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (!Menu) { return; }

		FToolMenuSection& Section = Menu->FindOrAddSection("WatabouBridge",
			LOCTEXT("WatabouBridgeSection", "Watabou"));

		Section.AddMenuEntry(
			"OpenWatabouImport",
			LOCTEXT("OpenWatabouImport", "Watabou Bridge"),
			LOCTEXT("OpenWatabouImportTip", "Open the embedded Watabou generator and JSON importer."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import"),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				FGlobalTabmanager::Get()->TryInvokeTab(FWatabouBridgeEditorModule::ImportTabId);
			})));
	});
}

void FWatabouBridgeEditorModule::UnregisterToolMenus()
{
	if (UObjectInitialized())
	{
		if (UToolMenus* ToolMenus = UToolMenus::Get())
		{
			ToolMenus->RemoveSection("LevelEditor.MainMenu.Tools", "WatabouBridge");
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FWatabouBridgeEditorModule, WatabouBridgeEditor)
