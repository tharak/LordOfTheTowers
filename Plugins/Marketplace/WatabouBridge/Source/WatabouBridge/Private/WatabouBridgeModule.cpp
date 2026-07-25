// Copyright 2026 Timothé Lapetite

#include "WatabouBridgeModule.h"
#include "WatabouBridge.h"

DEFINE_LOG_CATEGORY(LogWatabou);

void FWatabouBridgeModule::StartupModule()
{
	UE_LOG(LogWatabou, Verbose, TEXT("WatabouBridge umbrella module loaded"));
}

void FWatabouBridgeModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FWatabouBridgeModule, WatabouBridge)
