// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "IWatabouParser.h"

/**
 * Parser for Watabou Dwellings JSON output.
 *
 * Dwellings does NOT emit GeoJSON. Its export is a small floor-stack model:
 *   floors[]                   -- one entry per level
 *     level                    -- int
 *     rooms[]   {name, cells[{i,j}]}
 *     doors[]   {edge:{cell:{i,j}, dir}, type?}
 *     windows[] {cell:{i,j}, dir}
 *     stairs[]  (shape inferred; sample is empty)
 *   exit        {cell:{i,j}, dir}
 *
 * Fills the generic UWatabouAssetBase. One subcollection per floor (id
 * "floor_<level>") with nested subcollections for rooms / doors / windows /
 * stairs. Rooms become MultiPoint features (one coord per cell). Doors,
 * windows, stairs become Point features with cell + direction in details.
 * Cell coords are native (i, j); consumers project as needed. Dwellings has
 * no nested seed links (terminal in the recursive graph).
 */
class WATABOUDWELLINGS_API FDwellingsParser : public IWatabouParser
{
public:
	FDwellingsParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;
};
