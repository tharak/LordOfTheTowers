// Copyright 2026 Timothé Lapetite

#include "Processors/WatabouGeoJSONProcessor.h"

// FWatabouGeoJSONProcessor is the identity instance of FWatabouProcessorBase -- it overrides nothing.
// All emission logic (the recursive walk, per-type emit, single-point merge, and the opt-in transform
// bakes) lives in FWatabouProcessorBase (WatabouProcessor.cpp). This translation unit only anchors the
// header's inclusion within the module; there is no GeoJSON-specific behavior to implement.
