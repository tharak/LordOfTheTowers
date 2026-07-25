// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Processors/WatabouProcessor.h"

/**
 * Processor for Watabou Perilous Shores (hex-map model).
 *
 * Projection is identity here: FPerilousShoresParser already bakes the per-map hex layout into the feature
 * coordinates (a stateless ProjectPoint hook can't see the layout), exactly as the city-family parsers emit
 * world coords. So this processor opts in ONLY to per-id consolidation: the hundreds of per-hex Point
 * features fold into one "hexes" cloud, each keeping its own feature key so Load Watabou Properties recovers
 * q / r / terrain / town per hex (the Dwellings treatment). The named_regions MultiPoints fold into one
 * "named_regions" cloud whose points all share their region's key (-> the region name). Roads and rivers are
 * LineStrings, one path per feature.
 */
class FPerilousShoresProcessor : public FWatabouProcessorBase
{
protected:
	// One cloud per id (hexes, named_regions); ConsolidationBucket stays identity (no multi-id buckets like
	// Dwellings' tiles/features). Roads / rivers (LineStrings) stay standalone paths via the base.
	//
	// No static presentation yaw: warp mode rotates by a per-seed random angle absent from the JSON, so no
	// offset can match it. Rendered x/y already bake in the rotation; on the canonical fallback any render-
	// matching orientation is the caller's (seed / node Transform).
	virtual bool ConsolidatesById() const override { return true; }

	// Facing: the parser writes one grid-wide dir_x/dir_y on every hex (the averaged topmost-neighbour
	// direction), and the base applies it as a yaw so each point's +X follows the grid -- natural on warped
	// meshes. Gated by the node's Apply Facing toggle (off -> world-aligned). PS has no floors and is a full
	// map, not a stamped asset, so FloorHeight / MajorAxisAlign stay off.
	virtual EWatabouBake SupportedBakes() const override { return EWatabouBake::Facing; }
};
