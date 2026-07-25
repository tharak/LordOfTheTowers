// Copyright 2026 Timothé Lapetite

#include "WatabouBrowserPool.h"

#include "WatabouBridge.h"
#include "WatabouBridgeEditorModule.h"
#include "WatabouBrowserSlot.h"
#include "WatabouGeneratorInfo.h"
#include "WatabouGeneratorRegistry.h"
#include "WatabouRecursiveImporter.h"

#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"

FWatabouBrowserPool::FWatabouBrowserPool() = default;

FWatabouBrowserPool::~FWatabouBrowserPool()
{
	Shutdown();
}

void FWatabouBrowserPool::EnsureInitialized()
{
	if (HostWindow.IsValid()) { return; }
	EnsureHostWindow();
	if (!HostWindow.IsValid()) { return; }   // Slate not ready; deferred

	// Spawn DesiredSlotCount slots into the freshly created host. We can't go
	// through SetSlotCount here because Slots.Num()==0 matches no path cleanly --
	// just open-code the grow loop. Mirrors the grow branch of SetSlotCount.
	const int32 N = FMath::Clamp(DesiredSlotCount, MinSlots, MaxSlots);
	for (int32 i = 0; i < N; ++i) { SpawnSlot(); }
	RebuildHostContent();
	OnChanged.Broadcast();
}

void FWatabouBrowserPool::Shutdown()
{
	// Unsubscribe from the importer (if still alive). Done before TeardownHost so
	// any cascading notifications during slot destruction don't trip a stale handle.
	if (ImporterIdleHandle.IsValid())
	{
		if (TSharedPtr<FWatabouRecursiveImporter> Imp = WeakRecursiveImporter.Pin())
		{
			Imp->OnBecameIdle.Remove(ImporterIdleHandle);
		}
		ImporterIdleHandle.Reset();
	}
	WeakRecursiveImporter.Reset();

	// Cancel queued items with an error so producers stop tracking them.
	for (FQueued& Q : Queue)
	{
		Q.OnComplete.ExecuteIfBound(nullptr, TEXT("pool shutdown"));
	}
	Queue.Reset();

	TeardownHost();
}

void FWatabouBrowserPool::TeardownHost()
{
	// In-flight slot callbacks captured WeakPtrs to themselves; tearing down the
	// shared array drops those refs. CEF will tear down with its widgets.
	for (const TSharedRef<FWatabouBrowserSlot>& Slot : Slots)
	{
		Slot->OnIdle.Unbind();
	}
	for (const TSharedRef<FWatabouBrowserSlot>& Slot : Draining)
	{
		Slot->OnIdle.Unbind();
	}
	Slots.Reset();
	Draining.Reset();
	NextSlotIndex = 0;   // fresh indices on next wake

	if (HostContent.IsValid())
	{
		HostContent->ClearChildren();
		HostContent.Reset();
	}
	if (HostWindow.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RequestDestroyWindow(HostWindow.ToSharedRef());
	}
	HostWindow.Reset();
}

void FWatabouBrowserPool::SetRecursiveImporter(const TSharedRef<FWatabouRecursiveImporter>& InImporter)
{
	// Remove any prior binding (defensive; module wires this exactly once today).
	if (ImporterIdleHandle.IsValid())
	{
		if (TSharedPtr<FWatabouRecursiveImporter> Prev = WeakRecursiveImporter.Pin())
		{
			Prev->OnBecameIdle.Remove(ImporterIdleHandle);
		}
		ImporterIdleHandle.Reset();
	}

	WeakRecursiveImporter = InImporter;
	ImporterIdleHandle = InImporter->OnBecameIdle.AddSP(
		SharedThis(this), &FWatabouBrowserPool::HandleImporterBecameIdle);
}

void FWatabouBrowserPool::HandleImporterBecameIdle()
{
	MaybeAutoShutdown();
}

void FWatabouBrowserPool::MaybeAutoShutdown()
{
	if (!HostWindow.IsValid())   { return; }   // already torn down
	if (Queue.Num() > 0)         { return; }
	if (GetBusyCount() > 0)      { return; }
	if (Draining.Num() > 0)      { return; }

	// Recursive importer still walking counts as "more work imminent" -- a parent
	// import can finish (slot idle) before the importer enqueues child refs.
	// The importer's OnBecameIdle will trigger a re-check when it actually stops.
	if (TSharedPtr<FWatabouRecursiveImporter> Imp = WeakRecursiveImporter.Pin())
	{
		if (Imp->IsRunning()) { return; }
	}

	UE_LOG(LogWatabou, Log, TEXT("BrowserPool: idle -- tearing down host window"));
	TeardownHost();
	OnChanged.Broadcast();
}

void FWatabouBrowserPool::EnsureHostWindow()
{
	if (HostWindow.IsValid()) { return; }
	if (!FSlateApplication::IsInitialized())
	{
		UE_LOG(LogWatabou, Warning, TEXT("BrowserPool: Slate not initialized; deferring host window"));
		return;
	}

	HostContent = SNew(SHorizontalBox);

	// Why visible-but-off-screen rather than HideWindow():
	//   HideWindow stops Slate from laying out the window, so SWebBrowser reports
	//   tiny/zero geometry to CEF. Lime/OpenFL sizes its <canvas> to the viewport
	//   at app-start -- with no viewport that's a 1x1 canvas and a 1x1 thumbnail.
	//   Keeping the window visible forces normal layout; we just stash it far
	//   enough off-screen that it can't intrude on the editor.
	//
	// SHorizontalBox (not SOverlay): each slot owns its own non-overlapping region.
	// Stacked SOverlay slots can make CEF treat under-layer browsers as occluded
	// and throttle their render thread, which broke parallelism with N>1 slots.
	//
	// Window is sized to fit MaxSlots side-by-side so the layout doesn't change as
	// the user resizes the pool. Position is far enough negative to stay off any
	// reasonable multi-monitor setup (4-monitor stacks top out around -8K).
	const FVector2D OffScreenPos(-32000.f, -32000.f);
	const FVector2D WindowSize(SlotPixelSize * MaxSlots, SlotPixelSize);

	HostWindow = SNew(SWindow)
		.Title(NSLOCTEXT("WatabouBridge", "PoolHostWindow", "Watabou Browser Pool"))
		.ClientSize(WindowSize)
		.ScreenPosition(OffScreenPos)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.HasCloseButton(false)
		.CreateTitleBar(false)
		.IsTopmostWindow(false)
		.SizingRule(ESizingRule::FixedSize)
		.FocusWhenFirstShown(false)
		.ActivationPolicy(EWindowActivationPolicy::Never)
		[
			HostContent.ToSharedRef()
		];

	// Add but don't show yet -- ScreenPosition isn't always honored at OS-window
	// creation. We move to the final off-screen position first, then show; this
	// prevents the window from flashing at the default screen position before
	// reaching its off-screen home.
	FSlateApplication::Get().AddWindow(HostWindow.ToSharedRef(), /*bShowImmediately=*/false);
	HostWindow->MoveWindowTo(OffScreenPos);
	HostWindow->ShowWindow();
}

void FWatabouBrowserPool::SetSlotCount(int32 N)
{
	N = FMath::Clamp(N, MinSlots, MaxSlots);
	DesiredSlotCount = N;

	// Dormant: stash the value for the next EnsureInitialized() and bail. We
	// deliberately do NOT wake the pool here -- the spinbox lives in the panel
	// header and gets twiddled during normal browsing, which shouldn't resurrect
	// the off-screen host window. Broadcast so observers (panel spinbox) see the
	// new value reflected.
	if (!HostWindow.IsValid())
	{
		OnChanged.Broadcast();
		return;
	}

	const int32 Current = Slots.Num();
	if (N == Current) { return; }

	if (N > Current)
	{
		for (int32 i = Current; i < N; ++i) { SpawnSlot(); }
		RebuildHostContent();
		OnChanged.Broadcast();
		DispatchPending();
		return;
	}

	// Shrinking: move surplus slots into Draining[]. Idle ones disposed right away;
	// busy ones finish in-flight then drop themselves via HandleSlotIdle's drain check.
	while (Slots.Num() > N)
	{
		TSharedRef<FWatabouBrowserSlot> Slot = Slots.Pop(EAllowShrinking::No);
		if (!Slot->IsBusy())
		{
			// Idle slot: dispose immediately.
			Slot->OnIdle.Unbind();
		}
		else
		{
			Draining.Add(Slot);
		}
	}
	RebuildHostContent();
	OnChanged.Broadcast();

	// A shrink to a quiescent pool can leave it fully idle (no busy, no draining,
	// no queue) -- check whether we should tear down. The shrink-victims that
	// went into Draining will re-trigger this from HandleSlotIdle when they
	// finish, covering the busy case.
	MaybeAutoShutdown();
}

void FWatabouBrowserPool::SpawnSlot()
{
	TSharedRef<FWatabouBrowserSlot> Slot = MakeShared<FWatabouBrowserSlot>(NextSlotIndex++);
	Slot->Construct();
	Slot->OnIdle.BindRaw(this, &FWatabouBrowserPool::HandleSlotIdle);
	Slots.Add(Slot);
}

void FWatabouBrowserPool::RebuildHostContent()
{
	if (!HostContent.IsValid()) { return; }
	HostContent->ClearChildren();

	// Each slot occupies a non-overlapping SlotPixelSize square. Side-by-side
	// layout means CEF treats every browser as fully visible, so all of them
	// render at full speed in parallel.
	auto AddSlotWidget = [this](const TSharedRef<FWatabouBrowserSlot>& Slot)
	{
		HostContent->AddSlot()
			.AutoWidth()
			[
				SNew(SBox).WidthOverride(SlotPixelSize).HeightOverride(SlotPixelSize)
				[
					Slot->GetWidget()
				]
			];
	};
	for (const TSharedRef<FWatabouBrowserSlot>& Slot : Slots)    { AddSlotWidget(Slot); }
	// Draining slots still need a Slate parent until their in-flight import resolves.
	for (const TSharedRef<FWatabouBrowserSlot>& Slot : Draining) { AddSlotWidget(Slot); }
}

int32 FWatabouBrowserPool::GetBusyCount() const
{
	int32 N = 0;
	for (const TSharedRef<FWatabouBrowserSlot>& Slot : Slots)
	{
		if (Slot->IsBusy()) { ++N; }
	}
	return N;
}

void FWatabouBrowserPool::Enqueue(
	const FWatabouSeedRef& Ref,
	const FString& ParentAssetPath,
	FOnWatabouSlotImportComplete OnComplete)
{
	// Lazy wake: a dormant pool gets its host window + slots created right here,
	// the first time any producer asks to enqueue work. Subsequent calls are
	// cheap (HostWindow.IsValid() early-out).
	EnsureInitialized();

	TSharedPtr<FWatabouGeneratorInfo> Gen = FWatabouGeneratorRegistry::Get().FindById(Ref.GeneratorId);
	if (!Gen.IsValid())
	{
		OnComplete.ExecuteIfBound(nullptr,
			FString::Printf(TEXT("Pool: unknown generator '%s'"), *Ref.GeneratorId));
		return;
	}

	// One Restart attempt covers both "server stopped" and "server up but routes missing"
	// (e.g. raced per-generator module loads during startup). Re-verify once after.
	FWatabouBridgeEditorModule& Module = FWatabouBridgeEditorModule::Get();
	if (!Module.IsServerRunning() || !Module.HasRoutesForGenerator(Gen->Id))
	{
		Module.Restart();
		if (!Module.HasRoutesForGenerator(Gen->Id))
		{
			OnComplete.ExecuteIfBound(nullptr,
				FString::Printf(TEXT("Pool: no cached bundle for '%s' (run Update Generators)"), *Gen->Id));
			return;
		}
	}

	Queue.Add(FQueued{ Ref, ParentAssetPath, OnComplete, Gen });
	DispatchPending();
	OnChanged.Broadcast();
}

void FWatabouBrowserPool::DispatchPending()
{
	while (Queue.Num() > 0)
	{
		FWatabouBrowserSlot* Idle = nullptr;
		for (const TSharedRef<FWatabouBrowserSlot>& Slot : Slots)
		{
			if (!Slot->IsBusy()) { Idle = &Slot.Get(); break; }
		}
		if (!Idle) { return; }   // no capacity right now; wait for HandleSlotIdle

		FQueued Item = MoveTemp(Queue[0]);
		Queue.RemoveAt(0, EAllowShrinking::No);
		Idle->Begin(Item.Gen, Item.Ref, Item.ParentAssetPath, Item.OnComplete);
	}
}

void FWatabouBrowserPool::HandleSlotIdle(FWatabouBrowserSlot& Slot)
{
	// Drain check: was this a shrink-victim that just finished its in-flight import?
	// Skipped entirely in the common case where the pool isn't shrinking.
	for (int32 i = 0; i < Draining.Num(); ++i)
	{
		if (&Draining[i].Get() == &Slot)
		{
			Draining[i]->OnIdle.Unbind();
			Draining.RemoveAt(i, EAllowShrinking::No);
			RebuildHostContent();
			OnChanged.Broadcast();
			MaybeAutoShutdown();   // last shrink-victim leaving may quiesce the pool
			return;   // don't dispatch onto a slot we're disposing
		}
	}

	OnChanged.Broadcast();
	// NOTE: OnChanged's broadcast cascades synchronously into the recursive
	// importer's HandlePoolChanged, which may DispatchPending its own queued
	// refs into our pool. That's the path that keeps a recursive batch flowing:
	// by the time we run our DispatchPending below the pool's Queue may have
	// already gained items.
	DispatchPending();

	// Re-check teardown. If the broadcast cascade ended the batch
	// (importer Finish() flipped to Done), the importer's OnBecameIdle fired
	// inside the cascade and already called MaybeAutoShutdown -- but the pool
	// was still mid-HandleSlotIdle then and the slot's busy state wasn't yet
	// fully resolved up the stack. Calling again here, after Dispatch settled,
	// catches the case where everything truly is idle now.
	MaybeAutoShutdown();
}

void FWatabouBrowserPool::CancelQueue()
{
	if (Queue.Num() == 0) { return; }

	TArray<FQueued> Drop = MoveTemp(Queue);
	Queue.Reset();
	for (FQueued& Q : Drop)
	{
		Q.OnComplete.ExecuteIfBound(nullptr, TEXT("batch cancelled"));
	}
	OnChanged.Broadcast();
	MaybeAutoShutdown();
}
