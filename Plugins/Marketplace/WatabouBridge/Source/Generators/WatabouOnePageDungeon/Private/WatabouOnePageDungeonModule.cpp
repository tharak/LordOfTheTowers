// Copyright 2026 Timothé Lapetite

#include "WatabouOnePageDungeonModule.h"
#include "OnePageDungeonInfo.h"
#include "WatabouGeneratorRegistry.h"

void FWatabouOnePageDungeonModule::StartupModule()
{
	FWatabouGeneratorRegistry::Get().Register(MakeShared<FOnePageDungeonInfo>());
}

void FWatabouOnePageDungeonModule::ShutdownModule()
{
	FWatabouGeneratorRegistry::Get().Unregister(TEXT("one-page-dungeon"));
}

IMPLEMENT_MODULE(FWatabouOnePageDungeonModule, WatabouOnePageDungeon)
