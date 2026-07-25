// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorSettings.h"
#include "UrbanPlacesSettings.generated.h"

/**
 * Import settings for Urban Places. No generator-specific scalar params: scale is driven by the
 * size tags (small/large) rather than numeric dimensions, and palette/shading are client-side
 * State options, not URL params. Seed + name (base) + tags (FUrbanPlacesInfo::GetTagGroups) cover
 * the full URL contract, so this struct adds nothing of its own.
 */
USTRUCT()
struct FUrbanPlacesSettings : public FWatabouGeneratorSettings
{
	GENERATED_BODY()
};
