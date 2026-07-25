// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouSeedRef.h"

class UWatabouAssetBase;

/**
 * Encode a 2D polygon outline into a FWatabouSeedRef pointing at the Dwellings
 * generator. Replicates each upstream "Open in Dwellings" URL variant from the
 * bundles:
 *
 *   - Urban Places (com.watabou.urban.model.House.open + getPlan, 1.2.1)
 *   - Village Generator (Xd.open + Xd.getPlan, 1.6.6)
 *   - Medieval Fantasy City Generator (Te.openInDwellings + Te.getPlan)
 *   - Neighbourhood (com.watabou.neighbourhood.model.Building.open + getPlan, 1.2.2)
 *
 * Lives in the Dwellings module so anything that links to Dwellings can call
 * into it without per-generator copies. Pure functions only -- no UE/PCG deps
 * beyond Watabou types.
 *
 * Coordinate convention: by default the encoder expects Unreal-space input
 * (X already flipped by FWatabouGeometryParser) and un-flips internally so
 * plan/seed match what Watabou's UI would produce. Set bUnrealSpaceInput=false
 * to pass native Watabou coords directly.
 *
 * Byte-exactness caveat: the "plan" bitmap is reduced to its largest connected
 * cell component (see EncodePlan) so the generated dwelling can't crash the
 * bundle's room connector. Single-component footprints (the common case) stay
 * byte-identical to upstream; footprints that sample into multiple disjoint
 * components deliberately diverge. City/Neighbourhood seeds are additionally
 * best-effort (see FOptions) where the GeoJSON lacks the bundle's runtime indices.
 */
namespace DwellingsPlanEncoder
{
	/**
	 * Per-generator algorithm knobs. Defaults match Urban Places exactly.
	 *
	 * CellSizeDivisor and MaxCells are NOT baked into these recipes -- each parser resolves them
	 * per-generator from UWatabouBridgeSettings (the shared value, or that generator's override).
	 * The recipes below cover only the structural knobs:
	 *   Urban Places  : defaults (no swap, no fill, centroid seed)
	 *   Village       : bSkipPointInPolygon=true, ParentSeed=parse(Asset->SourceSeed.Seed)
	 *   City          : bProportionalClamp=true, bSwapURLAxes=true,
	 *                   ParentSeed=parse(Asset->SourceSeed.Seed) (best-effort -- the bundle
	 *                   composes ward/patch/block indices that the GeoJSON doesn't carry).
	 *   Neighbourhood : bSwapURLAxes=true, bEnsureLongAxisV0=true, bReverseV0InSampling=false,
	 *                   ParentSeed=parse(Asset->SourceSeed.Seed). Best-effort for buildings
	 *                   flagged `large` upstream (their PiP test runs against runtime-decomposed
	 *                   wings, which the GeoJSON doesn't carry).
	 */
	struct WATABOUDWELLINGS_API FOptions
	{
		/** URL "from=" provenance marker. Omitted if empty. */
		FString FromMarker;

		/** Divides each OBB axis length to decide cell counts (clamped [1,MaxCells]). */
		double  CellSizeDivisor = 4.0;

		/**
		 * Upper bound on cell count along each axis after the divisor. Parsers set this per-generator
		 * from UWatabouBridgeSettings (shared Max Cells or the generator's override, clamped to
		 * WatabouDwellingLimits::BundleMaxCells). Default 11 is the stock baseline.
		 */
		int32   MaxCells = 11;

		/**
		 * If true, mirrors City's clamp behavior: when one cell count > MaxCells,
		 * scale the other down proportionally; if one drops < 1, scale the other
		 * down inversely. Preserves aspect ratio at extremes.
		 */
		bool    bProportionalClamp = false;

		/**
		 * If true, URL "w" is derived from OBB len0 and "h" from len1 (City /
		 * Neighbourhood convention). Default (false) is Urban Places' convention:
		 * "w" <- len1, "h" <- len0.
		 */
		bool    bSwapURLAxes = false;

		/**
		 * If true, the plan bitmap is filled unconditionally (every cell = 1)
		 * instead of running point-in-polygon. Matches Village's algorithm, which
		 * treats every building as a rectangle.
		 */
		bool    bSkipPointInPolygon = false;

		/**
		 * Neighbourhood's Building constructor enforces `len0 >= len1` on the OBB
		 * (`c.len0>c.len1 || this.rect.swapAxes()`). Setting this true makes the
		 * encoder swap V0<->V1 (and Len0<->Len1) post-OBB whenever Len0 < Len1, so
		 * cell sampling lines up with the upstream rect's axis convention. The
		 * existing min-area OBB pass alone doesn't enforce that ordering.
		 */
		bool    bEnsureLongAxisV0 = false;

		/**
		 * Cell-center sampling formula for the V0 axis. Watabou's older generators
		 * (Urban Places, Village, City) walk V0 right-to-left:
		 *   center += v0 * ((cols - col - 1 + 0.5) / cols)
		 * Neighbourhood walks V0 left-to-right:
		 *   center += v0 * ((col + 0.5) / cols)
		 * Defaults to the reversed form to preserve the existing recipes.
		 */
		bool    bReverseV0InSampling = true;

		/**
		 * When set to true, polygon X is negated before computation so that the
		 * Watabou-space arithmetic is applied. Set false only when feeding native
		 * Watabou coords already.
		 */
		bool    bUnrealSpaceInput = true;

		/**
		 * Seed derivation:
		 *  - When ParentSeed < 0 (default): seed = floor(1000 * |centroid.x + centroid.y|),
		 *    matching Urban Places.
		 *  - When ParentSeed >= 0: seed = ParentSeed + ElementIndex, matching
		 *    Village's `region.bp.seed + buildings.indexOf(this)`, City's
		 *    `bp.seed + various indices` (best-effort), and Neighbourhood's
		 *    `hood.seed + hood.buildings.indexOf(this)`.
		 *
		 * ElementIndex is supplied automatically by AttachBuildingSeeds; only the
		 * single-polygon entry point needs the caller to set it.
		 */
		int64   ParentSeed = -1;

		/** Index added to ParentSeed when ParentSeed >= 0. Unused otherwise. */
		int32   ElementIndex = 0;

		/**
		 * Fill ParentSeed from Asset->SourceSeed.Seed when it parses as a
		 * non-negative integer; otherwise leave ParentSeed unchanged (default -1
		 * means "use centroid-based seed"). Mirrors how Watabou's bundles compose
		 * per-building seeds from the parent URL's numeric "seed".
		 */
		void SetParentSeedFromAsset(const UWatabouAssetBase* Asset);
	};

	/**
	 * Build a FWatabouSeedRef (GeneratorId="dwellings") from a single polygon.
	 *
	 * URL params (post-conditions):
	 *  - "w"    : decimal cell count along the OBB axis chosen by bSwapURLAxes
	 *  - "h"    : decimal cell count along the other OBB axis
	 *  - "plan" : hex-packed inside/outside bitmap, LSB-first per nibble
	 *  - "tags" : empty (per-building "tall" flag isn't recoverable from JSON)
	 *  - "from" : Options.FromMarker (omitted if empty)
	 *
	 * Returns false (OutSeedRef left default) if Polygon has fewer than 3
	 * distinct vertices or the OBB collapses.
	 */
	WATABOUDWELLINGS_API bool MakeSeedRefFromPolygon(
		TConstArrayView<FVector2D> Polygon,
		FWatabouSeedRef& OutSeedRef,
		const FOptions& Options = {});

	/**
	 * Walk a parsed asset's feature tree and attach a "dwellings" seed ref to
	 * every Polygon element whose Id matches BuildingId. Each call gets a fresh
	 * Options copy with ElementIndex set to the running per-collection index.
	 *
	 * Seed refs are written into Element.Details.StructValues["dwellings"]; the
	 * caller should run UWatabouAssetBase::AggregateMetadata afterwards.
	 *
	 * Returns the number of elements that received a seed ref.
	 * Existing "dwellings" entries are overwritten (idempotent).
	 */
	WATABOUDWELLINGS_API int32 AttachBuildingSeeds(
		UWatabouAssetBase* InAsset,
		FName BuildingId,
		const FOptions& Options);

	/**
	 * Convenience for parsers: AttachBuildingSeeds + UWatabouAssetBase::AggregateMetadata
	 * + a Log-level diagnostic line. Returns the number of seed refs attached.
	 */
	WATABOUDWELLINGS_API int32 RunBuildingsPass(
		UWatabouAssetBase* InAsset,
		const FOptions& Options,
		const TCHAR* LogTag,
		FName BuildingId = FName(TEXT("buildings")));
}
