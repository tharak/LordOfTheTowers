// Copyright 2026 Timothé Lapetite

#include "WatabouBrowserSlot.h"

#include "SWebBrowser.h"
#include "WatabouAssetBase.h"
#include "WatabouAssetBuilder.h"
#include "WatabouBridge.h"
#include "WatabouBridgeEditorModule.h"
#include "WatabouBridgeJSObject.h"
#include "WatabouGeneratorInfo.h"
#include "WatabouImportConfig.h"
#include "WatabouSeedRef.h"
#include "WatabouShim.h"

FWatabouBrowserSlot::FWatabouBrowserSlot(int32 InSlotIndex)
	: SlotIndex(InSlotIndex)
{
}

FWatabouBrowserSlot::~FWatabouBrowserSlot() = default;

void FWatabouBrowserSlot::Construct()
{
	WebBrowser = SNew(SWebBrowser)
		.InitialURL(TEXT("about:blank"))
		.ShowControls(false)
		.ShowAddressBar(false)
		.ShowInitialThrobber(false)
		.SupportsTransparency(false)
		.OnLoadCompleted(FSimpleDelegate::CreateSP(this, &FWatabouBrowserSlot::HandleLoadCompleted))
		.OnConsoleMessage(FOnConsoleMessageDelegate::CreateSP(this, &FWatabouBrowserSlot::HandleConsoleMessage));

	// window.ue return channel -- the only transport that reaches C++ on macOS's WKWebView backend
	// (which never fires OnConsoleMessage). One bridge object per slot so parallel slots never
	// cross-talk; its native delegates fan into the same handlers the console-log fallback uses.
	// Bound permanently, before any navigation, so it survives the bundle's in-place regenerations.
	Bridge = TStrongObjectPtr<UWatabouBridgeJSObject>(NewObject<UWatabouBridgeJSObject>());
	Bridge->OnExportReceivedNative.AddSP(this, &FWatabouBrowserSlot::HandleExportPayload);
	Bridge->OnErrorSignalNative.AddSP(this, &FWatabouBrowserSlot::HandleErrorString);
	WebBrowser->BindUObject(TEXT("watabou"), Bridge.Get(), /*bIsPermanent=*/true);
}

TSharedRef<SWidget> FWatabouBrowserSlot::GetWidget() const
{
	check(WebBrowser.IsValid());
	return WebBrowser.ToSharedRef();
}

void FWatabouBrowserSlot::Begin(
	const TSharedPtr<FWatabouGeneratorInfo>& Gen,
	const FWatabouSeedRef& Ref,
	const FString& ParentAssetPath,
	FOnWatabouSlotImportComplete OnComplete)
{
	check(!IsBusy());
	check(Gen.IsValid());
	check(WebBrowser.IsValid());

	PendingGenerator       = Gen;
	PendingSeedRef         = Ref;
	PendingParentAssetPath = ParentAssetPath;
	PendingCallback        = OnComplete;
	CurrentRefKey          = Ref.GetCanonicalKey();
	State                  = EWatabouSlotState::Loading;

	// Build URL via the same routing the panel uses. Routes were already verified
	// upstream (pool checks before dispatching).
	const FWatabouBridgeEditorModule& Module = FWatabouBridgeEditorModule::Get();
	FString Query = Ref.BuildQueryString();
	if (!Query.IsEmpty() && !Query.StartsWith(TEXT("?")) && !Query.StartsWith(TEXT("/")))
	{
		Query = TEXT("?") + Query;
	}

	if (!Module.IsServerRunning() || !Module.HasRoutesForGenerator(Gen->Id))
	{
		// Unreachable in normal flow: the pool verifies routes (and restarts the server if
		// needed) before dispatching to a slot. If we somehow land here, fail the import
		// rather than silently loading from watabou.github.io -- hitting the live site
		// defeats the bundle-pinning design (upstream JSON-format drift breaks parsing).
		UE_LOG(LogWatabou, Error,
			TEXT("Slot %d: no local routes for '%s' at dispatch -- failing import (run Update Generators)"),
			SlotIndex, *Gen->Id);
		Complete(nullptr, FString::Printf(TEXT("no local routes for '%s'"), *Gen->Id));
		return;
	}

	CurrentURL = FString::Printf(TEXT("http://127.0.0.1:%u/%s/%s"),
		Module.GetServerPort(), *Gen->Id, *Query);

	UE_LOG(LogWatabou, Log, TEXT("Slot %d Begin [%s] under '%s': %s"),
		SlotIndex, *Gen->DisplayName,
		ParentAssetPath.IsEmpty() ? TEXT("(top-level)") : *ParentAssetPath, *CurrentURL);

	WebBrowser->LoadURL(CurrentURL);
}

void FWatabouBrowserSlot::HandleLoadCompleted()
{
	if (!WebBrowser.IsValid() || !PendingGenerator.IsValid()) { return; }
	if (State != EWatabouSlotState::Loading) { return; }

	// Hidden pool slots always auto-export when the bundle reports ready -- there's no UI to click Import.
	WebBrowser->ExecuteJavascript(WatabouShim::BuildInjectionScript(*PendingGenerator, /*bAutoExport=*/true));
}

void FWatabouBrowserSlot::HandleConsoleMessage(
	const FString& Message,
	const FString& Source,
	int32 Line,
	EWebBrowserConsoleLogSeverity Severity)
{
	FString Event;
	FWatabouExportPayload Payload;
	FString ErrorMessage;
	if (WatabouShim::TryParseBridgeMessage(Message, Event, Payload, ErrorMessage))
	{
		// Transport diagnostic (Verbose): the window.ue path logs its own line in OnBridgeMessage.
		UE_LOG(LogWatabou, Verbose, TEXT("Slot %d bridge transport: console.log sentinel (event='%s')"), SlotIndex, *Event);
		if (Event == TEXT("export"))
		{
			HandleExportPayload(Payload);
			return;
		}
		if (Event == TEXT("ready"))
		{
			return;
		}
		if (Event == TEXT("error"))
		{
			HandleErrorString(ErrorMessage);
			return;
		}
	}

	if (!WatabouShim::IsCefLogMuted())
	{
		UE_LOG(LogWatabou, Log, TEXT("[CEF slot %d %s:%d] %s"), SlotIndex, *Source, Line, *Message);
	}
}

void FWatabouBrowserSlot::HandleExportPayload(const FWatabouExportPayload& Payload)
{
	if (!IsBusy())
	{
		// Stale export from a previous navigation that's no longer in flight. The shim's
		// __watabou_ready_fired guard prevents this in steady state but bundle URL
		// canonicalization races can still produce extras.
		UE_LOG(LogWatabou, Verbose, TEXT("Slot %d: dropping export, slot is idle"), SlotIndex);
		return;
	}

	State = EWatabouSlotState::Generating;

	UE_LOG(LogWatabou, Log, TEXT("Slot %d Export %s: %d chars (state='%s', thumb=%d bytes)"),
		SlotIndex, *Payload.FileName, Payload.Content.Len(),
		*Payload.StateUrl, Payload.ThumbnailDataUrl.Len());

	WatabouAssetBuilder::FBuildArgs Args;
	Args.Gen             = PendingGenerator;
	Args.Payload         = Payload;
	Args.RequestedRef    = PendingSeedRef;           // slot imports a known ref -> use it as the asset's identity
	Args.ParentAssetPath = PendingParentAssetPath;   // non-empty for recursive children (nests under parent)
	Args.TopLevelBaseDir = UWatabouImportConfig::Get()->GetImportTargetDir();   // used only when top-level
	Args.bAutoSave       = WatabouShim::IsAutoSaveOnImport();

	if (Args.bAutoSave) { State = EWatabouSlotState::Saving; }

	TWeakPtr<FWatabouBrowserSlot> WeakSelf = AsShared();
	WatabouAssetBuilder::BuildAssetFromExport(Args,
		FOnWatabouAssetBuilt::CreateLambda(
			[WeakSelf](UWatabouAssetBase* Asset, const FString& Error)
			{
				if (TSharedPtr<FWatabouBrowserSlot> Self = WeakSelf.Pin())
				{
					Self->Complete(Asset, Error);
				}
			}));
}

void FWatabouBrowserSlot::HandleErrorString(const FString& Error)
{
	UE_LOG(LogWatabou, Warning, TEXT("Slot %d JS error: %s"), SlotIndex, *Error);
	if (IsBusy())
	{
		Complete(nullptr, Error);
	}
}

void FWatabouBrowserSlot::Complete(UWatabouAssetBase* Asset, const FString& Error)
{
	// Reset state BEFORE firing the callback. The importer's OnRefImported may
	// synchronously call DispatchPending -> Pool::Enqueue -> Slot::Begin on this
	// same slot. If State weren't Idle here, that re-entrant Begin would either
	// be rejected by check(!IsBusy()) (Failed state isn't busy, but check is
	// against IsBusy which excludes Failed) or its State=Loading would be stomped
	// by the trailing State=Idle below. Reset eagerly to make re-entry safe.
	FOnWatabouSlotImportComplete CallbackCopy = PendingCallback;
	PendingCallback.Unbind();
	PendingGenerator.Reset();
	PendingParentAssetPath.Reset();
	CurrentRefKey.Reset();
	State = EWatabouSlotState::Idle;

	CallbackCopy.ExecuteIfBound(Asset, Error);

	// If the callback re-entered Begin, the slot is now busy on the next ref.
	// OnIdle still fires for the pool's bookkeeping; the pool checks IsBusy
	// before dispatching, so it won't double-assign.
	OnIdle.ExecuteIfBound(*this);
}
