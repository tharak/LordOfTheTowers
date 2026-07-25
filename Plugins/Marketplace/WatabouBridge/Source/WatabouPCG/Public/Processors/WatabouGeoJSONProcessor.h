// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Processors/WatabouProcessor.h"

/**
 * Generic GeoJSON-shaped processor: the identity instance of FWatabouProcessorBase.
 *
 * Serves the city-family generators (City / Village / Urban Places / Neighbourhood), whose parsers
 * already emit world-space float coordinates flipped to UE's left-handed convention, AND doubles as
 * the unknown-generator fallback (any asset whose generator has no registered processor).
 *
 * It adds nothing to the base: ProjectPoint / OrientDir are identity and SupportedBakes() is None, so
 * none of the bakes fire (no floor-z / facing / alignment) -- raw 2D coords project to (x, y, 0). The
 * generic placement semantics still apply (the asset anchor, default Bounds Center, and unit point
 * bounds), so this is NOT byte-for-byte the old PCGExLoadWatabouData emission -- that legacy path was
 * only a reference to kickstart the rewrite, not an invariant. All emission logic lives in
 * FWatabouProcessorBase.
 */
class WATABOUPCG_API FWatabouGeoJSONProcessor : public FWatabouProcessorBase
{
};
