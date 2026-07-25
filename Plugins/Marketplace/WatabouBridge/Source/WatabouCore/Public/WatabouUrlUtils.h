// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouSeedRef.h"

class FJsonObject;

namespace WatabouUrlUtils
{
	/**
	 * Parse a full Watabou generator URL and produce a FWatabouSeedRef.
	 *
	 * Example:
	 *   "https://watabou.github.io/village-generator/?seed=883754999&name=Sunhill&tags=river&from=perilous"
	 *
	 * Produces:
	 *   GeneratorId = "village-generator"
	 *   Seed        = "883754999"
	 *   Tags        = ["river"]
	 *   Params      = { name = "Sunhill", from = "perilous" }
	 *
	 * Returns false for URLs that don't match the watabou.github.io shape.
	 */
	WATABOUCORE_API bool ParseWatabouUrl(const FString& Url, FWatabouSeedRef& OutRef);

	/**
	 * Parse a query string (with or without leading '?') against a known generator id.
	 * Used by the editor's import panel where the picker selects the generator separately.
	 */
	WATABOUCORE_API FWatabouSeedRef ParseQueryString(const FString& GeneratorId, const FString& Query);

	/** Internal: pull seed/tags/params out of a key=value&key=value string. */
	WATABOUCORE_API void ApplyQueryStringToSeedRef(const FString& Query, FWatabouSeedRef& InOutRef);

	/**
	 * Best human-readable name for a just-exported map, available BEFORE the parser runs so it can
	 * seed the asset's default name. Tries, in order: the 'name' URL param, a top-level "title"
	 * string in the JSON (One Page Dungeon), a top-level "name" string (Perilous Shores). Returns
	 * empty when the generator supplies none -- callers fall back to GetCanonicalHash().
	 */
	WATABOUCORE_API FString PeekFriendlyName(const TSharedPtr<FJsonObject>& JsonObj, const FWatabouSeedRef& Ref);
}
