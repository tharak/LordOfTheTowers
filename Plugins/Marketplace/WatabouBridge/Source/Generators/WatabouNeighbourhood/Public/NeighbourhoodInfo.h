// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorInfo.h"

/**
 * Watabou Neighbourhood generator. Emits GeoJSON-shaped output (FeatureCollection
 * with values / districts / buildings / greens / trees / pins) with "Open in
 * Dwellings" support per building. The Dwellings recipe diverges from existing
 * generators (divisor=10, clamp=[1,16], non-reversed v0 sampling axis) and is
 * routed through the encoder's option knobs -- see FNeighbourhoodParser.
 */
struct WATABOUNEIGHBOURHOOD_API FNeighbourhoodInfo : public FWatabouGeneratorInfo
{
	FNeighbourhoodInfo();

	virtual TSharedPtr<IWatabouParser> CreateParser() const override;
	virtual FWatabouResourceManifest GetResourceManifest() const override;
	virtual FInstancedStruct CreateDefaultSettings() const override;
	virtual TArray<FWatabouTagGroup> GetTagGroups() const override;
};
