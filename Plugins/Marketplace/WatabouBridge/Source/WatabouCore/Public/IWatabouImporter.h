// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "WatabouSeedRef.h"

class UWatabouAssetBase;

/**
 * Result of an import attempt.
 *
 * The Asset pointer is null when the resolver failed; the Error field carries
 * a human-readable reason.
 */
struct WATABOUCORE_API FWatabouImportResult
{
	TWeakObjectPtr<UWatabouAssetBase> Asset;
	FString Error;

	bool Succeeded() const { return Asset.IsValid() && Error.IsEmpty(); }
};

/**
 * Abstract importer: given a FWatabouSeedRef, produce a UWatabouAssetBase.
 *
 * Two implementations exist:
 *   - FEditorWatabouImporter (in WatabouBridgeEditor): uses CEF to drive the
 *     embedded browser, captures the JSON, parses it, saves a new uasset.
 *   - FRuntimeWatabouImporter (future, in WatabouBridgeRuntime): same approach
 *     but at game-runtime on platforms where CEF is available (Win64/Mac).
 *
 * Callers (PCG nodes especially) consume only this interface and don't care
 * which implementation is in scope. In a cooked build without the runtime
 * importer, the registry exposes nothing and resolvers fall back to cache-only.
 *
 * Implementations register themselves via SetActiveImporter at module
 * startup. Only one can be active per process.
 */
class WATABOUCORE_API IWatabouImporter
{
public:
	virtual ~IWatabouImporter() = default;

	/** Returns a future that resolves once the import completes (success or failure). */
	virtual TFuture<FWatabouImportResult> Resolve(const FWatabouSeedRef& Seed) = 0;

	/** Returns true if this importer can run on the current platform / build configuration. */
	virtual bool IsAvailable() const = 0;

	/** Globally-registered importer instance. May be null in cooked builds without runtime CEF. */
	static IWatabouImporter* GetActive();
	static void SetActive(IWatabouImporter* Importer);
};
