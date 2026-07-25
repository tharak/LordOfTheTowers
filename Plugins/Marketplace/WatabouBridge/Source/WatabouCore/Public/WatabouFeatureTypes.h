// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "WatabouFeatureTypes.generated.h"

/**
 * GeoJSON-flavored feature kind. Every Watabou generator's JSON output decomposes
 * into a tree of these.
 */
UENUM(BlueprintType)
enum class EWatabouFeatureType : uint8
{
	Unknown     = 0,
	Point       = 1,
	MultiPoints = 2,
	LineString  = 3,
	Polygon     = 5,
	Collection  = 42,
};

/**
 * Which bucket of FWatabouFeatureDetails carries a given key.
 *
 * Drives the per-asset manifest in UWatabouAssetBase::DiscoveredDetailKeys so
 * consumers (PCG bridge, attribute pickers, runtime converters) can set up the
 * right reader for each known key without scanning the whole feature tree.
 *
 * Append-only: existing entries must keep their numeric values across versions
 * so serialized assets stay readable.
 */
UENUM(BlueprintType)
enum class EWatabouDetailKind : uint8
{
	None    = 0,
	String  = 1,
	Name    = 2,
	Numeric = 3,
	Struct  = 4,
};

/**
 * One entry in UWatabouAssetBase::DiscoveredDetailKeys.
 *
 * Carries the bucket Kind plus, when Kind=Struct, the FName of the UScriptStruct
 * holding the value. Without StructTypeName, consumers would only know "this
 * key is structured" -- not "this key holds FWatabouSeedRef" (or whatever the
 * future struct kinds turn out to be).
 *
 * NAME_None for StructTypeName means "structured but unspecified type", which
 * shouldn't normally happen if parsers always FInstancedStruct::Make<T>().
 */
USTRUCT(BlueprintType)
struct WATABOUCORE_API FWatabouDetailSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	EWatabouDetailKind Kind = EWatabouDetailKind::None;

	/** When Kind=Struct, the UScriptStruct's FName (e.g. "WatabouSeedRef"). NAME_None otherwise. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FName StructTypeName = NAME_None;

	bool operator==(const FWatabouDetailSlot& Other) const
	{
		return Kind == Other.Kind && StructTypeName == Other.StructTypeName;
	}

	/** Resolve StructTypeName to a live UScriptStruct. Returns null if Kind != Struct or type isn't loaded.
	 *  Result is cached on first call; safe to call in hot paths. */
	UScriptStruct* ResolveStructType() const;

private:
	/** Transient cache for ResolveStructType -- not serialized. Populated lazily on first call. */
	mutable TWeakObjectPtr<UScriptStruct> CachedStructType;
};

/**
 * (Type, Id) identifier used to count and group features. The Id usually comes
 * from the JSON object's "id" field (e.g. "earth", "ocean", "river", "trees").
 */
USTRUCT(BlueprintType)
struct WATABOUCORE_API FWatabouFeatureIdentifier
{
	GENERATED_BODY()

	FWatabouFeatureIdentifier() = default;
	FWatabouFeatureIdentifier(const EWatabouFeatureType InType, const FName InId)
		: Type(InType), Id(InId) {}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	EWatabouFeatureType Type = EWatabouFeatureType::Unknown;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FName Id = NAME_None;

	bool operator==(const FWatabouFeatureIdentifier& Other) const
	{
		return Other.Id == Id && Other.Type == Type;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FWatabouFeatureIdentifier& InIdentifier)
	{
		return HashCombineFast(GetTypeHash(InIdentifier.Id), GetTypeHash(InIdentifier.Type));
	}
};

/**
 * Open-vocabulary metadata bag attached to a feature OR a whole collection.
 *
 * Watabou outputs are additive: new fields appear on objects from version to
 * version, and the data model is intentionally schemaless. Anything the parser
 * doesn't lift into a well-known place lands here so consumers can read it
 * without per-version code churn.
 *
 * Four value kinds cover what we've seen:
 *  - StringValues:  free text + per-element unique strings (URLs, descriptions,
 *                   hex_key, river_key, channel-joined edge keys, ...).
 *  - NameValues:    bounded-vocabulary tags that repeat heavily (terrain,
 *                   town_type, room name, direction). FName-deduplicated at the
 *                   global table; cheap to compare and cheap to bridge into
 *                   PCG's FName attributes.
 *  - NumericValues: numbers and bool flags (encoded as 0 / 1).
 *  - StructValues:  structured cross-references whose integrity matters (today:
 *                   FWatabouSeedRef for "town" / "danger" / future link-shaped
 *                   flags). A new link kind needs zero parser changes if it
 *                   follows the same JSON shape -- just a new key name.
 *
 * Aggregation passes (e.g. UWatabouAssetBase::AggregateMetadata) walk all
 * StructValues maps in the feature tree and collect entries by UScriptStruct,
 * so domain-specific summaries (e.g. DiscoveredDetailKeys) stay generic.
 */
USTRUCT(BlueprintType)
struct WATABOUCORE_API FWatabouFeatureDetails
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TMap<FName, FString> StringValues;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TMap<FName, FName> NameValues;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TMap<FName, double> NumericValues;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TMap<FName, FInstancedStruct> StructValues;

	bool IsEmpty() const
	{
		return StringValues.IsEmpty() && NameValues.IsEmpty()
			&& NumericValues.IsEmpty() && StructValues.IsEmpty();
	}
};

/**
 * A single GeoJSON-style feature: a typed geometry with an id and a coordinate set.
 *
 * Coordinate convention: Watabou outputs (X, Y) pairs in JSON. The parser flips X
 * (multiplies by -1) so the result aligns with Unreal's left-handed coordinate
 * convention. The flip is centralized in the parser, not done here.
 */
USTRUCT(BlueprintType)
struct WATABOUCORE_API FWatabouFeature
{
	GENERATED_BODY()

	FWatabouFeature() = default;
	FWatabouFeature(const EWatabouFeatureType InType, const FName InId)
		: Type(InType), Id(InId) {}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	EWatabouFeatureType Type = EWatabouFeatureType::Unknown;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	FName Id = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Watabou")
	TArray<FVector2D> Coordinates;

	FWatabouFeatureIdentifier GetIdentifier() const
	{
		return FWatabouFeatureIdentifier(Type, Id);
	}
};
