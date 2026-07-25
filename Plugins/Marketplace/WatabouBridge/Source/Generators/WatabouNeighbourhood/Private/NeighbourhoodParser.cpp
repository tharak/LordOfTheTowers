// Copyright 2026 Timothé Lapetite

#include "NeighbourhoodParser.h"
#include "DwellingsPlanEncoder.h"
#include "WatabouAssetBase.h"
#include "WatabouBridgeSettings.h"

bool FNeighbourhoodParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	if (!FWatabouGeometryParser::Parse(InJson, OutAsset, OutError))
	{
		return false;
	}

	const UWatabouBridgeSettings* Settings = GetDefault<UWatabouBridgeSettings>();

	DwellingsPlanEncoder::FOptions Options;
	Options.FromMarker           = TEXT("neighbourhoods");
	Options.CellSizeDivisor      = Settings->ResolveCellSizeDivisor(Settings->Neighbourhood);
	Options.MaxCells             = Settings->ResolveMaxCells(Settings->Neighbourhood);
	Options.bSwapURLAxes         = true;
	Options.bEnsureLongAxisV0    = true;
	Options.bReverseV0InSampling = false;
	Options.SetParentSeedFromAsset(OutAsset);

	DwellingsPlanEncoder::RunBuildingsPass(OutAsset, Options, TEXT("FNeighbourhoodParser"));
	return true;
}
