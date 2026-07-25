// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FWatabouCityGeneratorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
