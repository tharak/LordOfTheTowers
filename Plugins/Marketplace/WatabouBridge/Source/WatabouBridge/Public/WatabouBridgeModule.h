// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Umbrella module for the WatabouBridge plugin.
 *
 * Responsibilities:
 *   - Owns the LogWatabou category (declared in WatabouBridge.h, defined in the .cpp).
 *   - Future home for UWatabouBridgeSettings (Project Settings -> Plugins -> Watabou Bridge).
 *   - Future home for plugin-wide hooks that aren't tied to Core data types or
 *     editor UI specifically.
 *
 * Loaded at PostConfigInit so the log category is available to every dependent
 * module's StartupModule().
 */
class FWatabouBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
