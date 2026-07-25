// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

/**
 * Hard limits of the (patched) Watabou Dwellings bundle, shared across modules.
 *
 * Lives in WatabouCore so both the editor-side patch (FDwellingsInfo / the resource updater,
 * which rewrites com.watabou.dwellings.model.Specs.MAX_SIZE to this value) and the import-time
 * settings resolver (UWatabouBridgeSettings, which must never let a per-generator cap exceed it)
 * can reference one source of truth. Core cannot depend on the Dwellings module, so the constant
 * cannot live there.
 */
namespace WatabouDwellingLimits
{
	/**
	 * Per-axis cell cap the patched Dwellings bundle accepts (Specs.MAX_SIZE). Stock Watabou ships 11;
	 * the bundle patch raises it to this so a single dwelling can span more cells. Any w/h emitted
	 * beyond this is REJECTED by the bundle's qc.fromCode (it substitutes a random shape), so every
	 * generator's resolved Max Cells is clamped to this ceiling.
	 *
	 * Change here, then re-run "Update Generators" (re-bakes the cached bundle to this value) or
	 * hand-edit the two live Dwellings.js copies' `ta.MAX_SIZE=NN`. Keep the ClampMax meta on
	 * UWatabouBridgeSettings::MaxCells (and FWatabouDwellingTuning::MaxCells) in sync with this.
	 */
	inline constexpr int32 BundleMaxCells = 64;
}
