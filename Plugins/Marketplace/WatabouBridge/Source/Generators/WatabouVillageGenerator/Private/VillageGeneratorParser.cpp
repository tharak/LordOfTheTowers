// Copyright 2026 Timothé Lapetite

#include "VillageGeneratorParser.h"
#include "DwellingsPlanEncoder.h"
#include "WatabouAssetBase.h"
#include "WatabouBridgeSettings.h"

bool FVillageGeneratorParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	if (!FWatabouGeometryParser::Parse(InJson, OutAsset, OutError))
	{
		return false;
	}

	const UWatabouBridgeSettings* Settings = GetDefault<UWatabouBridgeSettings>();

	DwellingsPlanEncoder::FOptions Options;
	Options.FromMarker          = TEXT("vg");
	Options.CellSizeDivisor     = Settings->ResolveCellSizeDivisor(Settings->Village);
	Options.MaxCells            = Settings->ResolveMaxCells(Settings->Village);
	Options.bSkipPointInPolygon = true;
	Options.SetParentSeedFromAsset(OutAsset);

	DwellingsPlanEncoder::RunBuildingsPass(OutAsset, Options, TEXT("FVillageGeneratorParser"));
	return true;
}
