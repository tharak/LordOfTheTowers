// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

class UWatabouAssetBase;

/**
 * Editor-side hook registration for WatabouAssetBuilder. Module calls Register
 * in StartupModule (and Unregister in ShutdownModule), wiring up the
 * UnrealEd / ImageWrapper / Slate-adjacent operations that Core can't see.
 *
 * Private to the editor module -- runtime never instantiates these.
 */
namespace WatabouAssetBuilderHooks
{
	void Register();
	void Unregister();

	/** Synchronous save of one asset's package (RF_Public|RF_Standalone, SAVE_None). The single save
	 *  implementation, shared by the deferred import save and the recursive importer's relink pass so
	 *  save policy never drifts between them. Logs and returns false on failure. */
	bool SavePackageNow(UWatabouAssetBase* Asset);
}
