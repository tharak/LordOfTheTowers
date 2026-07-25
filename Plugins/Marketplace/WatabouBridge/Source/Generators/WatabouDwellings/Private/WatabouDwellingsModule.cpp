// Copyright 2026 Timothé Lapetite

#include "WatabouDwellingsModule.h"
#include "DwellingsInfo.h"
#include "WatabouGeneratorRegistry.h"

void FWatabouDwellingsModule::StartupModule()
{
	FWatabouGeneratorRegistry::Get().Register(MakeShared<FDwellingsInfo>());
}

void FWatabouDwellingsModule::ShutdownModule()
{
	FWatabouGeneratorRegistry::Get().Unregister(TEXT("dwellings"));
}

IMPLEMENT_MODULE(FWatabouDwellingsModule, WatabouDwellings)
