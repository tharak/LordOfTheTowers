// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "IWatabouParser.h"

/**
 * Parser for Watabou One Page Dungeon JSON output (format 1.2.x).
 *
 * Top-level fields:
 *   version, title, story    -- header strings
 *   rects[]                  -- {x, y, w, h, rotunda?, ending?} (tile-aligned)
 *   doors[]                  -- {x, y, dir:{x,y}, type:int}
 *   notes[]                  -- {text, ref, pos:{x,y}}          (pos is float)
 *   columns[]                -- {x, y}                          (float coords)
 *   water[]                  -- {x, y}                          (tile-aligned)
 *
 * Fills the generic UWatabouAssetBase. Each rect is emitted TWICE: as a Polygon
 * OUTLINE in "rects" (the four corners, or a tessellated circle for a rotunda)
 * AND as a MultiPoints TILE-OCCUPANCY cloud in "tiles" (one point per covered
 * tile center, circle-masked for rotundas). Doors/columns/water/notes become
 * Point features in their own subcollections, with native attributes
 * (rotunda/ending flags, door dir/type, note text/ref) carried in
 * FWatabouFeatureDetails. Geometry is baked in RAW native coordinates; the X
 * flip + door wall offset + facing live in FOnePageDungeonProcessor. OPD is
 * standalone in the recursive seed graph, so no seed links are decoded.
 *
 * Two derived attributes are reconstructed at parse time (OPD has no native
 * floor concept -- see ComputeFloorPlan): every rect carries a "level" (int,
 * surface entrances = 0, one lower per stair descended), and stair doors --
 * plus their floor tiles -- carry a "stair_kind" name. Declared stairs drive
 * the levels; each door is relabeled by the gap it spans: "step" (one level,
 * incl. a flat door promoted to a stair), "ladder" (>1), "flat" (a declared
 * stair that turned out level), plus "entrance" / "down_exit". A level never
 * changes across a door unless it is a step/ladder. Both load by key.
 */
class WATABOUONEPAGEDUNGEON_API FOnePageDungeonParser : public IWatabouParser
{
public:
	FOnePageDungeonParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;
	virtual TArray<FString> GetSupportedVersions() const override { return { TEXT("1.2.7") }; }
};
