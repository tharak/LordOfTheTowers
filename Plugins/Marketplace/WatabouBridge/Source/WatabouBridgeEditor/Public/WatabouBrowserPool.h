// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouBrowserSlot.h"   // FOnWatabouSlotImportComplete
#include "WatabouSeedRef.h"

class SWindow;
class SHorizontalBox;
class FWatabouRecursiveImporter;
struct FWatabouGeneratorInfo;

/**
 * Module-lifetime pool of hidden FWatabouBrowserSlots. Imports queued via
 * Enqueue() are dispatched to the first idle slot or queued in arrival order.
 * Slots host their SWebBrowser widgets inside a single off-screen module-owned
 * SWindow so they keep ticking regardless of whether the import panel is open.
 *
 * The pool deliberately knows nothing about recursion / dedup / parent-path
 * walking -- that all lives in FWatabouRecursiveImporter which is the pool's
 * primary producer. The asset context-menu action also pushes here directly.
 *
 * Lazy lifecycle: the off-screen host window + slots are NOT created at module
 * startup. The first Enqueue() call materialises them; once all work drains AND
 * the recursive importer reports idle, the host window is torn down. The pool
 * object itself outlives this cycle so observers (panel, batch notifier) can
 * keep their OnChanged bindings across dormancy.
 *
 * Threading: game thread only.
 */
class WATABOUBRIDGEEDITOR_API FWatabouBrowserPool : public TSharedFromThis<FWatabouBrowserPool>
{
public:
	static constexpr int32 MinSlots = 1;
	static constexpr int32 MaxSlots = 8;

	FWatabouBrowserPool();
	~FWatabouBrowserPool();

	/** Idempotent: creates the off-screen host window and spawns DesiredSlotCount
	 *  slots if currently dormant. Called automatically by Enqueue(); also safe to
	 *  call from external producers that want to pre-warm the pool. */
	void EnsureInitialized();

	/** Drain everything: pending queue is cancelled with errors, in-flight slots are
	 *  released (their callbacks may still fire post-Shutdown if they were mid-async,
	 *  which is fine since they no-op on a destroyed pool). Host window is destroyed. */
	void Shutdown();

	/** Wire the back-reference used by MaybeAutoShutdown() to gate teardown on the
	 *  recursive importer being idle. Also subscribes to the importer's
	 *  OnBecameIdle delegate so the pool re-checks teardown when a batch ends
	 *  while the pool is already quiescent. Module-level wiring; call once after
	 *  both objects are constructed. */
	void SetRecursiveImporter(const TSharedRef<FWatabouRecursiveImporter>& InImporter);

	/** Live-resize the slot count. Growing spawns immediately; shrinking moves
	 *  surplus slots to the drain list -- they finish in-flight, then dispose.
	 *  When the pool is dormant, just stashes the value for the next EnsureInitialized(). */
	void SetSlotCount(int32 N);

	int32 GetSlotCount() const         { return HostWindow.IsValid() ? Slots.Num() : DesiredSlotCount; }
	int32 GetBusyCount() const;
	int32 GetIdleCount() const         { return GetSlotCount() - GetBusyCount(); }
	int32 GetQueueDepth() const        { return Queue.Num(); }
	bool  IsIdle() const               { return GetBusyCount() == 0 && Queue.Num() == 0; }
	bool  IsDormant() const            { return !HostWindow.IsValid(); }

	/**
	 * Enqueue a single import. Routes to an idle slot if one is available;
	 * otherwise tails the queue. OnComplete fires on the game thread when the
	 * slot finishes (with Asset on success, error string on failure).
	 *
	 * If the server has no routes for the ref's generator, OnComplete fires
	 * synchronously with an error.
	 */
	void Enqueue(
		const FWatabouSeedRef& Ref,
		const FString& ParentAssetPath,
		FOnWatabouSlotImportComplete OnComplete);

	/** Drop everything in the queue. Fires each dropped item's OnComplete with
	 *  an error so the producer can account for them. In-flight slots finish
	 *  naturally and call back as normal. */
	void CancelQueue();

	/** Multicast: state changed -- slot busy/idle transition, queue depth change,
	 *  or slot count change. Observers (panel status text, batch notifier) bind here. */
	DECLARE_MULTICAST_DELEGATE(FOnPoolChanged);
	FOnPoolChanged OnChanged;

private:
	void EnsureHostWindow();
	void SpawnSlot();
	void DispatchPending();
	void HandleSlotIdle(FWatabouBrowserSlot& Slot);
	void RebuildHostContent();

	/** Teardown the off-screen host window and dispose all slots. Caller must
	 *  guarantee Queue/Busy/Draining are all empty -- see MaybeAutoShutdown(). */
	void TeardownHost();

	/** Event-driven dormancy check, called from every state-change site that
	 *  could leave the pool fully quiescent. Tears down the host window if (and
	 *  only if) all of: Queue empty, no busy slot, no draining slot, recursive
	 *  importer not running. Otherwise no-op -- the next state-change event
	 *  re-checks. */
	void MaybeAutoShutdown();

	/** Bound to the recursive importer's OnBecameIdle. The pool's own slot-idle
	 *  event already fires MaybeAutoShutdown, but a batch can terminate AFTER
	 *  the last slot went idle (HandleSlotIdle ran before Finish() flipped the
	 *  importer's state in the synchronous broadcast cascade). This second
	 *  trigger covers that ordering. */
	void HandleImporterBecameIdle();

	struct FQueued
	{
		FWatabouSeedRef Ref;
		FString ParentAssetPath;
		FOnWatabouSlotImportComplete OnComplete;
		TSharedPtr<FWatabouGeneratorInfo> Gen;   // resolved at Enqueue time
	};

	/** Edge length (px) per slot. The shim downsamples thumbnails to 512, so going
	 *  larger just bloats the per-frame canvas paint each bundle does -- with N>1
	 *  slots running in parallel this is what dominates generation time, not the
	 *  game-thread or HTTP server. 512 keeps thumbnails crisp without choking the
	 *  CEF render process. */
	static constexpr float SlotPixelSize = 512.f;

	TSharedPtr<SWindow>                     HostWindow;
	TSharedPtr<SHorizontalBox>              HostContent;
	TArray<TSharedRef<FWatabouBrowserSlot>> Slots;
	TArray<TSharedRef<FWatabouBrowserSlot>> Draining;
	TArray<FQueued>                         Queue;
	int32                                   NextSlotIndex = 0;

	/** Slot count to spawn on next EnsureInitialized(). Tracked across dormancy
	 *  so the spinbox value survives teardown/wake cycles without resurrecting
	 *  the host window just because the user nudged the slider. */
	int32                                   DesiredSlotCount = MinSlots;

	TWeakPtr<FWatabouRecursiveImporter>     WeakRecursiveImporter;
	FDelegateHandle                         ImporterIdleHandle;
};
