// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

struct FWatabouFeatureDetails;

/**
 * Canonical facing decode shared by every generator's consumption path.
 *
 * Watabou generators encode a feature's direction NON-uniformly: Dwellings stores a cardinal string
 * (NameValues["dir"] = n/s/e/w, occasionally diagonals); One Page Dungeon stores a vector
 * (NumericValues["dir_x"/"dir_y"]). ReadDirection unifies both into one native-space 2D vector so a
 * single processor can turn any of them into a facing yaw.
 *
 * The returned vector is in the generator's NATIVE 2D space (Watabou screen space: +X = east,
 * +Y = south for the cardinal form). It is decode-only: handedness flips and the yaw convention are
 * the consumer's concern (the processor applies the same flip it uses for positions, then atan2).
 */
namespace WatabouFacing
{
	/**
	 * Decode a feature's facing into a native-space direction. Tries the cardinal-string form first,
	 * then the {dir_x, dir_y} vector form. Returns false when the feature carries no usable direction
	 * (e.g. a room, or a degenerate zero vector) -- callers then apply no facing rotation.
	 */
	WATABOUCORE_API bool ReadDirection(const FWatabouFeatureDetails& Details, FVector2D& OutDir);
}
