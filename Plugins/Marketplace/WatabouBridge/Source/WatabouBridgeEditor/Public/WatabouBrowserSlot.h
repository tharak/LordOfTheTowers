// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h" // TStrongObjectPtr (GC-roots the per-slot bridge object)
#include "IWebBrowserWindow.h"   // EWebBrowserConsoleLogSeverity
#include "WatabouAssetBuilder.h" // FOnWatabouAssetBuilt (delegate signature reuse)
#include "WatabouBridgeJSObject.h" // full type: TStrongObjectPtr<UWatabouBridgeJSObject> owns it, so a forward decl is insufficient (its Reset upcasts to const UObject*)
#include "WatabouSeedRef.h"      // FWatabouSeedRef (stored by value on the slot)

class SWebBrowser;
class UWatabouAssetBase;
struct FWatabouGeneratorInfo;
struct FWatabouExportPayload;

/**
 * Outcome callback for FWatabouBrowserSlot::Begin. Asset is non-null on success;
 * Error is empty on success and non-empty otherwise. Fired on the game thread.
 */
DECLARE_DELEGATE_TwoParams(FOnWatabouSlotImportComplete, UWatabouAssetBase* /*Asset*/, const FString& /*Error*/);

/**
 * Lifecycle of a single import on one slot. Mirror of EWatabouImportState that
 * used to live on SWatabouImportPanel, scoped per-slot now that the pool runs
 * multiple in parallel.
 */
enum class EWatabouSlotState : uint8
{
	Idle,
	Loading,     // bundle generating JSON (between Begin and export-received)
	Generating,  // deserialize + tree-build + thumbnail apply (synchronous on game thread)
	Saving,      // deferred SavePackage in flight
	Failed,
};

/**
 * Owns one SWebBrowser plus the in-flight state for the import it's currently
 * running. The JS shim reports back over one of two transports, both funnelling
 * into this slot's handlers:
 *   * window.ue.watabou.onbridgemessage -- a per-slot UWatabouBridgeJSObject bound
 *     to this slot's browser. Each slot gets its OWN instance so parallel slots
 *     never cross-talk. The only channel that works on macOS's WKWebView backend.
 *   * the `__UE_BRIDGE__` console-log sentinel via this browser's OnConsoleMessage
 *     handler -- the CEF (Windows) fallback.
 *
 * Slots are not Slate widgets themselves -- GetWidget() returns the SWebBrowser
 * for parenting into a host container (visible panel or hidden module window).
 *
 * Threading: all methods game-thread-only. CEF + Slate handlers fire on game thread.
 */
class WATABOUBRIDGEEDITOR_API FWatabouBrowserSlot : public TSharedFromThis<FWatabouBrowserSlot>
{
public:
	explicit FWatabouBrowserSlot(int32 InSlotIndex);
	~FWatabouBrowserSlot();

	void Construct();   // builds the SWebBrowser; call once after MakeShared.

	/** SWebBrowser widget to parent in whatever host container holds the slot. */
	TSharedRef<SWidget> GetWidget() const;

	int32 GetIndex() const { return SlotIndex; }
	bool  IsBusy() const   { return State != EWatabouSlotState::Idle && State != EWatabouSlotState::Failed; }
	const FString& GetCurrentRefKey() const { return CurrentRefKey; }
	EWatabouSlotState GetState() const { return State; }

	/**
	 * Start a single import. Pre-condition: !IsBusy(). Caller has already looked
	 * up the generator and confirmed the local HTTP server has routes for it.
	 */
	void Begin(
		const TSharedPtr<FWatabouGeneratorInfo>& Gen,
		const FWatabouSeedRef& Ref,
		const FString& ParentAssetPath,
		FOnWatabouSlotImportComplete OnComplete);

	/** Fired after Complete() resolves the callback and resets pending state. The
	 *  pool listens here to dispatch the next queued ref. */
	DECLARE_DELEGATE_OneParam(FOnSlotIdle, FWatabouBrowserSlot& /*Slot*/);
	FOnSlotIdle OnIdle;

private:
	void HandleLoadCompleted();
	void HandleConsoleMessage(const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity);
	void HandleExportPayload(const FWatabouExportPayload& Payload);
	void HandleErrorString(const FString& Error);
	void Complete(UWatabouAssetBase* Asset, const FString& Error);

	int32 SlotIndex;

	TSharedPtr<SWebBrowser> WebBrowser;

	/** Per-slot window.ue bridge endpoint, bound to WebBrowser in Construct(). GC-rooted via
	 *  TStrongObjectPtr since the slot is not a UObject/FGCObject. Its native delegates fan into
	 *  HandleExportPayload / HandleErrorString -- the same handlers the console-log path uses. */
	TStrongObjectPtr<UWatabouBridgeJSObject> Bridge;

	FString CurrentURL;

	TSharedPtr<FWatabouGeneratorInfo> PendingGenerator;
	FWatabouSeedRef PendingSeedRef;
	FString PendingParentAssetPath;
	FOnWatabouSlotImportComplete PendingCallback;
	FString CurrentRefKey;
	EWatabouSlotState State = EWatabouSlotState::Idle;
};
