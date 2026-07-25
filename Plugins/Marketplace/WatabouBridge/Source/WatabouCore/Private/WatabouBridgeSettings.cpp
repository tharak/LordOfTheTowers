// Copyright 2026 Timothé Lapetite

#include "WatabouBridgeSettings.h"

// Factory defaults preserve each generator's historical behavior (all overridable in Project Settings):
// Neighbourhood sampled at a coarser 10.0 divisor; City fills up to the full patched bundle cap;
// Neighbourhood capped at 16. City / Village / Urban Places otherwise track the shared CellSizeDivisor
// (4.0); Village / Urban Places track the shared MaxCells (11). Anything not overridden here follows the
// shared values, so flipping a generator's toggle off returns it to the common metric.
UWatabouBridgeSettings::UWatabouBridgeSettings()
{
	
}
