// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorInfo.h"

/**
 * Watabou Village Generator (Cinzel/ShareTech variant). Emits GeoJSON-shaped
 * output that the base FWatabouGeometryParser handles unchanged.
 */
struct WATABOUVILLAGEGENERATOR_API FVillageGeneratorInfo : public FWatabouGeneratorInfo
{
	FVillageGeneratorInfo();

	virtual TSharedPtr<IWatabouParser> CreateParser() const override;
	virtual FWatabouResourceManifest GetResourceManifest() const override;
	virtual FInstancedStruct CreateDefaultSettings() const override;
	virtual TArray<FWatabouTagGroup> GetTagGroups() const override;
};
