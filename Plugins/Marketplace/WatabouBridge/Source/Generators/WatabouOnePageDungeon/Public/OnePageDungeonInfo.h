// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorInfo.h"

/**
 * Watabou One Page Dungeon Generator -- dungeon room/corridor layouts.
 *
 * Note: this generator uses lowercase "assets/" (the others use capital
 * "Assets/"). Case-sensitive HTTP servers must serve the right case; UE's
 * HTTP server is case-sensitive on Linux but case-insensitive on Windows.
 */
struct WATABOUONEPAGEDUNGEON_API FOnePageDungeonInfo : public FWatabouGeneratorInfo
{
	FOnePageDungeonInfo();

	virtual TSharedPtr<IWatabouParser> CreateParser() const override;
	virtual TSharedPtr<IWatabouProcessor> CreateProcessor() const override;
	virtual FWatabouResourceManifest GetResourceManifest() const override;
	virtual FInstancedStruct CreateDefaultSettings() const override;
	virtual TArray<FWatabouTagGroup> GetTagGroups() const override;
};
