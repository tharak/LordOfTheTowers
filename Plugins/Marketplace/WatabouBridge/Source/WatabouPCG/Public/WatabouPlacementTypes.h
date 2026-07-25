// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouPlacementTypes.generated.h"

/**
 * Which point of a stamped asset (in its projected-local footprint) lands on the resolved placement.
 *
 * The mirror of EWatabouDataOrigin: Data Origin chooses the placement on the SEED side, Asset Anchor
 * chooses the reference point on the ASSET side. Applies to EVERY stamp (per-point and @Data), since
 * it's an intrinsic property of the asset. XY only -- Z stays at the asset's own z=0 (level-0 ground),
 * so floor-z stacks up from the placement plane and basements stay below it.
 */
UENUM(BlueprintType)
enum class EWatabouAssetAnchor : uint8
{
	BoundsCenter UMETA(DisplayName = "Bounds Center", ToolTip = "AABB center of the asset's projected footprint (XY). Centers the asset on the placement."),
	Centroid     UMETA(DisplayName = "Centroid", ToolTip = "Average of the asset's projected coordinates (XY) -- biased toward dense areas."),
	Origin       UMETA(DisplayName = "Origin", ToolTip = "The asset's local origin (0,0); legacy behavior -- may sit at a footprint corner."),
};
