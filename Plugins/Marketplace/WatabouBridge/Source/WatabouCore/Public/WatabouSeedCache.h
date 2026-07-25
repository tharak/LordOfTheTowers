// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouSeedRef.h"

class UWatabouAssetBase;

/**
 * Looks up already-imported Watabou uassets by their source seed.
 *
 * Runtime-safe (no editor / CEF / HTTP dependencies). Uses the AssetRegistry's
 * `SourceSeed` UPROPERTY tag (declared `AssetRegistrySearchable` on
 * UWatabouAssetBase) to find matching assets by canonical key.
 *
 * Returns soft pointers so callers can decide whether to sync-load. PCG nodes
 * typically resolve only the assets they actually consume.
 */
class WATABOUCORE_API FWatabouSeedCache
{
public:
	/** Find any uasset whose SourceSeed matches Seed. Returns null if none. */
	static TSoftObjectPtr<UWatabouAssetBase> Find(const FWatabouSeedRef& Seed);

	/** Returns all matches (cache may legitimately hold duplicates per seed during dev). */
	static void FindAll(const FWatabouSeedRef& Seed, TArray<TSoftObjectPtr<UWatabouAssetBase>>& Out);

	/**
	 * Batch lookup: resolve many seeds with a SINGLE AssetRegistry query (the filter ORs all
	 * canonical keys) instead of one query per seed. OutByCanonicalKey maps each resolved seed's
	 * canonical key to its imported asset (first match); seeds with no match are simply absent.
	 * Prefer this over looping Find when resolving a batch (e.g. one per upstream feature).
	 */
	static void FindMany(const TArray<FWatabouSeedRef>& Seeds, TMap<FString, TSoftObjectPtr<UWatabouAssetBase>>& OutByCanonicalKey);

	/**
	 * Walk InAsset's Features tree (Details + ElementsDetails on every collection,
	 * recursively into SubCollections) and collect every FInstancedStruct entry
	 * typed as FWatabouSeedRef.
	 *
	 * If AllowedTargetGenerators is non-empty, only refs whose GeneratorId is in
	 * the set are emitted (set element is FName-of-the-GeneratorId). Pass an
	 * empty set to emit all.
	 *
	 * Output preserves discovery order: parent collections before subs,
	 * Details before ElementsDetails. Invalid refs are skipped.
	 */
	static void GatherRefs(
		const UWatabouAssetBase* InAsset,
		const TSet<FName>& AllowedTargetGenerators,
		TArray<FWatabouSeedRef>& OutRefs);

	/**
	 * Back-fill pass: walk InAsset's Features tree and set each nested FWatabouSeedRef's
	 * ResolvedAsset from the registry (Find by canonical key). Idempotent and re-runnable.
	 *
	 * Returns true and MarkPackageDirty()s InAsset if any pointer actually changed; the caller
	 * owns saving. A ref whose target isn't imported yet is left null (a later relink fills it).
	 */
	static bool RelinkAsset(UWatabouAssetBase* InAsset);
};
