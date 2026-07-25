// Copyright 2026 Timothé Lapetite

#include "UrbanPlacesParser.h"
#include "DwellingsPlanEncoder.h"
#include "WatabouAssetBase.h"
#include "WatabouBridgeSettings.h"

bool FUrbanPlacesParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	if (!FWatabouGeometryParser::Parse(InJson, OutAsset, OutError))
	{
		return false;
	}

	const UWatabouBridgeSettings* Settings = GetDefault<UWatabouBridgeSettings>();

	DwellingsPlanEncoder::FOptions Options;
	Options.FromMarker      = TEXT("urban");
	Options.CellSizeDivisor = Settings->ResolveCellSizeDivisor(Settings->UrbanPlaces);
	Options.MaxCells        = Settings->ResolveMaxCells(Settings->UrbanPlaces);

	DwellingsPlanEncoder::RunBuildingsPass(OutAsset, Options, TEXT("FUrbanPlacesParser"));
	return true;
}
