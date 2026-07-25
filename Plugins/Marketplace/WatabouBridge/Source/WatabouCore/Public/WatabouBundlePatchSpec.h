// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

/**
 * A secondary find/replace applied to a bundle JS AFTER the universal window.__hx
 * registry patch (see FWatabouBundlePatchSpec::ExtraReplacements). Lets a generator
 * declare bundle tweaks beyond registry exposure -- e.g. raising a hardcoded limit.
 */
struct WATABOUCORE_API FWatabouBundleReplacement
{
	/** Human-readable label, surfaced in the updater log when the patch applies (or warns). */
	FString Label;

	/** Regex matched against the bundle text. Only the FIRST match is rewritten. */
	FString Pattern;

	/**
	 * Replacement for the matched span. $1..$9 expand to capture groups, so context the
	 * pattern had to match (to disambiguate the site) can be carried through unchanged.
	 */
	FString Replacement;

	/**
	 * When true, this patch is LOAD-BEARING: an application matching zero sites REJECTS the bundle
	 * download (the write fails, so the caller keeps the previously cached / vendored copy) and surfaces
	 * an error + an in-editor notification. A 0-match means an upstream bundle change moved the patch
	 * site, which would otherwise silently ship a broken feature (e.g. Dwellings reverting to the stock
	 * MAX_SIZE cap -> random shapes). Set false only for genuinely optional patches -- a 0-match then
	 * just logs a Warning and continues.
	 */
	bool bRequired = true;
};

/**
 * Describes the surgical edit applied to a vendored Watabou bundle JS file so
 * the Haxe class registry becomes visible to our JS shim as window.__hx.
 *
 * Validated empirically across all 5 generators: the pattern
 *   var <X>={},<Y>=function(){return <Z>.__string_rec
 * appears exactly once in every Watabou bundle, at the start of the Haxe IIFE.
 *
 * The vendoring tool reads this struct (or its parallel JSON, see Resources/)
 * and applies the rewrite: var <X>={} becomes var <X>=window.__hx={}.
 *
 * If a future bundle version uses a different output shape, the pattern can be
 * overridden per-generator.
 */
struct WATABOUCORE_API FWatabouBundlePatchSpec
{
	/** The bundle version this patch was authored against, e.g. "1.8.0". Informational. */
	FString BundleVersion;

	/** Regex matching the patch site. Defaults to the universal Haxe pattern. */
	FString PatchSitePattern = TEXT(R"(var\s+([a-zA-Z_$]\w{0,3})\s*=\s*\r?\n?\s*\{\s*\}\s*,\s*([a-zA-Z_$]\w{0,3})\s*=\s*function\(\)\s*\{\s*return\s+([a-zA-Z_$]\w{0,3})\.__string_rec)");

	/** Replacement template. Capture groups from the pattern can be referenced as $1, $2, etc. */
	FString PatchReplacement = TEXT("var $1=window.__hx={},$2=function(){return $3.__string_rec");

	/** Optional sanity-check: which registry letter is expected for this bundle (e.g. "h" or "g"). */
	FString ExpectedRegistryVar;

	/**
	 * Generator-specific rewrites applied to the bundle JS after the universal registry patch,
	 * in order. Empty for most generators. Dwellings uses one to raise Specs.MAX_SIZE (the
	 * per-axis dwelling cell cap) so large building footprints can fill instead of under-filling.
	 */
	TArray<FWatabouBundleReplacement> ExtraReplacements;
};
