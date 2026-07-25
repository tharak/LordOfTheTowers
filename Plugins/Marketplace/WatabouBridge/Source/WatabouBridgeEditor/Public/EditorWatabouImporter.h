// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "IWatabouImporter.h"

/**
 * Editor implementation of IWatabouImporter.
 *
 * Stubbed today -- the next iteration drives a headless SWebBrowser through the
 * same shim used by SWatabouImportPanel, parses the resulting JSON via a
 * per-generator parser (each per-generator module will register one), and
 * saves a UWatabouAssetBase subclass to a deterministic path derived from
 * Seed.GetCanonicalKey().
 *
 * Registered as the active importer in FWatabouBridgeEditorModule::StartupModule.
 */
class FEditorWatabouImporter : public IWatabouImporter
{
public:
	virtual TFuture<FWatabouImportResult> Resolve(const FWatabouSeedRef& Seed) override;
	virtual bool IsAvailable() const override { return true; }
};
