// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

namespace WatabouVersionUtils
{
	/**
	 * Extract the bundle version from a Watabou-generated JS file.
	 *
	 * Looks for the pattern  meta.h.version="<version>"  emitted by every Lime/OpenFL
	 * compiled bundle we've inspected (across all 5 generators).
	 *
	 * Returns the version string (e.g. "1.8.0") or empty if not found.
	 */
	WATABOUCORE_API FString ExtractBundleVersion(const FString& BundleJsText);

	/**
	 * True only when BOTH strings parse as dotted numeric versions ("1.8.0", "0.11.5",
	 * optional trailing letter(s) per segment, e.g. "1.2.7b") AND A is strictly newer
	 * than B. Missing segments compare as 0 ("1.8" == "1.8.0"); a segment's trailing
	 * suffix breaks numeric ties upward ("1.2.7b" > "1.2.7").
	 *
	 * Deliberately conservative: any unparseable side (empty, or the cache's
	 * "unknown-<timestamp>" fallback labels) returns false. The bundle-source resolver
	 * treats "can't prove newer" as "not newer", so an unreadable version can never
	 * flip serving priority on its own.
	 */
	WATABOUCORE_API bool IsStrictlyNewerVersion(const FString& A, const FString& B);
}
