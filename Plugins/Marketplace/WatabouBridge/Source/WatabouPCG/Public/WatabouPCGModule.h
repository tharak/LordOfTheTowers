// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Consumption module for WatabouBridge: PCG nodes that read UWatabouAssetBase
 * data assets and emit PCG point / path data.
 *
 * Hosts the processor framework (IWatabouProcessor + the generic GeoJSON
 * processor used as the unknown-generator fallback) and the "Load Watabou Data"
 * PCG node. Per-generator processor subclasses live in their own generator
 * modules and are produced lazily via FWatabouGeneratorInfo::CreateProcessor().
 *
 * Depends on PCG, not PCGEx. The "PCGEx-friendly" conventions we need (IsClosed /
 * IsHole @Data attributes, one-data-per-path) are reproduced locally in
 * WatabouPCGData, keeping this module free of any PCGExtendedToolkit dependency.
 */
class FWatabouPCGModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
