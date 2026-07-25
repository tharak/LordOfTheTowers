// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

struct FWatabouGeneratorInfo;

/**
 * Central registry of all known Watabou generators.
 *
 * Per-generator modules call Register() during StartupModule. The editor's
 * import picker and the resolver service both query through this registry --
 * neither needs to know which generator modules are linked into the build.
 *
 * Thread-safety: registration must happen on the game thread (during module
 * load). Reads after registration are lock-free.
 */
class WATABOUCORE_API FWatabouGeneratorRegistry
{
public:
	static FWatabouGeneratorRegistry& Get();

	void Register(TSharedRef<FWatabouGeneratorInfo> Info);
	void Unregister(const FString& Id);

	const TArray<TSharedRef<FWatabouGeneratorInfo>>& GetAll() const { return Generators; }

	TSharedPtr<FWatabouGeneratorInfo> FindById(const FString& Id) const;

private:
	TArray<TSharedRef<FWatabouGeneratorInfo>> Generators;
};
