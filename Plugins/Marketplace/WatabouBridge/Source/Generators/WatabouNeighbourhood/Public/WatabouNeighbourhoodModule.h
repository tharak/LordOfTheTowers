// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FWatabouNeighbourhoodModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
