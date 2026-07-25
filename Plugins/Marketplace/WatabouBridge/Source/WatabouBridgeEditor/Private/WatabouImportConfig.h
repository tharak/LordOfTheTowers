// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WatabouImportConfig.generated.h"

/**
 * Per-user, per-project persistence for the Watabou import panel.
 *
 * Each generator's last-used settings are stored as a URL query string -- the exact form
 * FWatabouGeneratorSettings already round-trips through (ToSeedRef -> BuildQueryString, and
 * LoadFrom <- ParseQueryString). Persisting the query string instead of the polymorphic
 * FInstancedStruct keeps config serialization trivial and robust (plain text), and makes the
 * saved form identical to what drives the embedded browser.
 */
UCLASS(config = EditorPerProjectUserSettings)
class UWatabouImportConfig : public UObject
{
	GENERATED_BODY()

public:
	/** The config CDO -- read/write through this. */
	static UWatabouImportConfig* Get();

	/** Last-used query string per generator id (e.g. "?seed=...&w=...&h=..."). */
	UPROPERTY(config)
	TMap<FString, FString> SavedQueries;

	/** Read a saved query; empty string if none stored for this generator. */
	FString GetSavedQuery(const FString& GeneratorId) const;

	/** Store + persist a generator's query string. No-op (no disk write) if unchanged. */
	void SetSavedQuery(const FString& GeneratorId, const FString& Query);

	/** Content folder new top-level imports are saved into (recursive children still nest under
	 *  their parent). Long package path, e.g. "/Game/WatabouImports". */
	UPROPERTY(config)
	FString ImportTargetDir = TEXT("/Game/WatabouImports");

	/** Import target dir, guaranteed non-empty (falls back to the default if unset). */
	FString GetImportTargetDir() const;

	/** Store + persist the import target dir. Empty -> default. No-op (no disk write) if unchanged. */
	void SetImportTargetDir(const FString& Dir);
};
