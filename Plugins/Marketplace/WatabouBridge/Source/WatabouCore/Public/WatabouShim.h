// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

struct FWatabouGeneratorInfo;
struct FWatabouExportPayload;

/**
 * JS shim injected into every browser host the bridge drives (SWebBrowser in
 * editor, UWebBrowser at runtime). The shim:
 *   - overrides window.saveAs to read the export blob and emit a console-log
 *     sentinel with the JSON + thumbnail data
 *   - polls the generator's readiness expression and either auto-fires the
 *     export trigger (headless / batch mode) or exposes window.__watabou_trigger_export
 *     for the editor's Import button to call (interactive mode)
 *   - emits ready / error events on the same console-log sentinel
 *
 * Lives in WatabouCore so both the editor slot and a future runtime task can
 * inject the same shim and parse the same sentinel.
 *
 * See [[reference-watabou-sandbox-pattern]] for the why behind the sentinel.
 */
namespace WatabouShim
{
	/** Build the JS shim to execute via the browser's ExecuteJavascript after page load. */
	WATABOUCORE_API FString BuildInjectionScript(const FWatabouGeneratorInfo& Gen, bool bAutoExport);

	/**
	 * Parse a console-log message and, if it matches the bridge sentinel, populate
	 * OutEvent + OutPayload + OutErrorMessage and return true. Returns false for
	 * non-bridge messages (caller can log them as plain CEF output).
	 *
	 * OutEvent: "export", "ready", or "error".
	 * Only OutPayload is populated when OutEvent == "export".
	 * Only OutErrorMessage is populated when OutEvent == "error".
	 */
	WATABOUCORE_API bool TryParseBridgeMessage(
		const FString& Message,
		FString& OutEvent,
		FWatabouExportPayload& OutPayload,
		FString& OutErrorMessage);

	/**
	 * Global toggle for CEF console-log forwarding. When muted, non-bridge messages
	 * from CEF (the bundle's own console.log/error chatter) are dropped instead of
	 * being forwarded to LogWatabou. Bridge sentinel messages always go through.
	 *
	 * Default: muted (true). The panel exposes a checkbox to flip it for debugging.
	 * Affects both the visible browser and every hidden pool slot, since both read
	 * the same flag from their respective HandleConsoleMessage paths.
	 */
	WATABOUCORE_API bool IsCefLogMuted();
	WATABOUCORE_API void SetCefLogMuted(bool bMuted);

	/**
	 * Global toggle for whether import operations save the resulting uasset to disk
	 * immediately. Honored by both the visible-browser manual import path and the
	 * hidden pool slot path -- previously only the visible path respected the panel
	 * checkbox, so batch imports always auto-saved regardless of UI state.
	 *
	 * Default: false. The panel exposes a checkbox to flip it.
	 */
	WATABOUCORE_API bool IsAutoSaveOnImport();
	WATABOUCORE_API void SetAutoSaveOnImport(bool bAutoSave);
}
