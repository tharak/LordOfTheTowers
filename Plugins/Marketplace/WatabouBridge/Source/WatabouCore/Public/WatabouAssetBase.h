// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WatabouSeedRef.h"
#include "WatabouFeatureTypes.h"
#include "WatabouAssetBase.generated.h"

class UWatabouFeaturesCollection;

/**
 * Single concrete asset class for every Watabou generator output.
 *
 * The whole data model lives in one place: the Features tree (open-vocabulary
 * geometry + metadata) plus a small set of well-known top-level fields that
 * consumers reach for often enough that walking the tree would be annoying.
 *
 * There are NO per-generator subclasses. Generator-specific JSON shapes become
 * generator-specific Feature tree shapes -- the typed information lives in
 * FWatabouFeatureDetails (and FInstancedStruct for cross-reference integrity),
 * not in UPROPERTY members. New fields in upstream JSON appear automatically
 * via the details bag with no plugin code change.
 *
 * Lookup key is FWatabouSeedRef.GetCanonicalKey() (queryable via
 * FWatabouSeedCache using the AssetRegistrySearchable CanonicalKey tag).
 */
UCLASS(BlueprintType)
class WATABOUCORE_API UWatabouAssetBase : public UDataAsset
{
	GENERATED_BODY()

public:
	UWatabouAssetBase();

	/** Source seed that produced this asset. AssetRegistry-searchable for cache lookups. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou", AssetRegistrySearchable)
	FWatabouSeedRef SourceSeed;

	/** Bundle version of the generator that produced this asset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FString BundleVersion;

	/** Human-readable title from the JSON (e.g. OPD's "title", PerilousShores' "name"). May be empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FString Title;

	/** Free-form generation tags from the JSON ("peninsula", "civilized", ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TArray<FString> GenerationTags;

	/** World bounds derived from the parsed geometry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FBox Bounds = FBox(ForceInit);

	/** Parsed feature tree. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Watabou")
	TObjectPtr<UWatabouFeaturesCollection> Features;

	/** Count of each (Type, Id) identifier seen. Useful for triage / summary. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou", meta = (TitleProperty = "{Id} | {Type}"))
	TMap<FWatabouFeatureIdentifier, int32> Identifiers;

	/**
	 * Typed manifest of every detail key seen in the feature tree.
	 *
	 * Key  : the detail key as it appears in FWatabouFeatureDetails maps.
	 * Slot : which bucket the value lives in (String / Name / Numeric / Struct)
	 *        and, when Struct, which UScriptStruct it holds. Drives converter
	 *        / attribute setup at consumption time.
	 *
	 * Populated by AggregateMetadata. On collision (the same key seen with a
	 * different Kind, or with a different struct type when Kind=Struct), the
	 * first slot is kept and a warning is logged -- a sign of parser hygiene
	 * drift rather than a runtime error.
	 *
	 * PCG nodes consume this to populate attribute pickers without scanning the
	 * entire tree at edit time, and to pre-build the right reader per key.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TMap<FName, FWatabouDetailSlot> DiscoveredDetailKeys;

	/** Reset parsed data (keeps SourceSeed / BundleVersion). */
	void Reset();

	/** Increment the (Type, Id) entry in Identifiers by one. */
	void BumpIdentifier(EWatabouFeatureType InType, FName InId);

	/**
	 * Post-parse pass: walks the feature tree and populates DiscoveredDetailKeys
	 * with every detail key encountered. Idempotent.
	 */
	static void AggregateMetadata(UWatabouAssetBase* InAsset);

#if WITH_EDITOR
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	/**
	 * Open the Watabou Import window, switch to this asset's generator, and restore the settings
	 * that produced it (seed, tags, and the params that generator's settings model) from SourceSeed.
	 * The window regenerates the seed in its embedded preview; the restored settings are loaded into
	 * the tab for the session only and do NOT overwrite that generator's saved last-used settings.
	 * No-op if the editor bridge isn't bound (e.g. the editor module failed to load).
	 */
	UFUNCTION(CallInEditor, Category = "Watabou", meta = (DisplayName = "Open in Import Window"))
	void OpenInImportWindow();
#endif
};
