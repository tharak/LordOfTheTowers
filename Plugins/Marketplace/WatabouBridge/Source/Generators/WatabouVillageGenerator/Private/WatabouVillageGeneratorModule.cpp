// Copyright 2026 Timothé Lapetite

#include "WatabouVillageGeneratorModule.h"
#include "VillageGeneratorInfo.h"
#include "WatabouGeneratorRegistry.h"

void FWatabouVillageGeneratorModule::StartupModule()
{
	FWatabouGeneratorRegistry::Get().Register(MakeShared<FVillageGeneratorInfo>());
}

void FWatabouVillageGeneratorModule::ShutdownModule()
{
	FWatabouGeneratorRegistry::Get().Unregister(TEXT("village-generator"));
}

IMPLEMENT_MODULE(FWatabouVillageGeneratorModule, WatabouVillageGenerator)
