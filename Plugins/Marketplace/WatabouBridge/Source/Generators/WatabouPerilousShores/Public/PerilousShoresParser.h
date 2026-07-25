// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "IWatabouParser.h"

/**
 * Parser for Perilous Shores 1.8.0 JSON output.
 *
 * Perilous Shores does NOT emit GeoJSON. Its output is a hex-grid model with top-level fields:
 *   bp          -- blueprint (width, height, tags)
 *   layout      -- hex layout ("odd-r", etc.)
 *   hexes       -- dict<hex_key, {q, r, terrain, optional town/danger links}>
 *   rivers      -- dict<river_key, {parent, channel[]}>  (channel = shared-edge keys)
 *   roads       -- dict<road_key, hex_key[]>  (searoutes has the same shape)
 *   features    -- array of {name, hexes[]} -- named regions / biomes
 *
 * Every keyed reference is resolved into geometry, all threaded through the SAME hex centres:
 *   hexes         -- Point per hex at its centre (q/r/terrain in details).
 *   rivers        -- LineString along the channel's shared-edge midpoints.
 *   roads         -- LineString through the member hex centres.
 *   named_regions -- MultiPoints of the member hex centres.
 *
 * Hex centre source: the bundle's RENDERED x/y when present (emitted by the getHex patch -- see
 * FPerilousShoresInfo -- with per-seed warp + rotation baked in, so it matches the render exactly for every
 * hex mode), else a canonical odd-r projection of q/r (correct topology, but can't reproduce the warp).
 *
 * Raw key chains (channel / path / hex_keys) and town/danger seed-ref links are kept in details. Geometry is
 * built here (not in the processor) because it needs per-map data (layout + rendered positions);
 * FPerilousShoresProcessor only folds the per-hex features into one cloud per id.
 */
class WATABOUPERILOUSSHORES_API FPerilousShoresParser : public IWatabouParser
{
public:
	FPerilousShoresParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;
	virtual TArray<FString> GetSupportedVersions() const override { return { TEXT("1.8.0") }; }
};
