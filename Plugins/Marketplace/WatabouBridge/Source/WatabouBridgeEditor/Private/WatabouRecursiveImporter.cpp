// Copyright 2026 Timothé Lapetite

#include "WatabouRecursiveImporter.h"

#include "WatabouAssetBase.h"
#include "WatabouAssetBuilderHooks.h"   // SavePackageNow -- the shared save path
#include "WatabouBridge.h"
#include "WatabouBrowserPool.h"
#include "WatabouSeedCache.h"

#include "Editor.h"
#include "TimerManager.h"
#include "UObject/Package.h"

FWatabouRecursiveImporter::FWatabouRecursiveImporter(const TSharedRef<FWatabouBrowserPool>& InPool)
	: WeakPool(InPool)
{
}

FWatabouRecursiveImporter::~FWatabouRecursiveImporter() = default;

void FWatabouRecursiveImporter::Initialize()
{
	if (TSharedPtr<FWatabouBrowserPool> Pool = WeakPool.Pin())
	{
		// AddSP ties the binding to this object's lifetime: when the importer is
		// destroyed the multicast delegate prunes it automatically.
		Pool->OnChanged.AddSP(SharedThis(this), &FWatabouRecursiveImporter::HandlePoolChanged);
	}
}

void FWatabouRecursiveImporter::HandlePoolChanged()
{
	// Pool gained capacity (slot count grew, or another producer's slot freed up).
	// Try to dispatch more of our queued work.
	if (IsRunning() && Progress.State != EBatchState::Cancelling && PendingCount() > 0)
	{
		DispatchPending();
	}
}

bool FWatabouRecursiveImporter::TryEnqueueRef(const FWatabouSeedRef& Ref, const FString& ParentAssetPath)
{
	if (!Ref.IsValid()) { return false; }

	if (Settings.AllowedTargetGenerators.Num() > 0
		&& !Settings.AllowedTargetGenerators.Contains(FName(*Ref.GeneratorId)))
	{
		return false;
	}

	const FString Key = Ref.GetCanonicalKey();
	if (KeysSeen.Contains(Key)) { return false; }
	// Registry-backed existence check (NOT the ref's pointer, which can be stale if the target was
	// deleted) so a removed child is correctly re-imported rather than skipped.
	if (!Settings.bForceReimport && FWatabouSeedCache::Find(Ref).ToSoftObjectPath().IsValid())
	{
		KeysSeen.Add(Key);
		return false;
	}

	KeysSeen.Add(Key);
	Queue.Add(FPendingRef{ Ref, ParentAssetPath });
	++Progress.Total;
	return true;
}

void FWatabouRecursiveImporter::EnqueueChildrenOf(UWatabouAssetBase* Asset)
{
	if (!Asset) { return; }
	const FString AssetPath = Asset->GetPackage()->GetName();

	TArray<FWatabouSeedRef> Refs;
	FWatabouSeedCache::GatherRefs(Asset, Settings.AllowedTargetGenerators, Refs);
	for (const FWatabouSeedRef& Ref : Refs)
	{
		TryEnqueueRef(Ref, AssetPath);
	}
}

void FWatabouRecursiveImporter::StartFromAsset(UWatabouAssetBase* RootAsset)
{
	if (IsRunning())
	{
		UE_LOG(LogWatabou, Warning, TEXT("RecursiveImporter: StartFromAsset ignored -- batch already running"));
		return;
	}

	Queue.Reset();
	QueueHead = 0;
	KeysSeen.Reset();
	InFlightByKey.Reset();
	AssetsToRelink.Reset();
	Progress = FProgress{};
	Progress.State = EBatchState::Running;

	// The root holds the refs that point at everything we're about to import, so it gets relinked too.
	if (RootAsset) { AssetsToRelink.Add(FSoftObjectPath(RootAsset)); }

	EnqueueChildrenOf(RootAsset);
	BroadcastProgress();

	if (PendingCount() == 0)
	{
		UE_LOG(LogWatabou, Log, TEXT("RecursiveImporter: nothing to do (no eligible refs in root asset)"));
		Finish();
		return;
	}

	UE_LOG(LogWatabou, Log, TEXT("RecursiveImporter: starting batch (%d initial refs, root='%s')"),
		PendingCount(), RootAsset ? *RootAsset->GetPackage()->GetName() : TEXT("(null)"));
	DispatchPending();
}

void FWatabouRecursiveImporter::EnqueueFromAsset(UWatabouAssetBase* Asset)
{
	if (Progress.State == EBatchState::Cancelling) { return; }

	if (!IsRunning())
	{
		StartFromAsset(Asset);
		return;
	}

	const int32 Before = Queue.Num();
	EnqueueChildrenOf(Asset);
	if (Queue.Num() > Before)
	{
		BroadcastProgress();
		DispatchPending();
	}
}

void FWatabouRecursiveImporter::ImportSingleRef(
	const FWatabouSeedRef& Ref,
	const FString& ParentAssetPath,
	FOnWatabouSlotImportComplete OnComplete)
{
	TSharedPtr<FWatabouBrowserPool> Pool = WeakPool.Pin();
	if (!Pool.IsValid())
	{
		OnComplete.ExecuteIfBound(nullptr, TEXT("ImportSingleRef: pool unavailable"));
		return;
	}
	Pool->Enqueue(Ref, ParentAssetPath, OnComplete);
}

void FWatabouRecursiveImporter::Abort()
{
	if (!IsRunning()) { return; }
	UE_LOG(LogWatabou, Log, TEXT("RecursiveImporter: abort requested -- dropping queue, finishing in-flight"));
	Progress.State = EBatchState::Cancelling;

	// Drop queued refs from our own queue. We could also call Pool->CancelQueue but the
	// pool may have queued items from other producers (the context-menu single import),
	// which we shouldn't touch.
	Queue.Reset();
	QueueHead = 0;

	BroadcastProgress();

	// If nothing is in flight, finish immediately.
	if (InFlightByKey.Num() == 0)
	{
		Finish();
	}
}

void FWatabouRecursiveImporter::DispatchPending()
{
	if (Progress.State == EBatchState::Cancelling) { return; }

	TSharedPtr<FWatabouBrowserPool> Pool = WeakPool.Pin();
	if (!Pool.IsValid())
	{
		UE_LOG(LogWatabou, Warning, TEXT("RecursiveImporter: pool gone -- aborting batch"));
		Progress.State = EBatchState::Cancelling;
		Finish();
		return;
	}

	// Push at most enough to fill all idle slots. Keeping the pool's own queue at
	// zero for our items means Abort can simply drop our remaining Queue without
	// touching items from other producers (asset-context-menu single-ref imports).
	//
	// We pop via QueueHead (no RemoveAt) so this stays O(1) per item for huge batches,
	// and so the re-entrant call this triggers (Pool::Enqueue -> OnChanged ->
	// HandlePoolChanged -> DispatchPending) shares one advancing cursor rather than
	// fighting over Queue[0].
	bool bDispatchedAny = false;
	while (QueueHead < Queue.Num() && Pool->GetIdleCount() > 0)
	{
		FPendingRef Item = MoveTemp(Queue[QueueHead]);
		++QueueHead;

		const FString Key = Item.Ref.GetCanonicalKey();
		InFlightByKey.Add(Key, Item);
		++Progress.InFlight;
		bDispatchedAny = true;

		TWeakPtr<FWatabouRecursiveImporter> WeakSelf = AsShared();
		Pool->Enqueue(Item.Ref, Item.ParentAssetPath,
			FOnWatabouSlotImportComplete::CreateLambda(
				[WeakSelf, Key](UWatabouAssetBase* Asset, const FString& Error)
				{
					if (TSharedPtr<FWatabouRecursiveImporter> Self = WeakSelf.Pin())
					{
						Self->OnRefImported(Key, Asset, Error);
					}
				}));
	}

	// Drop the consumed prefix once everything queued has been dispatched. Guarded on a
	// full drain (QueueHead == Num) so there's nothing to shift -- a plain Reset, safe to
	// run inside the re-entrant cascade above.
	if (QueueHead > 0 && QueueHead == Queue.Num())
	{
		Queue.Reset();
		QueueHead = 0;
	}

	if (bDispatchedAny) { BroadcastProgress(); }
}

void FWatabouRecursiveImporter::OnRefImported(const FString& Key, UWatabouAssetBase* Asset, const FString& Error)
{
	const FPendingRef* Found = InFlightByKey.Find(Key);
	const FPendingRef Ref = Found ? *Found : FPendingRef{};
	InFlightByKey.Remove(Key);
	if (Progress.InFlight > 0) { --Progress.InFlight; }

	const bool bSuccess = Error.IsEmpty() && Asset != nullptr;

	if (bSuccess)
	{
		++Progress.Done;
		Progress.LastError.Reset();
		AssetsToRelink.Add(FSoftObjectPath(Asset));   // may hold grandchild refs -> relink at batch end
		if (Progress.State != EBatchState::Cancelling)
		{
			EnqueueChildrenOf(Asset);
		}
	}
	else
	{
		++Progress.Failed;
		Progress.LastError = Error;
		UE_LOG(LogWatabou, Warning, TEXT("RecursiveImporter: ref '%s' failed -- %s  [params: %s | parent: %s]"),
			*Key, *Error,
			*Ref.Ref.BuildQueryString(),
			Ref.ParentAssetPath.IsEmpty() ? TEXT("(top-level)") : *Ref.ParentAssetPath);

		if (Settings.OnFailure == EOnFailure::Abort && Progress.State != EBatchState::Cancelling)
		{
			UE_LOG(LogWatabou, Log, TEXT("RecursiveImporter: OnFailure=Abort -- stopping after first failure"));
			Progress.State = EBatchState::Cancelling;
			Queue.Reset();
			QueueHead = 0;
		}
	}

	BroadcastProgress();

	if (Progress.State == EBatchState::Cancelling)
	{
		if (InFlightByKey.Num() == 0) { Finish(); }
		return;
	}

	DispatchPending();

	if (PendingCount() == 0 && InFlightByKey.Num() == 0)
	{
		Finish();
	}
}

void FWatabouRecursiveImporter::Finish()
{
	Progress.State = EBatchState::Done;
	UE_LOG(LogWatabou, Log,
		TEXT("RecursiveImporter: batch finished -- %d imported, %d failed (%d enqueued total)"),
		Progress.Done, Progress.Failed, Progress.Total);
	BroadcastProgress();

	// Fill parents' ResolvedAsset pointers now that every child exists -- but NEXT TICK, off the
	// slot-completion callstack: saving inside the import-complete cascade can stall, and the
	// re-entrant OnBecameIdle observers below would otherwise run mid-save. Capture the set (we clear
	// the member just below); a module-lifetime importer keeps the weak self alive until the tick.
	if (AssetsToRelink.Num() > 0)
	{
		const TArray<FSoftObjectPath> ToRelink = AssetsToRelink.Array();
		if (GEditor)
		{
			TWeakPtr<FWatabouRecursiveImporter> WeakSelf = AsShared();
			GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda(
				[WeakSelf, ToRelink]()
				{
					if (TSharedPtr<FWatabouRecursiveImporter> Self = WeakSelf.Pin())
					{
						Self->RelinkImportedAssets(ToRelink);
					}
				}));
		}
		else
		{
			RelinkImportedAssets(ToRelink);   // headless fallback: no editor tick loop
		}
	}

	Queue.Reset();
	QueueHead = 0;
	KeysSeen.Reset();
	InFlightByKey.Reset();
	AssetsToRelink.Reset();

	// Fire AFTER all internal state is cleared. Observers (e.g. the browser pool)
	// will re-check teardown predicates and may call back into us via IsRunning();
	// reaching them with stale Queue/InFlight contents would be misleading.
	OnBecameIdle.Broadcast();
}

void FWatabouRecursiveImporter::RelinkImportedAssets(const TArray<FSoftObjectPath>& Paths)
{
	int32 NumRelinked = 0;
	for (const FSoftObjectPath& Path : Paths)
	{
		UWatabouAssetBase* Asset = Cast<UWatabouAssetBase>(Path.ResolveObject());
		if (!Asset) { Asset = Cast<UWatabouAssetBase>(Path.TryLoad()); }
		if (!Asset) { continue; }

		if (FWatabouSeedCache::RelinkAsset(Asset) && WatabouAssetBuilderHooks::SavePackageNow(Asset))
		{
			++NumRelinked;
		}
	}

	if (NumRelinked > 0)
	{
		UE_LOG(LogWatabou, Log, TEXT("RecursiveImporter: relinked + saved %d parent asset(s)"), NumRelinked);
	}
}

void FWatabouRecursiveImporter::BroadcastProgress()
{
	OnProgressChanged.Broadcast(Progress);
}
