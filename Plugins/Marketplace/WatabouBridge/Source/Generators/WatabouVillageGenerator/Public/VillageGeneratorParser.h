// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeometryParser.h"

/**
 * Village Generator (generator "vg"). Adds the Dwellings linking pass: every
 * building polygon receives a "dwellings" seed ref matching what Village's
 * runtime "Open in Dwellings" menu would generate. Byte-match assumes the
 * GeoJSON building order matches region.buildings -- it does, in 1.6.6.
 */
class WATABOUVILLAGEGENERATOR_API FVillageGeneratorParser : public FWatabouGeometryParser
{
public:
	FVillageGeneratorParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;
	virtual TArray<FString> GetSupportedVersions() const override { return { TEXT("1.6.6") }; }
};
