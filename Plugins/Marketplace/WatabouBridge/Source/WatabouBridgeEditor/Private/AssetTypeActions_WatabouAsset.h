// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

/**
 * Asset right-click actions for UWatabouAssetBase. Adds "Resolve Linked Content
 * (Recursive)" which kicks off FWatabouRecursiveImporter::StartFromAsset on the
 * module-owned importer against the selected asset, runs in the background via
 * the module's headless browser pool regardless of whether the import panel is
 * open. Uses the importer's current Settings (mutated via the panel UI).
 */
class FAssetTypeActions_WatabouAsset : public FAssetTypeActions_Base
{
public:
	virtual FText    GetName() const override;
	virtual UClass*  GetSupportedClass() const override;
	virtual FColor   GetTypeColor() const override { return FColor(120, 160, 220); }
	virtual uint32   GetCategories() override { return EAssetTypeCategories::Misc; }

	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return true; }
	virtual void GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder) override;
};
