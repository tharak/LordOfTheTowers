// Copyright 2026 Timothé Lapetite

#include "WatabouUrbanPlacesModule.h"
#include "UrbanPlacesInfo.h"
#include "WatabouGeneratorRegistry.h"

void FWatabouUrbanPlacesModule::StartupModule()
{
	FWatabouGeneratorRegistry::Get().Register(MakeShared<FUrbanPlacesInfo>());
}

void FWatabouUrbanPlacesModule::ShutdownModule()
{
	FWatabouGeneratorRegistry::Get().Unregister(TEXT("urban-places"));
}

IMPLEMENT_MODULE(FWatabouUrbanPlacesModule, WatabouUrbanPlaces)
