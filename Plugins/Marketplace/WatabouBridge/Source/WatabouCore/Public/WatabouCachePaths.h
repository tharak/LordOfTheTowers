// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

/**
 * Path helpers for the versioned bundle cache.
 *
 * Cache lives at:  <Plugin>/Content/Bundles/
 *
 * This path is chosen so the cache SHIPS WITH PACKAGED BUILDS (Content/ is staged
 * automatically, and WatabouCore.Build.cs adds an explicit RuntimeDependencies
 * entry marking the bundles as NonUFS). Runtime JIT generation (Win64/Mac, future)
 * can therefore reach the same data the editor uses without redownload.
 *
 * Per-generator layout:
 *   <Cache>/<generator-id>/
 *     ├── _active.txt          single-line file, contents = active version string
 *     ├── 1.8.0/               versioned bundle tree (matches Watabou JS layout)
 *     │     ├── .ok            empty marker: this version passed the smoke test (known-good)
 *     │     ├── index.html
 *     │     ├── Perilous.js
 *     │     └── Assets/...
 *     └── 1.9.0/               older versions are kept until manually pruned
 *
 * The HTTP server resolves each generator's serving root from this cache plus the SHIPPED
 * snapshots (<Plugin>/Resources/Bundles/<id>/ -- pre-patched, tracked in source control and
 * redistributed with watabou's permission; see Resources/Bundles/NOTICE.md):
 *   1. active     -- <Cache>/<id>/<_active>/ if present and the bundle JS is patched, UNLESS
 *                    the shipped snapshot's bundle version is strictly newer (the plugin was
 *                    updated past a stale download) -- then the shipped snapshot serves first
 *   2. last-good  -- newest other version carrying a .ok marker (FindNewestVerifiedVersion)
 *   3. shipped    -- <Plugin>/Resources/Bundles/<id>/, the unconditional floor
 * Nothing on this path touches the network; downloads happen only through the explicit
 * "Update Generators" action. Tiers 1-2 live under Content/ (staged for packaged builds);
 * tier 3 is staged via an explicit RuntimeDependencies entry so it works in packaged
 * builds too.
 *
 * NOTE: tier 2 is currently DORMANT -- nothing calls MarkVersionVerified yet (the post-update
 * smoke-test verifier is not implemented), so no .ok markers exist and resolution is effectively
 * active <-> shipped. With the shipped snapshots now guaranteed present and pre-tested, that
 * verifier is mostly redundant; the plumbing stays for a possible future use.
 *
 * Gitignore: <Plugin>/Content/Bundles/ (this cache) is excluded from source control --
 * machine-local download state. The shipped Resources/Bundles/ snapshots ARE tracked.
 * Packaging vs source control are independent: RuntimeDependencies controls what ships
 * into cooked builds, .gitignore controls what's tracked.
 */
namespace WatabouCachePaths
{
	/** Absolute path of the cache root. Created on disk if missing when WriteActiveVersion runs. */
	WATABOUCORE_API FString GetCacheRoot();

	/** Per-generator cache directory: <Cache>/<GeneratorId>/. */
	WATABOUCORE_API FString GetGeneratorCacheRoot(const FString& GeneratorId);

	/** Versioned cache directory: <Cache>/<GeneratorId>/<Version>/. */
	WATABOUCORE_API FString GetVersionCacheDir(const FString& GeneratorId, const FString& Version);

	/** Staging directory used by the updater while a download is in flight: <Cache>/<GeneratorId>/.staging-<token>/. */
	WATABOUCORE_API FString GetStagingDir(const FString& GeneratorId, const FString& StagingToken);

	/** Path to the _active.txt pointer file: <Cache>/<GeneratorId>/_active.txt. */
	WATABOUCORE_API FString GetActiveVersionFilePath(const FString& GeneratorId);

	/** Read the active version string. Returns empty if the file is missing or empty. */
	WATABOUCORE_API FString ReadActiveVersion(const FString& GeneratorId);

	/** Write the active version, creating directories as needed. */
	WATABOUCORE_API bool WriteActiveVersion(const FString& GeneratorId, const FString& Version);

	/** Enumerate versions present on disk for a generator (each is a subdirectory). */
	WATABOUCORE_API TArray<FString> EnumerateVersions(const FString& GeneratorId);

	/** Path to a version's "known-good" marker: <Cache>/<GeneratorId>/<Version>/.ok. */
	WATABOUCORE_API FString GetVerifiedMarkerPath(const FString& GeneratorId, const FString& Version);

	/** True if the version carries a .ok marker (i.e. it passed the post-update smoke test). */
	WATABOUCORE_API bool IsVersionVerified(const FString& GeneratorId, const FString& Version);

	/** Write the .ok marker for a version. Intended for the post-update smoke gate on a pass --
	 *  NOT yet wired (no caller), so tier 2 of resolution stays dormant for now. */
	WATABOUCORE_API bool MarkVersionVerified(const FString& GeneratorId, const FString& Version);

	/**
	 * Most recently verified version for a generator, by .ok marker timestamp (newest first),
	 * optionally excluding one version (typically the active one). Empty if none are verified.
	 * This is the "last known good" backup tier.
	 */
	WATABOUCORE_API FString FindNewestVerifiedVersion(const FString& GeneratorId, const FString& ExcludeVersion = FString());
}
