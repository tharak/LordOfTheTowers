// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Templates/FunctionFwd.h"

class UWatabouAssetBase;
class UWatabouFeaturesCollection;
class UPCGParamData;
class UPCGBasePointData;
struct FPCGContext;

/**
 * The Feature Map: the back-reference that turns lean geometry into rich data on demand.
 *
 * The Load node emits geometry carrying only an int64 feature key; this map (a UPCGParamData
 * on a dedicated output pin) lets generator-agnostic downstream nodes -- a property loader,
 * or a chained Load node following a seed-ref -- resolve a key back to the exact source
 * feature without the geometry having to carry any of that feature's data.
 *
 * Key layout (mirrors PCGEx staging's bit-packed hash):
 *   int64 = (AssetId32 << 32) | FlatFeatureIndex32
 *     AssetId32         : GetTypeHash of the asset's FSoftObjectPath. Stable across nodes,
 *                         so merging two Load outputs that reference the same asset is safe.
 *     FlatFeatureIndex32: position in the canonical feature walk (see EnumerateFeatures).
 *
 * Because the flat index is canonical and reproducible, the map only needs to resolve
 * AssetId32 -> asset path: one entry per asset, not per feature. The consumer loads the
 * asset, re-walks it, and indexes by FlatFeatureIndex to recover the feature + its details.
 */
namespace WatabouPCG
{
	namespace FeatureMapAttributes
	{
		/** int64 feature key carried by emitted geometry (@Data for single-feature data, per-point for merged clouds). */
		const FName Key = FName(TEXT("WatabouKey"));

		/** int32 asset id (map param data): GetTypeHash of the asset path. */
		const FName AssetId = FName(TEXT("WatabouAssetId"));

		/** FSoftObjectPath asset reference (map param data). */
		const FName Asset = FName(TEXT("WatabouAsset"));
	}

	/**
	 * Canonical depth-first feature walk: every Element of a collection (in array order),
	 * then each SubCollection (in array order), recursively, starting at the asset root.
	 * Visit receives the owning collection, the element index within it, and the running
	 * flat index. Emitter and consumer MUST share this walk so flat indices agree.
	 */
	WATABOUPCG_API void EnumerateFeatures(
		const UWatabouAssetBase* Asset,
		TFunctionRef<void(const UWatabouFeaturesCollection* Collection, int32 ElementIndex, int32 FlatIndex)> Visit);

	/** Compose the int64 feature key from an asset id and a flat feature index. */
	WATABOUPCG_API int64 MakeFeatureKey(uint32 AssetId, int32 FlatIndex);

	/** Decompose an int64 feature key into its asset id and flat feature index. */
	WATABOUPCG_API void BreakFeatureKey(int64 Key, uint32& OutAssetId, int32& OutFlatIndex);

	/**
	 * Which WatabouKey domain a point data carries, mirroring the two ways the emit sink stamps it:
	 *   PerPoint : one key per point (Elements domain) -- a merged single-point cloud, each point its
	 *              own feature.
	 *   Data     : one key for the whole data set (@Data domain) -- a single feature (MultiPoints /
	 *              LineString / Polygon / standalone Point).
	 *   None     : not Watabou-keyed geometry.
	 */
	enum class EWatabouKeyDomain : uint8
	{
		None,
		PerPoint,
		Data,
	};

	/** Detect which WatabouKey domain Points carries (PerPoint preferred when both somehow exist). */
	WATABOUPCG_API EWatabouKeyDomain GetFeatureKeyDomain(const UPCGBasePointData* Points);

	/**
	 * Read the single @Data WatabouKey. Uses the parent-chain walker (a duplicated @Data attribute
	 * has no local entries), so it is safe on passthrough copies. Returns false when absent.
	 */
	WATABOUPCG_API bool TryReadDataFeatureKey(const UPCGBasePointData* Points, int64& OutKey);

	/**
	 * Visit each per-point WatabouKey (Elements domain) as Visit(PointIndex, Key). Points whose key
	 * cannot be read are skipped. Returns the number of points visited (0 if the per-point key
	 * attribute is absent / unreadable).
	 */
	WATABOUPCG_API int32 ForEachPerPointFeatureKey(const UPCGBasePointData* Points, TFunctionRef<void(int32 PointIndex, int64 Key)> Visit);

	/**
	 * Accumulates the asset->path map during emission and computes feature keys.
	 *
	 * One packer is shared across a whole node execution (all targets). Register each
	 * resolved asset once (single-threaded), then ask for feature keys while emitting.
	 * BuildMap serializes the accumulated assets to a param data for the Feature Map pin.
	 */
	class WATABOUPCG_API FWatabouFeatureMapPacker
	{
	public:
		/** Register an asset (idempotent). Computes its id and primes its canonical flat enumeration. */
		uint32 RegisterAsset(const UWatabouAssetBase* Asset);

		/**
		 * Feature key for (asset, collection, element). The asset must have been registered.
		 * Returns 0 if the asset is unregistered or the (collection, element) pair is unknown.
		 */
		int64 KeyFor(const UWatabouAssetBase* Asset, const UWatabouFeaturesCollection* Collection, int32 ElementIndex) const;

		/** True while no asset has been registered (no map to emit). */
		bool IsEmpty() const { return AssetPaths.IsEmpty(); }

		/** Serialize the accumulated asset map to a fresh param data (one entry per asset). */
		UPCGParamData* BuildMap(FPCGContext* Context) const;

	private:
		/** AssetId32 -> asset path. */
		TMap<uint32, FSoftObjectPath> AssetPaths;

		/** AssetId32 -> ((collection, element index) -> flat feature index). */
		TMap<uint32, TMap<TPair<const UWatabouFeaturesCollection*, int32>, int32>> FlatIndices;
	};

	/**
	 * Reads a Feature Map param data back into an asset map and resolves feature keys to their
	 * source features. Symmetric to FWatabouFeatureMapPacker; the consumer side of the map.
	 *
	 * Flow: Unpack(map) [repeatable, accumulates] -> GetAssetPaths() (async-load them) ->
	 * ResolveAssets() -> ResolveFeature(key) per point.
	 */
	class WATABOUPCG_API FWatabouFeatureMapUnpacker
	{
	public:
		/** Read AssetId -> path entries from a map param data (accumulates across calls). Returns false if it added nothing. */
		bool Unpack(const UPCGParamData* MapData);

		bool IsEmpty() const { return AssetPaths.IsEmpty(); }

		/** Append every asset path in the map (for the consumer's async load). */
		void GetAssetPaths(TArray<FSoftObjectPath>& OutPaths) const;

		/** Resolve loaded asset paths into live asset pointers. Call after the consumer has loaded them. */
		void ResolveAssets();

		/**
		 * Resolve a feature key to its source feature. Returns false when the key's asset is not
		 * resolved or the flat index is out of range. Lazily builds a per-asset flat-index cache.
		 */
		bool ResolveFeature(int64 Key, const UWatabouAssetBase*& OutAsset, const UWatabouFeaturesCollection*& OutCollection, int32& OutElementIndex) const;

	private:
		TMap<uint32, FSoftObjectPath> AssetPaths;
		TMap<uint32, const UWatabouAssetBase*> ResolvedAssets;

		/** Lazy per-asset flat index -> (collection, element index), built on first ResolveFeature for an asset. */
		mutable TMap<uint32, TMap<int32, TPair<const UWatabouFeaturesCollection*, int32>>> FlatLookup;
	};

	/**
	 * Unpack every Feature Map param data on the given input pin into Unpacker (accumulates across
	 * inputs). Shared by the nodes that consume a Feature Map so the read stays in one place.
	 */
	WATABOUPCG_API void UnpackFeatureMaps(FPCGContext* Context, FWatabouFeatureMapUnpacker& Unpacker, FName PinLabel);
}
