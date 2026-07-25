// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorInfo.h"

struct WATABOUDWELLINGS_API FDwellingsInfo : public FWatabouGeneratorInfo
{
	FDwellingsInfo();

	virtual TSharedPtr<IWatabouParser> CreateParser() const override;
	virtual TSharedPtr<IWatabouProcessor> CreateProcessor() const override;
	virtual FWatabouResourceManifest GetResourceManifest() const override;
	virtual FInstancedStruct CreateDefaultSettings() const override;
	virtual TArray<FWatabouTagGroup> GetTagGroups() const override;
};
