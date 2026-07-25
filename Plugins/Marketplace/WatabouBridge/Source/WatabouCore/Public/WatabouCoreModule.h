// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Core module for WatabouBridge.
 *
 * Hosts the cross-generator registry (FWatabouGeneratorRegistry), shared data
 * types (FWatabouSeedRef, IWatabouDataModel, UWatabouAssetBase), the cache
 * lookup service (FWatabouSeedCache), and the importer interface
 * (IWatabouImporter).
 *
 * Per-generator modules register their FWatabouGeneratorInfo with the registry
 * during their own StartupModule.
 *
 * Pure runtime -- no editor / CEF / HTTP dependencies. Editor-side implementation
 * of IWatabouImporter lives in WatabouBridgeEditor; the future runtime
 * implementation will live in WatabouBridgeRuntime.
 */
class FWatabouCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
