// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/GCObject.h"
#include "Styling/SlateTypes.h"               // FTextBlockStyle for the footer link style member
#include "StructUtils/InstancedStruct.h"      // per-generator settings storage
#include "IWebBrowserWindow.h"                // EWebBrowserConsoleLogSeverity
#include "WatabouRecursiveImporter.h"         // nested FProgress reference in HandleRecursiveProgress
#include "WatabouSeedRef.h"                   // PendingOpenSeed (TOptional member needs the full type)

class SWebBrowser;
class SEditableTextBox;
class SComboButton;
class SMultiLineEditableText;
class SWatabouTagPills;
class IStructureDetailsView;
class UWatabouBridgeJSObject;
class UWatabouAssetBase;
class FWatabouResourceUpdater;
struct FWatabouGeneratorInfo;
struct FWatabouGeneratorSettings;
struct FWatabouTagGroup;
struct FWatabouExportPayload;
struct FPropertyChangedEvent;

/**
 * Lifecycle of an interactive (visible-browser) import. Drives status text and locks out
 * concurrent Import clicks. Recursive batches run headless through the module's browser pool.
 */
enum class EWatabouImportState : uint8
{
	Idle,
	Loading,     // CEF/JS bundle generating the JSON (between trigger and export-received)
	Generating,  // deserialize + tree-build + dwellings encode
	Saving,      // UPackage::Save
	Failed,
};

/**
 * Editor panel: a convenience window over the module-owned browser pool + recursive importer.
 *
 * Four-region layout, left to right:
 *   [ rail: generator tabs + load-from-link + Update ]
 *   [ settings: <name> + Seed(+reroll) + typed params (IStructureDetailsView) + tag pills ]
 *   [ visible SWebBrowser (the render view) ]
 *   [ fixed import controls: Import / recursive / Force / Abort / Slots / status / Open ]
 *
 * Each generator owns an FWatabouGeneratorSettings subclass instance (held in
 * SettingsByGenerator, persisted per-session as a query string via UWatabouImportConfig).
 * Selecting a tab rebinds the settings view and auto-loads; settings are panel-authoritative
 * but the panel also reflects in-browser changes back via OnUrlChanged.
 */
class SWatabouImportPanel : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SWatabouImportPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SWatabouImportPanel();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SWatabouImportPanel"); }

	/**
	 * External entry (used by the asset's "Open in Import Window" action via the editor bridge):
	 * switch to the seed's generator, restore its settings, and load. Defer-aware -- if the embedded
	 * browser isn't realized yet (a freshly-spawned tab), the request is queued and consumed by the
	 * panel's initial load instead of racing it. bPersist=false restores into the tab for the session
	 * without overwriting the generator's saved last-used settings.
	 */
	void OpenSeedRef(const FWatabouSeedRef& Ref, bool bPersist);

private:
	// --- Layout builders ---
	TSharedRef<SWidget> BuildGeneratorRail();
	TSharedRef<SWidget> BuildSettingsColumn();
	TSharedRef<SWidget> BuildImportControls();
	TSharedRef<SWidget> BuildGeneratorFilterRow();   // recursion "Follow" filter (combo content)
	TSharedRef<SWidget> BuildBlockingOverlay();      // full-window scrim shown while a batch OR generator update runs
	TSharedRef<SWidget> BuildFooter();               // full-width Watabou attribution strip pinned to the window bottom

	// --- Blocking overlay ---
	EVisibility GetBlockingOverlayVisibility() const;  // Visible while the recursive importer runs or a generator update is in flight
	FReply OnAbortBatch();                             // overlay's Abort button -> importer Abort() (batch only)

	// --- Generator / settings state ---
	void SetActiveGeneratorById(const FString& GeneratorId, bool bPersist = true);   // switch tab: rebind UI + auto-load
	void BindActiveGeneratorUI();                              // sync: details view + pills + seed box
	FWatabouGeneratorSettings* GetActiveSettings();
	const FWatabouGeneratorSettings* GetActiveSettings() const;
	FString BuildQueryForActiveGenerator() const;
	void PersistActiveSettings();

	/** Restore a seed ref into its generator's settings, switch to that tab, and load. Shared worker
	 *  behind both the "Load from link" dropdown and OpenSeedRef. Assumes the browser is realized;
	 *  callers that might run pre-realization go through OpenSeedRef. */
	void LoadFromSeedRef(const FWatabouSeedRef& Ref, bool bPersist);

	void ApplyAndLoad(bool bPersist = true);   // build URL from active settings, (optionally) persist, navigate the browser
	void PerformInitialLoad();       // one-tick post-Construct load funnel; consumes a queued OpenSeedRef if present
	void RefreshSeedTextBox();
	void RefreshSettingsUI();        // after read-back: seed box + details ForceRefresh

	void OnSeedTextCommitted(const FText& Text, ETextCommit::Type CommitType);
	FReply OnRerollSeed();
	void OnDetailsChanged(const FPropertyChangedEvent& Event);

	// Tag pill callbacks (the panel owns selection truth + enforces mutual exclusion).
	bool IsTagSelected(const FString& InTag) const;
	void ToggleTag(const FWatabouTagGroup& Group, const FString& InTag);

	// --- Browser-driven read-back ---
	void HandleUrlChanged(const FText& NewUrl);

	// --- Buttons ---
	FReply OnImportClicked();
	TSharedRef<SWidget> BuildLoadFromLinkMenu();    // "Link" rail-button dropdown content
	void HandleLoadFromLink(const FString& Url);    // parse a pasted URL -> select generator + LoadFrom + load
	FReply OnCopyOutputClicked();
	FReply OnOpenInBrowserClicked();
	FReply OnUpdateGeneratorsClicked();
	FReply OnBrowseTargetDir();   // content-folder picker for the import target dir

	/** Kick a full resource update for every registered generator. The manual Update button is the
	 *  ONLY download path -- generators otherwise serve from the shipped snapshots / local cache.
	 *  Returns false if nothing was started (already running, or no upstream manifests to fetch). */
	bool StartGeneratorsUpdate();
	void OnUpdaterProgress(int32 Completed, int32 Total);
	void OnUpdaterFinished(int32 Succeeded, int32 Failed, int32 Total);

	// --- Bridge / browser handlers ---
	void HandleLoadCompleted();
	void HandleConsoleMessage(const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity);
	void HandleExportReceived(const FWatabouExportPayload& Payload);
	void HandleBridgeReady(const FString& Message);
	void HandleBridgeError(const FString& Message);

	FString BuildFullURL(const FWatabouGeneratorInfo& Gen, const FString& Query) const;

	void HandleRecursiveProgress(const FWatabouRecursiveImporter::FProgress& Progress);
	void HandlePoolChanged();

	void SetImportState(EWatabouImportState NewState, const FString& Detail = FString());

	bool IsImporting() const
	{
		return ImportState == EWatabouImportState::Loading
			|| ImportState == EWatabouImportState::Generating
			|| ImportState == EWatabouImportState::Saving;
	}

	bool ShouldIgnoreManualClickAsStuck(const TCHAR* ButtonName) const;

	/** Whether the panel's action controls (rail tabs, Import, Update) accept input. False while a
	 *  generator update OR a recursive batch is running, so the blocking overlay's guarantee holds
	 *  even against keyboard focus traversal (the scrim only occludes the pointer). */
	bool GetInteractiveEnabled() const;

	// --- Widgets ---
	TObjectPtr<UWatabouBridgeJSObject> Bridge;
	TSharedPtr<SWebBrowser> WebBrowser;
	TSharedPtr<IStructureDetailsView> StructDetailsView;
	TSharedPtr<SWatabouTagPills> TagPills;
	TSharedPtr<SEditableTextBox> SeedTextBox;
	TSharedPtr<SMultiLineEditableText> OutputField;   // status surface (relocated to the right panel)
	TSharedPtr<SComboButton> LinkComboButton;         // "Link" rail dropdown (load-from-link)
	TSharedPtr<SEditableTextBox> LinkTextBox;          // URL entry inside the Link dropdown
	TSharedPtr<SEditableTextBox> TargetDirTextBox;     // import target dir (editable + Browse)

	/** Small text style for the footer's watabou.github.io hyperlink: NormalText shrunk below the
	 *  default label size. Held as a member so the pointer passed to SHyperlink::TextStyle stays
	 *  valid for the panel's lifetime. */
	FTextBlockStyle FooterLinkTextStyle;

	// --- Per-generator settings ---
	/** One settings instance per generator id, retained across tab switches + persisted. */
	TMap<FName, FInstancedStruct> SettingsByGenerator;
	/** The currently-selected generator (drives the settings view, pills, and loads). */
	TSharedPtr<FWatabouGeneratorInfo> ActiveGenerator;

	FString CurrentURL;
	bool bPendingAutoExport = false;   // visible panel is always manual-import (two-step)

	/** False until PerformInitialLoad has run once (the embedded browser is realized only after the
	 *  first post-Construct tick). OpenSeedRef uses this to decide between applying now and queueing. */
	bool bInitialLoadDone = false;

	/** A seed-ref open request that arrived before the browser was realized (freshly-spawned tab).
	 *  PerformInitialLoad consumes it instead of doing the default load, so the asset's seed -- not
	 *  the restored last-used settings -- drives the first navigation. */
	TOptional<FWatabouSeedRef> PendingOpenSeed;
	bool bPendingOpenPersist = false;   // bPersist for the queued PendingOpenSeed

	/** Read-back guard: armed when the bundle reports ready, so the OnUrlChanged churn from our
	 *  own LoadURL is ignored and only genuine in-browser edits are reflected back into settings. */
	bool bReadbackArmed = false;

	/** Last imported JSON content, kept off-widget for the Copy button. */
	FString LastImportedJson;

	EWatabouImportState ImportState = EWatabouImportState::Idle;
	double LastImportStateChangeSeconds = 0.0;

	/** Master toggle: when true, every successful manual import seeds a recursive batch. */
	bool bRecursiveOnImport = false;

	bool bUpdating = false;
	TSharedPtr<FWatabouResourceUpdater> ActiveUpdater;

	FDelegateHandle ImporterProgressHandle;
	FDelegateHandle PoolChangedHandle;
};
