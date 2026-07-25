// Copyright 2026 Timothé Lapetite

#include "CityGeneratorParser.h"
#include "DwellingsPlanEncoder.h"
#include "WatabouAssetBase.h"
#include "WatabouBridgeSettings.h"

bool FCityGeneratorParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	if (!FWatabouGeometryParser::Parse(InJson, OutAsset, OutError))
	{
		return false;
	}

	const UWatabouBridgeSettings* Settings = GetDefault<UWatabouBridgeSettings>();

	DwellingsPlanEncoder::FOptions Options;
	Options.FromMarker         = TEXT("city");
	// Cell size + per-axis cap come from Project Settings -> Plugins -> Watabou Bridge: the shared Cell Size
	// Divisor / Max Cells, or City's override when enabled. Bigger divisor -> bigger / fewer cells; the cap
	// bounds how many cells a large building fills before it under-fills (the resolved value is clamped to the
	// patched bundle ceiling, beyond which the bundle would reject the plan). Import-time -- re-import cities.
	Options.CellSizeDivisor    = Settings->ResolveCellSizeDivisor(Settings->City);
	Options.MaxCells           = Settings->ResolveMaxCells(Settings->City);
	Options.bProportionalClamp = true;
	Options.bSwapURLAxes       = true;
	Options.SetParentSeedFromAsset(OutAsset);

	DwellingsPlanEncoder::RunBuildingsPass(OutAsset, Options, TEXT("FCityGeneratorParser"));
	return true;
}
