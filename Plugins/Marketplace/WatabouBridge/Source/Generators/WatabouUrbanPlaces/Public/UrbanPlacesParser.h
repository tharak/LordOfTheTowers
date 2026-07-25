// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeometryParser.h"

/**
 * Urban Places (generator "up") emits standard GeoJSON, so the geometry parser
 * handles the whole shape unchanged. This subclass adds one post-pass: after
 * the feature tree is built, every building polygon receives a Dwellings seed
 * ref encoding its footprint (mirroring Watabou's "Open in Dwellings" UI
 * action). The seed refs land in each Feature's Details.StructValues["dwellings"].
 */
class WATABOUURBANPLACES_API FUrbanPlacesParser : public FWatabouGeometryParser
{
public:
	FUrbanPlacesParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;
	virtual TArray<FString> GetSupportedVersions() const override { return { TEXT("1.2.1") }; }
};
