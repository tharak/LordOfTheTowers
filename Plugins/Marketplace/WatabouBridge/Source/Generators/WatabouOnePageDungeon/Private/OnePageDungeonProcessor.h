// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Processors/WatabouProcessor.h"
#include "WatabouFeaturesCollection.h"

/**
 * Processor for Watabou One Page Dungeon (tile-grid dungeon model).
 *
 * The parser bakes all geometry in RAW Watabou tile space, so this processor only owns the coordinate
 * projection, facing, and consolidation. Like Perilous Shores it is a standalone flat map: no FloorHeight
 * (no `level`) and no MajorAxisAlign (never a footprint-fitted stamp).
 */
class FOnePageDungeonProcessor : public FWatabouProcessorBase
{
protected:
	// Identity projection: OPD's raw tile coords already match the Watabou preview, so emitting them
	// unflipped lands the dungeon 1:1 with the published page (what you compare against). This DEPARTS
	// from the plugin-wide X-flip-to-UE-left-handed convention (geometry parser / Dwellings / Perilous
	// Shores), so OPD geometry is right-handed -- opposite polygon winding vs those generators. Kept
	// per-generator on purpose: the handedness differences originate in each Watabou generator's own
	// coordinate convention, not here -- if more generators need un-mirroring, unify on a shared
	// projection rather than copying flips. OrientDir matches ProjectPoint (both identity) so door
	// facing + the edge offset stay consistent with positions.
	virtual FVector2D ProjectPoint(const FVector2D& Native) const override { return Native; }
	virtual FVector2D OrientDir(const FVector2D& NativeDir) const override { return NativeDir; }

	virtual EWatabouBake SupportedBakes() const override { return EWatabouBake::Facing; }

	virtual bool UsesEdgeOffset() const override { return true; }

	virtual FVector2D EdgeOffsetLocal(const FVector2D& NativeDir) const override
	{
		// Doors emit at their corridor-tile center; nudge half a tile along dir onto the wall they face
		// (Dwellings cell-boundary pattern -- OPD doors carry no endpoint vertices).
		return OrientDir(NativeDir).GetSafeNormal() * 0.5;
	}

	virtual bool ConsolidatesById() const override { return true; }

	// Notes are flavor text, not geometry: kept in the asset but not emitted or advertised by default.
	virtual void GetDefaultSkipIds(TSet<FName>& OutIds) const override { OutIds.Add(FName(TEXT("notes"))); }

	virtual void GetFeatureTags(const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, TArray<FName>& OutTags) const override
	{
		// Tag rotunda outlines "rotunda" (extra tag, still on the "rects" pin) so round rooms are
		// identifiable without counting vertices -- robust vs future >4-point shapes (e.g. L-rooms).
		if (!Collection) { return; }
		if (const FWatabouFeatureDetails* Details = Collection->ElementsDetails.Find(ElementIndex))
		{
			const double* Rotunda = Details->NumericValues.Find(FName(TEXT("rotunda")));
			if (Rotunda && *Rotunda != 0.0) { OutTags.Add(FName(TEXT("rotunda"))); }
		}
	}
};
