// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeometryParser.h"

/**
 * Parser for Medieval Fantasy City Generator (mfcg) JSON output. Adds the
 * Dwellings linking pass on top of the base geometry parser.
 *
 * Seed is BEST-EFFORT, NOT byte-exact: the bundle composes
 *   bp.seed + cells.indexOf(patch) + group.blocks.indexOf(block) + d.indexOf(b)
 * per ward type, but the GeoJSON exports a flat building list with no
 * ward/patch/block tags. Our seed = parent + globalBuildingIndex matches the
 * single-mansion-per-patch case only.
 */
class WATABOUCITYGENERATOR_API FCityGeneratorParser : public FWatabouGeometryParser
{
public:
	FCityGeneratorParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;
};
