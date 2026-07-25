// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "IWatabouParser.h"

class FJsonValue;
class UWatabouFeaturesCollection;
class UWatabouAssetBase;
struct FWatabouFeature;
struct FWatabouFeatureDetails;

/**
 * Base parser implementing the GeoJSON-shaped output common to all current
 * Watabou generators (Perilous Shores, Village, City, Dwellings, One Page Dungeon).
 *
 * The JSON document root either has a "features" or "geometries" array of
 * objects, each with a "type" string (Feature / GeometryCollection / Point /
 * MultiPoint / LineString / MultiLineString / Polygon / MultiPolygon), an
 * optional "id", and a "coordinates" array of [x, y] pairs.
 *
 * Special case: a Polygon with id "earth" represents the world bounds and is
 * extracted into UWatabouAssetBase::Bounds in addition to becoming a regular
 * feature.
 *
 * X coordinate convention: Watabou outputs (X, Y) in JSON; this parser flips
 * X (multiplies by -1) to align with Unreal's left-handed orientation. The
 * flip is centralized in GetVector2D.
 *
 * Per-generator parsers can subclass this and override Build (or the smaller
 * BuildDetails / GetVector2D hooks) to handle generator-specific fields.
 */
class WATABOUCORE_API FWatabouGeometryParser : public IWatabouParser
{
public:
	FWatabouGeometryParser() = default;

	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) override;

protected:
	/** Called before Build. Override to validate top-level fields or reject early. */
	virtual bool PreBuild(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* InAsset, FString& OutError);

	/** Recursive: walks features/geometries arrays, emits FWatabouFeatures into InCollection. */
	virtual void Build(const TSharedPtr<FJsonObject>& InJson, UWatabouFeaturesCollection* InCollection, UWatabouAssetBase* InAsset);

	/** Pulls non-essential fields off a JSON object into FWatabouFeatureDetails. */
	virtual void BuildDetails(const TSharedPtr<FJsonObject>& InObj, UWatabouFeaturesCollection* InCollection, FWatabouFeatureDetails* InDetails = nullptr);

	/** Called after Build. Override to compute derived state or validate the result. */
	virtual bool PostBuild(UWatabouAssetBase* InAsset, FString& OutError);

	/** Decode a [x, y] JSON array into FVector2D with x flipped. Override to change orientation. */
	virtual bool GetVector2D(const TSharedPtr<FJsonValue>& InValue, FVector2D& OutValue) const;

	/** Decode a coordinate list into existing feature.Coordinates. Returns number of coords pushed. */
	virtual int32 BuildCoordinates(FWatabouFeature& InElement, const TArray<TSharedPtr<FJsonValue>>& InCoordinates) const;
};
