// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorInfo.h"

/**
 * Watabou Urban Places (generator short code "up"). Emits GeoJSON-shaped
 * output (FeatureCollection with values / earth / roads / squares / prisms /
 * buildings / trees) -- handled unchanged by FWatabouGeometryParser.
 */
struct WATABOUURBANPLACES_API FUrbanPlacesInfo : public FWatabouGeneratorInfo
{
	FUrbanPlacesInfo();

	virtual TSharedPtr<IWatabouParser> CreateParser() const override;
	virtual FWatabouResourceManifest GetResourceManifest() const override;
	virtual FInstancedStruct CreateDefaultSettings() const override;
	virtual TArray<FWatabouTagGroup> GetTagGroups() const override;
};
