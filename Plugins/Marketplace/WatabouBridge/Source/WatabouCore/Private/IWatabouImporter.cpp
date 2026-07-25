// Copyright 2026 Timothé Lapetite

#include "IWatabouImporter.h"

namespace WatabouImporter_Internal
{
	static IWatabouImporter* GActive = nullptr;
}

IWatabouImporter* IWatabouImporter::GetActive()
{
	return WatabouImporter_Internal::GActive;
}

void IWatabouImporter::SetActive(IWatabouImporter* Importer)
{
	WatabouImporter_Internal::GActive = Importer;
}
