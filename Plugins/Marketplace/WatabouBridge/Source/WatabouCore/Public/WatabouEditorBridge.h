// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

struct FWatabouSeedRef;

/**
 * Editor-UI hooks that runtime/Core types want to fire but only the editor module can
 * implement. Mirrors the IWatabouImporter::SetActive inversion (and the DECLARE_DELEGATE_*
 * hooks in WatabouAssetBuilder): Core declares the hook, FWatabouBridgeEditorModule binds it
 * at startup and clears it at shutdown.
 *
 * In a cooked / runtime build with no editor module, the delegate stays unbound and the
 * CallInEditor entry points that fire it are silent no-ops.
 */
DECLARE_DELEGATE_OneParam(FWatabouOpenInImporterDelegate, const FWatabouSeedRef& /*Seed*/);

struct WATABOUCORE_API FWatabouEditorBridge
{
	/**
	 * Open (spawn-or-focus) the Watabou Import window, switch to the seed's generator tab, and
	 * restore the seed's settings into it. Bound by the editor module; unbound elsewhere.
	 */
	static FWatabouOpenInImporterDelegate OnOpenInImporter;
};
