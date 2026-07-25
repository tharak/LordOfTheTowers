// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeometryParser.h"

/**
 * Neighbourhood emits standard GeoJSON (FeatureCollection with values / earth /
 * roads / buildings / prisms / squares / greens / trees / districts), handled
 * unchanged by FWatabouGeometryParser. This subclass adds a Dwellings-linking
 * post-pass on the buildings collection.
 *
 * Recipe (from com.watabou.neighbourhood.model.Building.open + getPlan):
 *   CellSizeDivisor       = 10.0
 *   MaxCells              = 16
 *   bSwapURLAxes          = true   (URL "w" <- len0, "h" <- len1)
 *   bEnsureLongAxisV0     = true   (Building ctor swaps to keep len0 >= len1)
 *   bReverseV0InSampling  = false  (getPlan walks V0 left-to-right)
 *   ParentSeed            = parse(SourceSeed.Seed)  (hood.seed + buildings.indexOf(this))
 *   FromMarker            = "neighbourhoods"
 *
 * Caveat: when the upstream bundle flags a building as `large`, its PiP test
 * runs against each wing's rect (runtime decomposition not in the GeoJSON).
 * Plans for large manors will diverge from upstream; the link still opens a
 * valid Dwellings page, just with a footprint sampled from the outer polygon.
 */
class WATABOUNEIGHBOURHOOD_API FNeighbourhoodParser : public FWatabouGeometryParser
{
public:
	FNeighbourhoodParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;
	virtual TArray<FString> GetSupportedVersions() const override { return { TEXT("1.2.2") }; }
};
