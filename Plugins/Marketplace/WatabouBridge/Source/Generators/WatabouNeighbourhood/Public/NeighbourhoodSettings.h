// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorSettings.h"
#include "NeighbourhoodSettings.generated.h"

/**
 * Import settings for Neighbourhood. No generator-specific scalar params: size is chosen via the
 * size tags (small/medium/large) -- the URL "size" key is only a write-only mirror of that tag and
 * is never read back -- and palette/render toggles are client-side State options, not URL params.
 * Seed + name (base) + tags (FNeighbourhoodInfo::GetTagGroups) cover the full URL contract.
 */
USTRUCT()
struct FNeighbourhoodSettings : public FWatabouGeneratorSettings
{
	GENERATED_BODY()
};
