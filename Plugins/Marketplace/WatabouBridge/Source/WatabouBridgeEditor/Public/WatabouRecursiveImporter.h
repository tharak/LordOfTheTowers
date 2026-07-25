// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouBrowserSlot.h"   // FOnWatabouSlotImportComplete
#include "WatabouSeedRef.h"

class FWatabouBrowserPool;
class UWatabouAssetBase;

/**
 * Walks a UWatabouAssetBase's nested FWatabouSeedRefs (e.g. PerilousShores
 * town/danger, City/Village/Neighbourhood/UrbanPlaces dwellings) and feeds
 * them through FWatabouBrowserPool until the queue drains. Dedups by
 * FWatabouSeedRef::GetCanonicalKey() against (a) refs already processed in
 * this batch and (b) assets already on disk via FWatabouSeedCache::Find.
 *
 * Termination: in-flight set empty + queue empty. No depth limit.
 *
 * Module-lifetime singleton owned by FWatabouBridgeEditorModule. Survives the
 * import panel open/close so batches keep running while the panel is hidden.
 */
class WATABOUBRIDGEEDITOR_API FWatabouRecursiveImporter
	: public TSharedFromThis<FWatabouRecursiveImporter>
{
public:
	enum class EOnFailure : uint8 { Continue, Abort };

	enum class EBatchState : uint8
	{
		Idle,
		Running,
		Cancelling,  // Abort requested -- finish in-flight imports, then stop without recursing
		Done,
	};

	struct FSettings
	{
		/** Restrict recursion to refs whose Ref.GeneratorId is in this set. Empty = no filter (everything). */
		TSet<FName>  AllowedTargetGenerators;

		/** When true, re-import refs even if an asset with matching CanonicalKey is already on disk. */
		bool         bForceReimport = false;

		/** Behavior when a single ref import fails. */
		EOnFailure   OnFailure = EOnFailure::Continue;
	};

	struct FProgress
	{
		EBatchState  State = EBatchState::Idle;
		int32        Done = 0;
		int32        Total = 0;       // grows as imports surface their own refs
		int32        Failed = 0;
		int32        InFlight = 0;    // number of imports currently dispatched to the pool
		FString      LastError;       // most recent failure message (cleared on success)
	};

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnProgressChanged, const FProgress&);
	FOnProgressChanged OnProgressChanged;

	/** Fired when the importer transitions out of Running/Cancelling into Done
	 *  (or from Idle into Done for a no-op batch). The browser pool subscribes
	 *  here so it can re-check teardown -- a batch can end AFTER the pool's last
	 *  slot went idle (Finish() flips state synchronously inside the slot's
	 *  resolve cascade, so HandleSlotIdle's own MaybeAutoShutdown call would
	 *  have seen IsRunning()==true and bailed). */
	DECLARE_MULTICAST_DELEGATE(FOnBecameIdle);
	FOnBecameIdle OnBecameIdle;

	explicit FWatabouRecursiveImporter(const TSharedRef<FWatabouBrowserPool>& InPool);
	~FWatabouRecursiveImporter();

	/** Called by the module after construction to wire pool change notifications.
	 *  Has to happen post-construct so AsShared() works. */
	void Initialize();

	/** Begin a fresh batch using the importer's current Settings. Walks RootAsset
	 *  for refs, seeds the pool, dispatches. RootAsset itself is not re-imported.
	 *  No-op if a batch is already running (logs warning). */
	void StartFromAsset(UWatabouAssetBase* RootAsset);

	/** Continue an in-flight batch by enqueuing refs from a just-imported asset.
	 *  When the batch isn't running, behaves like StartFromAsset. */
	void EnqueueFromAsset(UWatabouAssetBase* Asset);

	/**
	 * Import a single ref outside any batch (used by the asset context-menu
	 * action for "import this seed alone"). Goes straight to Pool->Enqueue with
	 * no dedup / no recursion. OnComplete fires when the slot finishes.
	 */
	void ImportSingleRef(
		const FWatabouSeedRef& Ref,
		const FString& ParentAssetPath,
		FOnWatabouSlotImportComplete OnComplete);

	/** Request cancellation. State goes Running -> Cancelling; queued refs are
	 *  dropped, in-flight slots finish naturally, then state goes Done. */
	void Abort();

	bool IsRunning() const { return Progress.State == EBatchState::Running || Progress.State == EBatchState::Cancelling; }
	const FProgress& GetProgress() const { return Progress; }
	const FSettings& GetSettings() const { return Settings; }

	/** Hot-applied: changes take effect on the next EnqueueChildrenOf call. */
	FSettings& MutableSettings() { return Settings; }

private:
	struct FPendingRef
	{
		FWatabouSeedRef Ref;
		/** Package path of the asset that contains this ref (becomes the child's parent). */
		FString         ParentAssetPath;
	};

	void DispatchPending();
	void HandlePoolChanged();
	void OnRefImported(const FString& Key, UWatabouAssetBase* Asset, const FString& Error);
	bool TryEnqueueRef(const FWatabouSeedRef& Ref, const FString& ParentAssetPath);
	void EnqueueChildrenOf(UWatabouAssetBase* Asset);
	void Finish();
	void BroadcastProgress();

	/** Post-batch: fill the given assets' nested-ref ResolvedAsset pointers (now that all children
	 *  exist) and save the ones that changed. Runs next tick, off the import-complete callstack. */
	void RelinkImportedAssets(const TArray<FSoftObjectPath>& Paths);

	/** Number of refs still waiting to be dispatched (Queue entries at/after QueueHead). */
	int32 PendingCount() const { return Queue.Num() - QueueHead; }

	TWeakPtr<FWatabouBrowserPool>  WeakPool;
	FSettings                      Settings;
	TArray<FPendingRef>            Queue;
	/** Index of the next undispatched entry in Queue. Dispatch advances this rather than
	 *  RemoveAt(0) (which is O(n) per pop -> O(n^2) over a large batch). The consumed prefix
	 *  is dropped only when the queue fully drains, which keeps the scheme safe under the
	 *  re-entrant DispatchPending cascade (Pool::Enqueue -> OnChanged -> HandlePoolChanged). */
	int32                          QueueHead = 0;
	TSet<FString>                  KeysSeen;       // in-batch + asset-registry dedup
	TMap<FString, FPendingRef>     InFlightByKey;  // multiple slots can run in parallel
	/** Every asset touched this batch (root + each import) -- relinked once the batch drains. */
	TSet<FSoftObjectPath>          AssetsToRelink;
	FProgress                      Progress;
};
