// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Plugin-wide log category. Defined in WatabouBridge.cpp.
 *
 * Every module in this plugin depends on the WatabouBridge umbrella to share this
 * category -- there is only one LogWatabou across Core, Editor, and per-generator
 * modules. Keeps output log filtering simple.
 */
WATABOUBRIDGE_API DECLARE_LOG_CATEGORY_EXTERN(LogWatabou, Log, All);
