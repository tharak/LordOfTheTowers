// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorSettings.h"
#include "OnePageDungeonSettings.generated.h"

/**
 * Import settings for One Page Dungeon. No generator-specific scalar params: the only non-tag URL
 * params are "name" (base) and "export" (an auto-download trigger we exclude -- we have our own
 * import flow), and display options live in the client-side State store, not the URL. Seed + name
 * (base) + tags (FOnePageDungeonInfo::GetTagGroups) cover the full URL contract.
 */
USTRUCT()
struct FOnePageDungeonSettings : public FWatabouGeneratorSettings
{
	GENERATED_BODY()
};
