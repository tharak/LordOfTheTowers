// Copyright 2026 Timothé Lapetite

#include "WatabouShim.h"

#include "WatabouBridge.h"
#include "WatabouExportPayload.h"
#include "WatabouGeneratorInfo.h"
#include "Misc/Base64.h"
#include "HAL/PlatformAtomics.h"

namespace WatabouShim
{
	// Default muted: bridge sentinel messages always pass through TryParseBridgeMessage;
	// the bundle's own console chatter is what gets dropped here.
	static int32 GCefLogMuted = 1;

	bool IsCefLogMuted()
	{
		return FPlatformAtomics::AtomicRead(&GCefLogMuted) != 0;
	}

	void SetCefLogMuted(bool bMuted)
	{
		FPlatformAtomics::InterlockedExchange(&GCefLogMuted, bMuted ? 1 : 0);
	}

	// Default off: matches previous panel-checkbox default. The visible-browser flow
	// and the hidden pool slot flow both read this to decide whether SavePackage runs.
	static int32 GAutoSaveOnImport = 0;

	bool IsAutoSaveOnImport()
	{
		return FPlatformAtomics::AtomicRead(&GAutoSaveOnImport) != 0;
	}

	void SetAutoSaveOnImport(bool bAutoSave)
	{
		FPlatformAtomics::InterlockedExchange(&GAutoSaveOnImport, bAutoSave ? 1 : 0);
	}

	static const TCHAR* InjectionTemplate = TEXT(R"JS(
(function() {
	var SENTINEL = '__UE_BRIDGE__';
	var b64 = function(s) { try { return btoa(unescape(encodeURIComponent(s || ''))); } catch (e) { return ''; } };
	// Deliver a bridge sentinel string to C++. Two transports, exactly one fires per message:
	//   1. window.ue.watabou.onbridgemessage(...)  -- the BindUObject bridge. This is the ONLY
	//      channel that reaches C++ on macOS: the WKWebView/AppleWebBrowser backend never forwards
	//      console.log to OnConsoleMessage. Preferred whenever the bound object is present.
	//   2. console.log(...)                         -- fallback, caught by OnConsoleMessage on the
	//      CEF (Windows) backend. Used only when the bound object isn't there.
	// Because we return after (1), C++ never receives an export twice.
	var deliver = function(msg) {
		try {
			var ue = window.ue;
			if (ue && ue.watabou && typeof ue.watabou.onbridgemessage === 'function') {
				ue.watabou.onbridgemessage(msg);
				return;
			}
		} catch (e) { /* fall through to console.log */ }
		console.log(msg);
	};
	var emit = function(event, payload) { deliver(SENTINEL + '|' + event + '|' + b64(payload)); };
	// Capture window.location.search at the moment of export so the editor uses the bundle's
	// authoritative state (seed/tags/params) instead of the panel's URL field, which goes stale
	// when the user tweaks settings inside the embedded browser.
	var currentStateUrl = function() {
		try { return (window.location && window.location.search) || ''; } catch (e) { return ''; }
	};
	// Center-crop the source to a square (min(w,h) per side), then scale to MaxDim.
	// Asset thumbnails in UE are square; aspect-preserving would squish a wide canvas
	// into the square preview slot. Returns an offscreen canvas ready for toDataURL.
	var centerSquareCrop = function(src, srcW, srcH, MaxDim) {
		var sz = Math.max(1, Math.min(srcW || MaxDim, srcH || MaxDim));
		var sx = Math.floor(((srcW || sz) - sz) / 2);
		var sy = Math.floor(((srcH || sz) - sz) / 2);
		var outDim = Math.min(sz, MaxDim);
		var out = document.createElement('canvas');
		out.width = outDim; out.height = outDim;
		out.getContext('2d').drawImage(src, sx, sy, sz, sz, 0, 0, outDim, outDim);
		return out;
	};
	var log = function(msg) { console.log('[WatabouShim] ' + msg); };
	// Default thumbnail capture: try the largest <canvas> first, fall back to <svg>.
	// Bundle-specific JsThumbnailExpr (substituted as __THUMBNAIL_EXPR__) can override.
	// Returns a Promise<string> resolving to a "data:image/png;base64,..." URL, or ''.
	window.__watabou_default_thumbnail = function() {
		try {
			var cs = document.querySelectorAll('canvas');
			var best = null, bestArea = 0;
			// Select by backing-store size only -- bounding-rect filtering would
			// reject the main canvas in hidden / off-screen-positioned hosts
			// (getBoundingClientRect can return zero when the document isn't
			// visibly composited). 1x1 scratch canvases get filtered out implicitly
			// because the main render canvas has a larger backing store.
			for (var i = 0; i < cs.length; ++i) {
				var c = cs[i];
				var a = (c.width || 0) * (c.height || 0);
				if (a > bestArea) { best = c; bestArea = a; }
			}
			// Require a meaningful canvas -- skip 1x1 scratch surfaces entirely;
			// they're never the bundle's render target and produce useless thumbnails.
			if (best && bestArea >= 4) {
				log('thumb: picked canvas ' + best.width + 'x' + best.height);
				var out = centerSquareCrop(best, best.width, best.height, 512);
				return Promise.resolve(out.toDataURL('image/png'));
			}
			var svg = document.querySelector('svg');
			if (svg) {
				var srcW = svg.clientWidth || (svg.viewBox && svg.viewBox.baseVal && svg.viewBox.baseVal.width) || 512;
				var srcH = svg.clientHeight || (svg.viewBox && svg.viewBox.baseVal && svg.viewBox.baseVal.height) || 512;
				var s = new XMLSerializer().serializeToString(svg);
				return new Promise(function(resolve) {
					var img = new Image();
					img.onload = function() {
						try {
							resolve(centerSquareCrop(img, srcW, srcH, 512).toDataURL('image/png'));
						} catch (e) { log('thumb: svg raster failed: ' + String(e)); resolve(''); }
					};
					img.onerror = function() { log('thumb: svg img.onerror'); resolve(''); };
					img.src = 'data:image/svg+xml;base64,' + btoa(unescape(encodeURIComponent(s)));
				});
			}
			log('thumb: no canvas or svg found');
			return Promise.resolve('');
		} catch (e) { log('thumb: default helper threw: ' + String(e)); return Promise.resolve(''); }
	};
	// Capture with a HARD top-level timeout so a stalled requestAnimationFrame can't hang
	// the entire export. rAF can stop firing in CEF when another browser dominates the
	// render process (notably when N>1 pool slots are running concurrently); previously
	// the timeout was nested inside the rAF chain, so if rAF never fired the timeout
	// never armed and the Promise hung -- and with it the bundle's whole saveAs path.
	//
	// We still try to wait two rAFs for the bundle to paint at least once (otherwise the
	// canvas may still be black/empty), but if rAF doesn't fire within the budget the
	// timeout fires and we resolve with an empty thumbnail rather than blocking export.
	var captureThumbnail = function() {
		return new Promise(function(outerResolve) {
			var resolved = false;
			var doResolve = function(v) { if (resolved) { return; } resolved = true; outerResolve(v || ''); };
			// Top-level timeout, runs regardless of rAF firing.
			setTimeout(function(){ doResolve(''); }, 3000);
			requestAnimationFrame(function() {
				requestAnimationFrame(function() {
					if (resolved) { return; }
					var work;
					try { work = __THUMBNAIL_EXPR__; }
					catch (e) { log('thumbnail expr threw: ' + String(e)); doResolve(''); return; }
					if (!work || typeof work.then !== 'function') { work = Promise.resolve(work || ''); }
					work.then(doResolve, function(){ doResolve(''); });
				});
			});
		});
	};
	var emitExport = function(fileName, content) {
		captureThumbnail().then(function(dataUrl) {
			deliver(SENTINEL + '|export|' + b64(fileName || '') + '|' + b64(content || '')
				+ '|' + b64(currentStateUrl()) + '|' + b64(dataUrl || ''));
		}, function() {
			deliver(SENTINEL + '|export|' + b64(fileName || '') + '|' + b64(content || '')
				+ '|' + b64(currentStateUrl()) + '|');
		});
	};
)JS")
	// MSVC C2026 caps string literals at ~16KB; the shim has grown past that with the thumbnail
	// helpers. C++ concatenates adjacent string literals at compile time, so split here.
	TEXT(R"JS(
	try {
		window.__watabou_auto_export = __AUTO_EXPORT_FLAG__;

		var href = (window.location && window.location.href) || '';
		if (href.indexOf('about:') === 0 || href === '' || href === 'about:blank') { return; }

		var hx = window.__hx;
		if (typeof hx !== 'object' || hx === null) {
			log('ERROR: window.__hx not exposed -- bundle patch missing?');
			emit('error', 'window.__hx not exposed');
			return;
		}

		var readyErr = null;
		var isReady = function() {
			try { readyErr = null; return Boolean(__READINESS_EXPR__); }
			catch (e) { readyErr = String(e); return false; }
		};

		// Diagnostic dump only fires when readiness is failing on first probe -- in steady
		// state (every recursive child) we'd be spamming the log otherwise. Surfaces the
		// __hx layout when a new generator's expressions need tuning.
		if (readyErr || !isReady()) {
			log('shim entry  __hx=ok  ready=false  auto=' + window.__watabou_auto_export
				+ (readyErr ? ('  err=' + readyErr) : ''));
			try {
				var hxKeys = Object.keys(hx);
				var pathSeg = '';
				try { pathSeg = (window.location.pathname || '').split('/').filter(function(s){return s;}).slice(-1)[0] || ''; } catch (e) {}
				var related = hxKeys.filter(function(k) {
					var lk = k.toLowerCase();
					return pathSeg && lk.indexOf(pathSeg.toLowerCase().replace(/-/g, '')) >= 0;
				});
				log('hx-introspect: total=' + hxKeys.length
					+ '  path-related=[' + related.slice(0, 12).join(',') + (related.length > 12 ? ',...' : '') + ']');
			} catch (e) { log('hx-introspect failed: ' + String(e)); }
		}

		// Define the saveAs override as a named function so we can self-heal: bundles
		// can re-set window.saveAs back to FileSaver's default during in-place regenerations
		// (no page navigation, so our shim isn't re-injected). The trigger function below
		// checks and reinstalls before firing -- without that, the first Import after a
		// regeneration silently fails because the bundle's saveAs becomes a no-op download.
		window.__watabou_saveas_override = function(blob, fileName, opts) {
			try {
				var reader = new FileReader();
				reader.onload = function() {
					var text = (typeof reader.result === 'string') ? reader.result : '';
					emitExport(fileName || '', text);
				};
				reader.onerror = function() { emit('error', 'FileReader failed for ' + (fileName || '')); };
				reader.readAsText(blob);
			} catch (e) { emit('error', 'saveAs override: ' + String(e)); }
		};
		var ensureSaveAsOverride = function() {
			if (window.saveAs !== window.__watabou_saveas_override) {
				window.saveAs = window.__watabou_saveas_override;
			}
		};
		ensureSaveAsOverride();

		// Expose a manually-callable trigger so the editor's "Import" button can fire export
		// against whatever the user has currently configured in the embedded browser.
		window.__watabou_trigger_export = function() {
			ensureSaveAsOverride();
			if (typeof window.__hx !== 'object' || window.__hx === null) {
				emit('error', 'trigger: __hx not exposed');
				return false;
			}
			try {
				if (!Boolean(__READINESS_EXPR__)) {
					emit('error', 'trigger: bundle not ready');
					return false;
				}
			} catch (e) {
				emit('error', 'trigger: readiness check threw: ' + String(e));
				return false;
			}
			try {
				__TRIGGER_EXPR__;
				return true;
			} catch (e) {
				emit('error', 'trigger threw: ' + String(e));
				return false;
			}
		};

		// Background poll that emits a 'ready' signal once the bundle finishes generating.
		// When __watabou_auto_export is true (Stage 2 / headless flow), it also calls the
		// trigger automatically. The UI's Load button sets it to false, so the user clicks
		// Import explicitly.
		var attempts = 0;
		var poll = function() {
			if (isReady()) {
				if (!window.__watabou_ready_fired) {
					window.__watabou_ready_fired = true;
					emit('ready', '');
					if (window.__watabou_auto_export) {
						window.__watabou_trigger_export();
					}
				}
			} else if (++attempts < 200) {
				setTimeout(poll, 100);
			} else {
				emit('error', 'Timed out waiting for readiness (20s)' + (readyErr ? ' -- last error: ' + readyErr : ''));
			}
		};
		poll();
	} catch (outerErr) {
		try { console.log('[WatabouShim] ERROR outer: ' + String(outerErr)); } catch (e) {}
	}
})();
)JS");

	FString BuildInjectionScript(const FWatabouGeneratorInfo& Gen, bool bAutoExport)
	{
		FString Script = InjectionTemplate;
		Script.ReplaceInline(TEXT("__AUTO_EXPORT_FLAG__"),
			bAutoExport ? TEXT("true") : TEXT("false"), ESearchCase::CaseSensitive);
		Script.ReplaceInline(TEXT("__READINESS_EXPR__"), *Gen.JsReadinessExpr, ESearchCase::CaseSensitive);
		Script.ReplaceInline(TEXT("__TRIGGER_EXPR__"),   *Gen.JsTriggerExpr,   ESearchCase::CaseSensitive);

		// Empty JsThumbnailExpr falls back to the shim's auto-detect helper.
		const FString ThumbExpr = Gen.JsThumbnailExpr.IsEmpty()
			? FString(TEXT("window.__watabou_default_thumbnail()"))
			: Gen.JsThumbnailExpr;
		Script.ReplaceInline(TEXT("__THUMBNAIL_EXPR__"), *ThumbExpr, ESearchCase::CaseSensitive);
		return Script;
	}

	static FString DecodeBase64Utf8(const FString& Base64)
	{
		TArray<uint8> Bytes;
		if (!FBase64::Decode(Base64, Bytes) || Bytes.Num() == 0) { return FString(); }
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Converter.Length(), Converter.Get());
	}

	bool TryParseBridgeMessage(
		const FString& Message,
		FString& OutEvent,
		FWatabouExportPayload& OutPayload,
		FString& OutErrorMessage)
	{
		static const FString Sentinel = TEXT("__UE_BRIDGE__|");
		if (!Message.StartsWith(Sentinel)) { return false; }

		TArray<FString> Parts;
		Message.ParseIntoArray(Parts, TEXT("|"), /*InCullEmpty=*/false);
		if (Parts.Num() < 2) { return false; }

		OutEvent = Parts[1];

		if (OutEvent == TEXT("export") && Parts.Num() >= 4)
		{
			OutPayload.FileName         = DecodeBase64Utf8(Parts[2]);
			OutPayload.Content          = DecodeBase64Utf8(Parts[3]);
			OutPayload.StateUrl         = Parts.Num() >= 5 ? DecodeBase64Utf8(Parts[4]) : FString();
			OutPayload.ThumbnailDataUrl = Parts.Num() >= 6 ? DecodeBase64Utf8(Parts[5]) : FString();
			return true;
		}
		if (OutEvent == TEXT("ready"))
		{
			return true;
		}
		if (OutEvent == TEXT("error") && Parts.Num() >= 3)
		{
			OutErrorMessage = DecodeBase64Utf8(Parts[2]);
			return true;
		}
		return false;
	}
}
