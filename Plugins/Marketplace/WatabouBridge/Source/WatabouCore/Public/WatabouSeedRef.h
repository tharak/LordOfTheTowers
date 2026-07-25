// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/Class.h"   // TStructOpsTypeTraitsBase2 (the WithIdenticalViaEquality specialization below)
#include "WatabouSeedRef.generated.h"

class UWatabouAssetBase;

/**
 * A reference to a Watabou-generated piece of content by (generator id, seed, tags, params).
 *
 * Generic across all generators: every Watabou generator's content can be addressed by this tuple.
 * Used to mark nested seeds inside a parent generator's data (e.g. a Perilous Shores map has
 * city landmarks that hold FWatabouSeedRef pointing at the City Generator), and to look up
 * already-imported uassets in FWatabouSeedCache.
 *
 * The canonical key is derived deterministically from the (generator, seed, tags, params) tuple and
 * is stable across sessions, so it can be used as a map key or AssetRegistry tag. GetCanonicalHash()
 * is a compact, filename-safe hex digest of that key -- the default asset-name stem.
 *
 * Identity is the canonical key ALONE. ResolvedAsset is a denormalized locator (filled by the
 * relink pass once the referenced content is imported) and is deliberately EXCLUDED from
 * GetCanonicalKey / operator== / GetTypeHash -- two refs to the same content stay equal whether or
 * not their pointer is resolved.
 */
USTRUCT(BlueprintType)
struct WATABOUCORE_API FWatabouSeedRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Watabou")
	FString GeneratorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Watabou")
	FString Seed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Watabou")
	TArray<FString> Tags;

	/** Extra URL params (size, hexes, w, h, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Watabou")
	TMap<FString, FString> Params;

	/**
	 * Resolved import of this ref, when known. A denormalized cache of FWatabouSeedCache::Find()
	 * filled by the relink pass after the referenced content is imported. NOT part of identity:
	 * GetCanonicalKey / operator== / GetTypeHash all ignore it. Survives the target being renamed
	 * or moved (soft path follows redirectors), so it's the drift-immune fast path for resolution.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Watabou")
	TSoftObjectPtr<UWatabouAssetBase> ResolvedAsset;

	/** Stable, order-independent key over (generator, seed, tags, params). The content's identity. */
	FString GetCanonicalKey() const;

	/** Compact hex digest (16 chars) of GetCanonicalKey(), stable across sessions and platforms.
	 *  Filename-safe; used as the default asset-name stem and the unique disambiguator. */
	FString GetCanonicalHash() const;

	/** Build the URL query string portion ("?seed=...&tags=...&w=...") for the bridge. */
	FString BuildQueryString() const;

	bool IsValid() const { return !GeneratorId.IsEmpty() && !Seed.IsEmpty(); }

	bool operator==(const FWatabouSeedRef& Other) const;
	bool operator!=(const FWatabouSeedRef& Other) const { return !(*this == Other); }
};

FORCEINLINE uint32 GetTypeHash(const FWatabouSeedRef& Ref)
{
	return GetTypeHash(Ref.GetCanonicalKey());
}

/** Make UE's reflection-level struct comparison (FInstancedStruct::Identical, delta/transaction diffing)
 *  route through operator== -- which keys on the canonical identity and ignores ResolvedAsset -- so two
 *  refs to the same content stay equal whether or not their pointer is resolved. Without this, the
 *  generated property-wise compare would fold the (non-identity) ResolvedAsset back into equality. */
template<>
struct TStructOpsTypeTraits<FWatabouSeedRef> : public TStructOpsTypeTraitsBase2<FWatabouSeedRef>
{
	enum { WithIdenticalViaEquality = true };
};
