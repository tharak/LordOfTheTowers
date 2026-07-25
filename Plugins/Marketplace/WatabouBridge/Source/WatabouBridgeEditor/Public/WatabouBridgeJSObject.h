// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WatabouExportPayload.h"
#include "WatabouBridgeJSObject.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWatabouExport, const FWatabouExportPayload&, Payload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWatabouMessage, FString, Message);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnWatabouExportNative, const FWatabouExportPayload& /*Payload*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWatabouMessageNative, const FString& /*Message*/);

/**
 * UE-side endpoint for the JS shim's exported data.
 *
 * The shim reaches C++ over one of two transports (see WatabouShim.cpp's `deliver`):
 *
 *   * window.ue.watabou.onbridgemessage(<sentinel>) -- the BindUObject bridge, routed here via
 *     OnBridgeMessage(). This is the ONLY channel that works on macOS: the WKWebView /
 *     AppleWebBrowser backend never forwards console.log to IWebBrowserWindow::OnConsoleMessage
 *     (only the CEF backend does). Bound permanently at browser construction -- before the first
 *     navigation -- so it survives the bundle's in-place regenerations.
 *
 *   * console.log(`__UE_BRIDGE__|<event>|<base64-utf8>`) -- the CEF (Windows) fallback, parsed by
 *     the panel's / slot's OnConsoleMessage handler. Used only when the bound object isn't there.
 *
 * Both transports funnel into the same OnExport/OnReady/OnError, so exactly one path processes
 * any given event (the shim returns after the window.ue call and never also logs it).
 *
 * UFUNCTIONs are BlueprintCallable both so Blueprint can hook the dynamic delegates and so the
 * window.ue bridge can invoke OnBridgeMessage by reflection.
 *
 * Native (non-dynamic) variants of the multicast delegates exist alongside the dynamic ones so
 * Slate widgets can bind directly (the dynamic form requires UObject handlers).
 */
UCLASS(BlueprintType, Category = "Watabou")
class WATABOUBRIDGEEDITOR_API UWatabouBridgeJSObject : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Entry point for the window.ue bridge. Receives the raw `__UE_BRIDGE__|...` sentinel string
	 * (identical to what the console.log fallback emits), parses it with
	 * WatabouShim::TryParseBridgeMessage, and dispatches to OnExport / OnReady / OnError. Exposed
	 * to JS as window.ue.watabou.onbridgemessage (BindUObject lower-cases every bound name).
	 */
	UFUNCTION(BlueprintCallable, Category = "Watabou|Bridge")
	void OnBridgeMessage(const FString& RawSentinel);

	/** Receive an export from the JS shim. See FWatabouExportPayload for field semantics. */
	UFUNCTION(BlueprintCallable, Category = "Watabou|Bridge")
	void OnExport(const FWatabouExportPayload& Payload);

	UFUNCTION(BlueprintCallable, Category = "Watabou|Bridge")
	void OnReady();

	UFUNCTION(BlueprintCallable, Category = "Watabou|Bridge")
	void OnError(const FString& Message);

	UPROPERTY(BlueprintAssignable, Category = "Watabou|Bridge")
	FOnWatabouExport OnExportReceived;

	UPROPERTY(BlueprintAssignable, Category = "Watabou|Bridge")
	FOnWatabouMessage OnReadySignal;

	UPROPERTY(BlueprintAssignable, Category = "Watabou|Bridge")
	FOnWatabouMessage OnErrorSignal;

	FOnWatabouExportNative OnExportReceivedNative;
	FOnWatabouMessageNative OnReadySignalNative;
	FOnWatabouMessageNative OnErrorSignalNative;
};
