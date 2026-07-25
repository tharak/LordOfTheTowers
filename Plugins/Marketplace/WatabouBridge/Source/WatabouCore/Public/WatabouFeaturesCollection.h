// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WatabouFeatureTypes.h"
#include "WatabouFeaturesCollection.generated.h"

/**
 * Tree node holding a Watabou generator's parsed features.
 *
 * Top-level collection corresponds to the JSON document root. Each
 * GeometryCollection in the JSON becomes a SubCollection. Leaf features
 * (Points / MultiPoints / LineStrings / Polygons) live in Elements.
 *
 * Per-element sparse metadata (rare fields) lives in ElementsDetails keyed
 * by element index. The collection's own metadata is in Details.
 */
UCLASS(BlueprintType, EditInlineNew)
class WATABOUCORE_API UWatabouFeaturesCollection : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FWatabouFeatureDetails Details;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FName Id = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou", meta = (TitleProperty = "{Id}"))
	TArray<FWatabouFeature> Elements;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Watabou", meta = (TitleProperty = "{Id}"))
	TArray<TObjectPtr<UWatabouFeaturesCollection>> SubCollections;

	/** Sparse per-element details keyed by index in Elements. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TMap<int32, FWatabouFeatureDetails> ElementsDetails;

	void Reset();
	bool IsValidCollection() const;

	/**
	 * Get-or-add a sub-collection by Id. Returns the existing entry if one with
	 * matching Id is already present (re-import safe); otherwise creates a new
	 * sub-collection outered to InOuter (typically the asset being filled).
	 */
	UWatabouFeaturesCollection* FindOrAddSubCollection(FName InId, UObject* InOuter);
};
