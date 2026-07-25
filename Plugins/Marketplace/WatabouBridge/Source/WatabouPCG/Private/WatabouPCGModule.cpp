// Copyright 2026 Timothé Lapetite

#include "WatabouPCGModule.h"

// No startup registration: the generic GeoJSON processor is the in-module fallback
// (instantiated on demand by the Load node), and per-generator processors register
// themselves lazily through FWatabouGeneratorInfo::CreateProcessor() from their own
// generator modules. The UPCGSettings node is reflection-discovered by PCG.

void FWatabouPCGModule::StartupModule()
{
}

void FWatabouPCGModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FWatabouPCGModule, WatabouPCG)
