// Copyright 2026 Timothé Lapetite

#include "WatabouPerilousShoresModule.h"
#include "PerilousShoresInfo.h"
#include "WatabouGeneratorRegistry.h"

void FWatabouPerilousShoresModule::StartupModule()
{
	FWatabouGeneratorRegistry::Get().Register(MakeShared<FPerilousShoresInfo>());
}

void FWatabouPerilousShoresModule::ShutdownModule()
{
	FWatabouGeneratorRegistry::Get().Unregister(TEXT("perilous-shores"));
}

IMPLEMENT_MODULE(FWatabouPerilousShoresModule, WatabouPerilousShores)
