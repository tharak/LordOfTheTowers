// Copyright 2026 Timothé Lapetite

#include "WatabouNeighbourhoodModule.h"
#include "NeighbourhoodInfo.h"
#include "WatabouGeneratorRegistry.h"

void FWatabouNeighbourhoodModule::StartupModule()
{
	FWatabouGeneratorRegistry::Get().Register(MakeShared<FNeighbourhoodInfo>());
}

void FWatabouNeighbourhoodModule::ShutdownModule()
{
	FWatabouGeneratorRegistry::Get().Unregister(TEXT("neighbourhood"));
}

IMPLEMENT_MODULE(FWatabouNeighbourhoodModule, WatabouNeighbourhood)
