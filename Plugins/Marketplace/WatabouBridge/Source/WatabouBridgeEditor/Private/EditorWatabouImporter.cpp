// Copyright 2026 Timothé Lapetite

#include "EditorWatabouImporter.h"
#include "WatabouBridge.h"

TFuture<FWatabouImportResult> FEditorWatabouImporter::Resolve(const FWatabouSeedRef& Seed)
{
	UE_LOG(LogWatabou, Warning,
		TEXT("FEditorWatabouImporter::Resolve called for %s -- not yet implemented"),
		*Seed.GetCanonicalKey());

	TPromise<FWatabouImportResult> Promise;
	FWatabouImportResult Result;
	Result.Error = TEXT("EditorWatabouImporter not yet implemented (headless flow lands with first PCG node)");
	Promise.SetValue(MoveTemp(Result));
	return Promise.GetFuture();
}
