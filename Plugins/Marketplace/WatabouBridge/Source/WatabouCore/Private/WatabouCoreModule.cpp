// Copyright 2026 Timothé Lapetite

#include "WatabouCoreModule.h"
#include "WatabouBridge.h"

void FWatabouCoreModule::StartupModule()
{
	UE_LOG(LogWatabou, Verbose, TEXT("WatabouCore loaded"));
}

void FWatabouCoreModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FWatabouCoreModule, WatabouCore)
