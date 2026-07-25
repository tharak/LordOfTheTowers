// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorInfo.h"

/**
 * Watabou Perilous Shores -- map generator with hexes, biomes, rivers, towns, dungeons.
 *
 * Hex-grid JSON shape -- not GeoJSON -- decoded by FPerilousShoresParser into
 * the generic UWatabouAssetBase feature tree.
 */
struct WATABOUPERILOUSSHORES_API FPerilousShoresInfo : public FWatabouGeneratorInfo
{
	FPerilousShoresInfo();

	virtual TSharedPtr<IWatabouParser> CreateParser() const override;
	virtual TSharedPtr<IWatabouProcessor> CreateProcessor() const override;
	virtual FWatabouResourceManifest GetResourceManifest() const override;
	virtual FInstancedStruct CreateDefaultSettings() const override;
	virtual TArray<FWatabouTagGroup> GetTagGroups() const override;
};
