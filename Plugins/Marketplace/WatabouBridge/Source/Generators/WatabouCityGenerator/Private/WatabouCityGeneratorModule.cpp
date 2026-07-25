// Copyright 2026 Timothé Lapetite

#include "WatabouCityGeneratorModule.h"
#include "CityGeneratorInfo.h"
#include "WatabouGeneratorRegistry.h"

void FWatabouCityGeneratorModule::StartupModule()
{
	FWatabouGeneratorRegistry::Get().Register(MakeShared<FCityGeneratorInfo>());
}

void FWatabouCityGeneratorModule::ShutdownModule()
{
	FWatabouGeneratorRegistry::Get().Unregister(TEXT("city-generator"));
}

IMPLEMENT_MODULE(FWatabouCityGeneratorModule, WatabouCityGenerator)
