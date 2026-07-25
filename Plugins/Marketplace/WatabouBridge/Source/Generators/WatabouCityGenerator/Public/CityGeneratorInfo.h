// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorInfo.h"

/**
 * Watabou Medieval Fantasy City Generator (mfcg) -- city plan generator with
 * walls, districts, streets, plazas, gates.
 *
 * Unique among the five: Export.asJSON() is self-contained (no arg, internally
 * fetches City.instance and runs the export). Other generators take the model
 * explicitly.
 */
struct WATABOUCITYGENERATOR_API FCityGeneratorInfo : public FWatabouGeneratorInfo
{
	FCityGeneratorInfo();

	virtual TSharedPtr<IWatabouParser> CreateParser() const override;
	virtual FWatabouResourceManifest GetResourceManifest() const override;
	virtual FInstancedStruct CreateDefaultSettings() const override;
};
