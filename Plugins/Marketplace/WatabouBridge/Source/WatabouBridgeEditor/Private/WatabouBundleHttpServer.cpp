// Copyright 2026 Timothé Lapetite

#include "WatabouBundleHttpServer.h"
#include "WatabouBridge.h"
#include "WatabouCachePaths.h"
#include "WatabouGeneratorInfo.h"
#include "WatabouGeneratorRegistry.h"
#include "WatabouVersionUtils.h"

#include "HttpServerModule.h"
#include "HttpPath.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"

namespace WatabouBundleHttpServer_Internal
{
	static const TMap<FString, FString>& GetMimeMap()
	{
		static const TMap<FString, FString> Map = {
			{ TEXT("html"), TEXT("text/html; charset=utf-8") },
			{ TEXT("htm"),  TEXT("text/html; charset=utf-8") },
			{ TEXT("js"),   TEXT("application/javascript; charset=utf-8") },
			{ TEXT("mjs"),  TEXT("application/javascript; charset=utf-8") },
			{ TEXT("css"),  TEXT("text/css; charset=utf-8") },
			{ TEXT("json"), TEXT("application/json; charset=utf-8") },
			{ TEXT("txt"),  TEXT("text/plain; charset=utf-8") },
			{ TEXT("svg"),  TEXT("image/svg+xml") },
			{ TEXT("png"),  TEXT("image/png") },
			{ TEXT("jpg"),  TEXT("image/jpeg") },
			{ TEXT("jpeg"), TEXT("image/jpeg") },
			{ TEXT("gif"),  TEXT("image/gif") },
			{ TEXT("ico"),  TEXT("image/x-icon") },
			{ TEXT("woff"), TEXT("font/woff") },
			{ TEXT("woff2"),TEXT("font/woff2") },
			{ TEXT("ttf"),  TEXT("font/ttf") },
			{ TEXT("otf"),  TEXT("font/otf") },
			{ TEXT("eot"),  TEXT("application/vnd.ms-fontobject") },
			{ TEXT("wasm"), TEXT("application/wasm") },
			{ TEXT("wav"),  TEXT("audio/wav") },
			{ TEXT("mp3"),  TEXT("audio/mpeg") },
			{ TEXT("ogg"),  TEXT("audio/ogg") },
		};
		return Map;
	}

	static FString InferContentType(const FString& DiskPath)
	{
		const FString Ext = FPaths::GetExtension(DiskPath).ToLower();
		const FString* Found = GetMimeMap().Find(Ext);
		return Found ? *Found : FString(TEXT("application/octet-stream"));
	}

	static void NormalizeSlashes(FString& Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	}

	// Cheap integrity gate used during resolution: the bundle JS must carry the window.__hx patch,
	// or the embedded shim aborts at import time. A present-but-unpatched bundle (un-patchable
	// upstream change, corrupted cache) is treated as broken so resolution falls to the next tier.
	// Empty BundleScript -> can't check -> assume intact.
	//
	// Scans raw bytes for the ASCII marker rather than LoadFileToString: the bundle JS is multi-MB
	// and BindAllStaticRoutes loads+converts it again, so a full UTF-8->TCHAR conversion here would
	// be wasted work on every server (re)start (incl. each post-Update restart).
	static bool IsBundleIntact(const FString& Dir, const FString& BundleScript)
	{
		if (BundleScript.IsEmpty()) { return true; }
		FString JsPath = FPaths::Combine(Dir, BundleScript);
		NormalizeSlashes(JsPath);

		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *JsPath)) { return false; }

		static const ANSICHAR Marker[] = "window.__hx=";
		const int32 MarkerLen = UE_ARRAY_COUNT(Marker) - 1;   // drop the trailing NUL
		if (Bytes.Num() < MarkerLen) { return false; }
		const uint8* const Data = Bytes.GetData();
		for (int32 i = 0, Last = Bytes.Num() - MarkerLen; i <= Last; ++i)
		{
			if (FMemory::Memcmp(Data + i, Marker, MarkerLen) == 0) { return true; }
		}
		return false;
	}

	// Bundle version of the JS inside Dir, or empty when the file / version marker is absent.
	// Full LoadFileToString of a multi-MB bundle -- only called when BOTH an active cache and a
	// shipped snapshot exist for a generator (the only case the version comparison decides).
	static FString ReadBundleVersionInDir(const FString& Dir, const FString& BundleScript)
	{
		if (BundleScript.IsEmpty()) { return FString(); }
		FString JsPath = FPaths::Combine(Dir, BundleScript);
		NormalizeSlashes(JsPath);

		FString JsText;
		if (!FFileHelper::LoadFileToString(JsText, *JsPath)) { return FString(); }
		return WatabouVersionUtils::ExtractBundleVersion(JsText);
	}
}

bool FWatabouBundleHttpServer::Start(uint16 PreferredPort)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("HTTPServer")))
	{
		FModuleManager::Get().LoadModule(TEXT("HTTPServer"));
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WatabouBridge"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogWatabou, Error, TEXT("Cannot find plugin WatabouBridge (IPluginManager)."));
		return false;
	}
	VendoredRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Bundles"));
	FPaths::NormalizeDirectoryName(VendoredRoot);
	WatabouBundleHttpServer_Internal::NormalizeSlashes(VendoredRoot);

	// Acquire a router on a port we can actually bind. GetHttpRouter(port, bFailOnBindFailure=true)
	// returns null when the OS port is already in use, so we retry on fresh ephemeral ports rather
	// than reporting "running" on a port nothing is listening to (StartAllListeners returns void,
	// so a bind failure would otherwise go undetected).
	uint16 PortToUse = 0;
	constexpr int32 MaxBindAttempts = 8;
	for (int32 Attempt = 0; Attempt < MaxBindAttempts && !Router.IsValid(); ++Attempt)
	{
		const uint16 Candidate = (PreferredPort != 0 && Attempt == 0) ? PreferredPort : ChooseEphemeralPort();
		Router = FHttpServerModule::Get().GetHttpRouter(Candidate, /*bFailOnBindFailure=*/true);
		if (Router.IsValid())
		{
			PortToUse = Candidate;
		}
		else
		{
			UE_LOG(LogWatabou, Verbose, TEXT("WatabouBundleHttpServer: port %u unavailable -- retrying"), Candidate);
		}
	}
	if (!Router.IsValid())
	{
		UE_LOG(LogWatabou, Error,
			TEXT("WatabouBundleHttpServer: failed to bind a listener after %d attempts"), MaxBindAttempts);
		return false;
	}

	const int32 NumBound = BindAllStaticRoutes();
	if (NumBound == 0)
	{
		UE_LOG(LogWatabou, Warning,
			TEXT("No routes bound -- run \"Update Generators\" to populate the cache, or check Resources/Bundles."));
	}

	FHttpServerModule::Get().StartAllListeners();
	Port = PortToUse;
	UE_LOG(LogWatabou, Log,
		TEXT("WatabouBundleHttpServer: routes=%d   listening on 127.0.0.1:%u"), NumBound, Port);
	return true;
}

void FWatabouBundleHttpServer::ResolveGeneratorSources(TArray<FGeneratorSource>& Out) const
{
	using namespace WatabouBundleHttpServer_Internal;

	for (const TSharedRef<FWatabouGeneratorInfo>& Info : FWatabouGeneratorRegistry::Get().GetAll())
	{
		const FString& BundleScript = Info->BundleScript;

		FString VendoredDir = FPaths::Combine(VendoredRoot, Info->Id);
		NormalizeSlashes(VendoredDir);
		const bool bHasVendoredDir = IFileManager::Get().DirectoryExists(*VendoredDir);

		// Accept a cached <id>/<version>/ dir only if it exists AND its bundle JS is patched; a
		// broken/unpatched version is skipped so resolution drops to the next fallback tier.
		auto TryCacheVersion = [&Out, &Info, &BundleScript](const FString& Version, const TCHAR* TierLabel) -> bool
		{
			if (Version.IsEmpty()) { return false; }
			FString Dir = WatabouCachePaths::GetVersionCacheDir(Info->Id, Version);
			NormalizeSlashes(Dir);
			if (!IFileManager::Get().DirectoryExists(*Dir)) { return false; }
			if (!IsBundleIntact(Dir, BundleScript))
			{
				UE_LOG(LogWatabou, Warning,
					TEXT("[%s] %s version '%s' present but unpatched/broken -- falling back to the next tier."),
					*Info->Id, TierLabel, *Version);
				return false;
			}
			FGeneratorSource Src;
			Src.GeneratorId = Info->Id;
			Src.AbsoluteDir = Dir;
			Src.SourceLabel = FString::Printf(TEXT("%s:%s"), TierLabel, *Version);
			Out.Add(MoveTemp(Src));
			return true;
		};

		// The shipped snapshot (Resources/Bundles/<id>/, pre-patched). bRequireIntact when it
		// competes against a healthy active cache; unconditional when it's the last-resort floor,
		// where serving a diagnosable broken bundle (bind-time warning) beats serving nothing.
		auto TryVendored = [&Out, &Info, &BundleScript, &VendoredDir, bHasVendoredDir](const bool bRequireIntact) -> bool
		{
			if (!bHasVendoredDir) { return false; }
			if (bRequireIntact && !IsBundleIntact(VendoredDir, BundleScript))
			{
				UE_LOG(LogWatabou, Warning,
					TEXT("[%s] shipped bundle present but unpatched/broken -- preferring the cache."),
					*Info->Id);
				return false;
			}
			FGeneratorSource Src;
			Src.GeneratorId = Info->Id;
			Src.AbsoluteDir = VendoredDir;
			Src.SourceLabel = TEXT("vendored");
			Out.Add(MoveTemp(Src));
			return true;
		};

		// Priority: the ACTIVE cache (the user's last explicit "Update Generators" download) is
		// primary, EXCEPT when the shipped snapshot is provably newer -- no active at all (fresh
		// install), or both bundle versions parse and shipped > active (the plugin was updated
		// past a stale download). "Can't prove newer" (unknown-<timestamp> labels, a future
		// version-marker change) conservatively keeps the cache primary, so an explicit update
		// always takes effect. No tier touches the network.
		const FString ActiveVersion = WatabouCachePaths::ReadActiveVersion(Info->Id);
		bool bVendoredOutranksCache = bHasVendoredDir && ActiveVersion.IsEmpty();
		if (bHasVendoredDir && !ActiveVersion.IsEmpty())
		{
			const FString VendoredVersion = ReadBundleVersionInDir(VendoredDir, BundleScript);
			bVendoredOutranksCache = WatabouVersionUtils::IsStrictlyNewerVersion(VendoredVersion, ActiveVersion);
			if (bVendoredOutranksCache)
			{
				UE_LOG(LogWatabou, Log,
					TEXT("[%s] shipped bundle %s is newer than the active cache %s -- serving the shipped copy."),
					*Info->Id, *VendoredVersion, *ActiveVersion);
			}
		}

		if (bVendoredOutranksCache && TryVendored(/*bRequireIntact=*/true)) { continue; }

		if (TryCacheVersion(ActiveVersion, TEXT("cache"))) { continue; }

		// Last known good: newest OTHER version carrying a .ok marker. DORMANT for now -- nothing
		// writes .ok yet (the post-update smoke-test verifier isn't implemented), so this returns
		// empty and we fall straight to the shipped snapshot. Plumbed for when that verifier lands.
		const FString KnownGood = WatabouCachePaths::FindNewestVerifiedVersion(Info->Id, ActiveVersion);
		if (TryCacheVersion(KnownGood, TEXT("known-good"))) { continue; }

		// Floor: the shipped snapshot, served unconditionally (re-attempted even if the strict pass
		// above rejected it -- a diagnosable broken bundle still beats no routes at all).
		if (TryVendored(/*bRequireIntact=*/false)) { continue; }

		UE_LOG(LogWatabou, Warning,
			TEXT("[%s] no source directory available (cache absent/broken + shipped bundle missing) -- this generator won't serve until you run Update Generators."),
			*Info->Id);
	}
}

bool FWatabouBundleHttpServer::BindFileRoute(const FString& UrlPath, const TSharedRef<TArray<uint8>>& Bytes, const FString& ContentType)
{
	FHttpRouteHandle Handle = Router->BindRoute(
		FHttpPath(UrlPath),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateLambda(
			[Bytes, ContentType](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
			{
				TArray<uint8> Copy = *Bytes;
				TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(MoveTemp(Copy), ContentType);
				Response->Code = EHttpServerResponseCodes::Ok;
				Response->Headers.FindOrAdd(TEXT("Cache-Control")).Add(TEXT("no-store"));
				OnComplete(MoveTemp(Response));
				return true;
			}));

	if (Handle.IsValid())
	{
		RouteHandles.Add(Handle);
		UE_LOG(LogWatabou, Verbose, TEXT("  bound %s"), *UrlPath);
		return true;
	}
	UE_LOG(LogWatabou, Warning, TEXT("  FAILED to bind %s"), *UrlPath);
	return false;
}

int32 FWatabouBundleHttpServer::BindAllStaticRoutes()
{
	using namespace WatabouBundleHttpServer_Internal;

	TArray<FGeneratorSource> Sources;
	ResolveGeneratorSources(Sources);

	int32 BoundCount = 0;
	for (const FGeneratorSource& Src : Sources)
	{
		const int32 BoundCountBefore = BoundCount;
		UE_LOG(LogWatabou, Log, TEXT("  generator %s  source=%s  dir=%s"),
			*Src.GeneratorId, *Src.SourceLabel, *Src.AbsoluteDir);

		const TSharedPtr<FWatabouGeneratorInfo> Info = FWatabouGeneratorRegistry::Get().FindById(Src.GeneratorId);
		FString BundleJsAbs;
		if (Info.IsValid() && !Info->BundleScript.IsEmpty())
		{
			BundleJsAbs = FPaths::Combine(Src.AbsoluteDir, Info->BundleScript);
			NormalizeSlashes(BundleJsAbs);
		}

		TArray<FString> AllFiles;
		IFileManager::Get().FindFilesRecursive(AllFiles, *Src.AbsoluteDir, TEXT("*"),
			/*Files=*/true, /*Directories=*/false);

		for (const FString& File : AllFiles)
		{
			FString Normalized = File;
			NormalizeSlashes(Normalized);
			if (!Normalized.StartsWith(Src.AbsoluteDir)) { continue; }

			// "/perilous-shores/Assets/default.json" -- generator id prefix + relative path within source dir.
			FString FileRel = Normalized.RightChop(Src.AbsoluteDir.Len());
			if (FileRel.StartsWith(TEXT("/"))) { FileRel.RightChopInline(1); }
			if (FileRel.IsEmpty()) { continue; }
			// Skip cache bookkeeping files.
			if (FileRel.StartsWith(TEXT("_")) || FileRel.StartsWith(TEXT("."))) { continue; }

			TSharedRef<TArray<uint8>> Bytes = MakeShared<TArray<uint8>>();
			if (!FFileHelper::LoadFileToArray(*Bytes, *Normalized))
			{
				UE_LOG(LogWatabou, Warning, TEXT("  [%s] failed to read %s -- skipped"), *Src.GeneratorId, *Normalized);
				continue;
			}

			// Sanity-check the bundle JS: it must contain the window.__hx patch marker, or the
			// embedded shim aborts with "window.__hx not exposed". Surfaces silent regressions
			// (corrupted cache, unpatched vendored copy, future Watabou bundle that bypasses
			// the universal regex). Reuses the bytes we just loaded -- no extra read.
			if (!BundleJsAbs.IsEmpty() && Normalized == BundleJsAbs)
			{
				FString JsText;
				FFileHelper::BufferToString(JsText, Bytes->GetData(), Bytes->Num());
				if (!JsText.Contains(TEXT("window.__hx=")))
				{
					UE_LOG(LogWatabou, Warning,
						TEXT("  [%s] %s is MISSING the window.__hx patch -- imports will fail. Run Update Generators to repatch."),
						*Src.GeneratorId, *Normalized);
				}
			}

			// Force preserveDrawingBuffer:true on every WebGL context the bundle creates.
			// Without this, the bundle's render-and-present cycle discards the back buffer,
			// and thumbnail capture (canvas.toDataURL / drawImage(canvas, ...)) reads pure
			// transparent pixels. Must run BEFORE the bundle's scripts execute. Serve-time
			// injection makes this work for vendored, freshly-cached, and old-cached bundles
			// alike. Idempotent via the comment marker.
			if (FileRel.EndsWith(TEXT("index.html")))
			{
				FString HtmlText;
				FFileHelper::BufferToString(HtmlText, Bytes->GetData(), Bytes->Num());
				static const FString PatchMarker(TEXT("/*__watabou_preserve_drawing_buffer__*/"));
				if (!HtmlText.Contains(PatchMarker))
				{
					static const FString PatchScript(TEXT(
						"<script>"
						"/*__watabou_preserve_drawing_buffer__*/"
						"(function(){"
						"var orig=HTMLCanvasElement.prototype.getContext;"
						"HTMLCanvasElement.prototype.getContext=function(t,a){"
						"if(t==='webgl'||t==='webgl2'||t==='experimental-webgl'){"
						"a=a||{};a.preserveDrawingBuffer=true;"
						"}"
						"return orig.call(this,t,a);"
						"};"
						"})();"
						"</script>"));
					const int32 HeadIdx = HtmlText.Find(TEXT("<head>"), ESearchCase::IgnoreCase);
					if (HeadIdx >= 0)
					{
						HtmlText.InsertAt(HeadIdx + 6, PatchScript);
					}
					else
					{
						HtmlText = PatchScript + HtmlText;
					}
					const FTCHARToUTF8 Converter(*HtmlText);
					Bytes->Reset();
					Bytes->Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
				}
			}

			const FString UrlPath = FString::Printf(TEXT("/%s/%s"), *Src.GeneratorId, *FileRel);
			const FString ContentType = InferContentType(Normalized);

			if (BindFileRoute(UrlPath, Bytes, ContentType)) { ++BoundCount; }

			// index.html also resolves at /<id>/ and /<id>.
			if (UrlPath.EndsWith(TEXT("/index.html")))
			{
				const FString DirWithSlash = UrlPath.LeftChop(FString(TEXT("index.html")).Len());
				if (BindFileRoute(DirWithSlash, Bytes, ContentType)) { ++BoundCount; }

				FString DirNoSlash = DirWithSlash;
				if (DirNoSlash.EndsWith(TEXT("/"))) { DirNoSlash.LeftChopInline(1); }
				if (!DirNoSlash.IsEmpty() && DirNoSlash != DirWithSlash)
				{
					if (BindFileRoute(DirNoSlash, Bytes, ContentType)) { ++BoundCount; }
				}
			}
		}

		// Record that this generator got routes bound this Start() cycle. The panel's
		// ApplyAndLoad uses HasRoutesForGenerator() to decide whether a server restart
		// is needed before navigating.
		if (BoundCount > BoundCountBefore)
		{
			BoundGeneratorIds.Add(Src.GeneratorId);
		}
	}

	return BoundCount;
}

uint16 FWatabouBundleHttpServer::ChooseEphemeralPort()
{
	// Hash the process cycle counter so concurrent editor instances are unlikely to collide.
	// FMath::Rand() is process-seeded once at startup, so two editors launched back-to-back
	// can pick the same port; FPlatformTime::Cycles64 varies per-launch.
	const uint16 LowerBound = 49152;
	const uint16 Range = 65535 - LowerBound;
	const uint32 Mix = GetTypeHash(FPlatformTime::Cycles64());
	return LowerBound + static_cast<uint16>(Mix % Range);
}

void FWatabouBundleHttpServer::Stop()
{
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			if (Handle.IsValid()) { Router->UnbindRoute(Handle); }
		}
	}
	RouteHandles.Reset();
	BoundGeneratorIds.Reset();
	Router.Reset();
	if (Port != 0)
	{
		UE_LOG(LogWatabou, Log, TEXT("WatabouBundleHttpServer: routes released on port %u"), Port);
	}
	Port = 0;
}
