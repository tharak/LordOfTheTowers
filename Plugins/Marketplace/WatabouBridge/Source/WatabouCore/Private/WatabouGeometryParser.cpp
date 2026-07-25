// Copyright 2026 Timothé Lapetite

#include "WatabouGeometryParser.h"
#include "WatabouAssetBase.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouFeatureTypes.h"
#include "WatabouBridge.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace WatabouGeometryParser_Internal
{
	static const TSet<FName> IgnoredDetails = {
		FName("id"), FName("type"), FName("coordinates"),
		FName("features"), FName("geometries"),
	};

	// GeoJSON-ish type strings used by Watabou outputs.
	static const TCHAR* TypeFeature             = TEXT("Feature");
	static const TCHAR* TypeGeometryCollection  = TEXT("GeometryCollection");
	static const TCHAR* TypePoint               = TEXT("Point");
	static const TCHAR* TypeMultiPoint          = TEXT("MultiPoint");
	static const TCHAR* TypeLineString          = TEXT("LineString");
	static const TCHAR* TypeMultiLineString     = TEXT("MultiLineString");
	static const TCHAR* TypePolygon             = TEXT("Polygon");
	static const TCHAR* TypeMultiPolygon        = TEXT("MultiPolygon");
}

bool FWatabouGeometryParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWatabouGeometryParser::Parse);

	if (!InJson.IsValid() || !OutAsset)
	{
		OutError = TEXT("Parse: null JSON or asset");
		return false;
	}

	// Note: we don't call OutAsset->Reset() here -- the caller decides whether the asset
	// should be cleared first. NewObject already gives us a clean asset; Reset would wipe
	// SourceSeed/BundleVersion that the caller set on us.

	if (!PreBuild(InJson, OutAsset, OutError)) { return false; }

	if (OutAsset->Features)
	{
		Build(InJson, OutAsset->Features, OutAsset);
	}

	return PostBuild(OutAsset, OutError);
}

bool FWatabouGeometryParser::PreBuild(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* InAsset, FString& OutError)
{
	// Default: nothing to validate.
	return true;
}

bool FWatabouGeometryParser::PostBuild(UWatabouAssetBase* InAsset, FString& OutError)
{
	// Not finding any features is plausible for non-GeoJSON-shaped generators
	// (e.g. Perilous Shores uses a custom mesh format) and means the parser
	// needs a specialized override.
	if (!InAsset->Features || !InAsset->Features->IsValidCollection())
	{
		UE_LOG(LogWatabou, Verbose,
			TEXT("FWatabouGeometryParser: no GeoJSON-style features found -- this generator likely needs a specialized parser"));
	}
	return true;
}

bool FWatabouGeometryParser::GetVector2D(const TSharedPtr<FJsonValue>& InValue, FVector2D& OutValue) const
{
	if (!InValue.IsValid()) { return false; }
	const TArray<TSharedPtr<FJsonValue>>& Pair = InValue->AsArray();
	if (Pair.Num() != 2 || !Pair[0].IsValid() || !Pair[1].IsValid()) { return false; }
	// X is flipped to align with Unreal's left-handed convention.
	OutValue = FVector2D(Pair[0]->AsNumber() * -1.0, Pair[1]->AsNumber());
	return true;
}

int32 FWatabouGeometryParser::BuildCoordinates(FWatabouFeature& InElement, const TArray<TSharedPtr<FJsonValue>>& InCoordinates) const
{
	int32 Count = 0;
	FVector2D Coord;
	for (const TSharedPtr<FJsonValue>& Value : InCoordinates)
	{
		if (GetVector2D(Value, Coord))
		{
			InElement.Coordinates.Add(Coord);
			++Count;
		}
	}
	return Count;
}

void FWatabouGeometryParser::BuildDetails(const TSharedPtr<FJsonObject>& InObj, UWatabouFeaturesCollection* InCollection, FWatabouFeatureDetails* InDetails)
{
	using namespace WatabouGeometryParser_Internal;

	const int32 ElementIdx = InCollection->Elements.Num() - 1;

	FWatabouFeatureDetails LocalDetails;
	FWatabouFeatureDetails* Target = InDetails ? InDetails : &LocalDetails;

	for (const auto& Pair : InObj->Values)
	{
		const FName DetailId(*Pair.Key);
		if (IgnoredDetails.Contains(DetailId)) { continue; }

		const TSharedPtr<FJsonValue>& Value = Pair.Value;
		if (!Value.IsValid()) { continue; }

		switch (Value->Type)
		{
		case EJson::Number:
			Target->NumericValues.Add(DetailId, Value->AsNumber());
			break;
		case EJson::String:
			Target->StringValues.Add(DetailId, Value->AsString());
			break;
		case EJson::Boolean:
			Target->NumericValues.Add(DetailId, Value->AsBool() ? 1.0 : 0.0);
			break;
		default:
			// Objects / arrays / null are skipped; specialized parsers can override.
			break;
		}
	}

	if (!InDetails && !LocalDetails.IsEmpty() && ElementIdx >= 0)
	{
		InCollection->ElementsDetails.Add(ElementIdx, MoveTemp(LocalDetails));
	}
}

void FWatabouGeometryParser::Build(const TSharedPtr<FJsonObject>& InJson, UWatabouFeaturesCollection* InCollection, UWatabouAssetBase* InAsset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWatabouGeometryParser::Build);

	using namespace WatabouGeometryParser_Internal;

	if (!InJson.IsValid() || !InCollection) { return; }

	// Top-level details on the collection itself.
	BuildDetails(InJson, InCollection, &InCollection->Details);

	const TArray<TSharedPtr<FJsonValue>>* FeaturesArray = nullptr;
	if (!InJson->TryGetArrayField(TEXT("features"), FeaturesArray) &&
		!InJson->TryGetArrayField(TEXT("geometries"), FeaturesArray))
	{
		return;
	}

	auto RemoveLastFeature = [InCollection]()
	{
		if (InCollection->Elements.Num() > 0)
		{
			InCollection->Elements.RemoveAt(InCollection->Elements.Num() - 1);
		}
	};

	for (const TSharedPtr<FJsonValue>& FeatureValue : *FeaturesArray)
	{
		const TSharedPtr<FJsonObject> FeatureObj = FeatureValue.IsValid() ? FeatureValue->AsObject() : nullptr;
		if (!FeatureObj.IsValid()) { continue; }

		FString FeatureType;
		FString FeatureIdStr;
		FName   FeatureId = NAME_None;

		if (FeatureObj->TryGetStringField(TEXT("id"), FeatureIdStr)) { FeatureId = FName(*FeatureIdStr); }
		else { FeatureId = InCollection->Id; }

		if (!FeatureObj->TryGetStringField(TEXT("type"), FeatureType)) { continue; }

		// Bare "Feature" objects carry only metadata about the parent collection.
		if (FeatureType == TypeFeature)
		{
			BuildDetails(FeatureObj, InCollection, &InCollection->Details);
			continue;
		}

		EWatabouFeatureType Type = EWatabouFeatureType::Unknown;
		if      (FeatureType == TypeGeometryCollection) { Type = EWatabouFeatureType::Collection; }
		else if (FeatureType == TypePoint)              { Type = EWatabouFeatureType::Point; }
		else if (FeatureType == TypeMultiPoint)         { Type = EWatabouFeatureType::MultiPoints; }
		else if (FeatureType == TypeLineString)         { Type = EWatabouFeatureType::LineString; }
		else if (FeatureType == TypeMultiLineString)    { Type = EWatabouFeatureType::LineString; }
		else if (FeatureType == TypePolygon)            { Type = EWatabouFeatureType::Polygon; }
		else if (FeatureType == TypeMultiPolygon)       { Type = EWatabouFeatureType::Polygon; }
		else { continue; }

		// Recursive: a GeometryCollection becomes a SubCollection of this collection.
		// Do NOT bind a reference into InAsset->Identifiers across the recursive Build()
		// below: the recursion inserts new identifier keys, which can rehash the map and
		// invalidate any prior FindOrAdd reference. Increment via a fresh lookup afterwards.
		if (FeatureType == TypeGeometryCollection)
		{
			UWatabouFeaturesCollection* NewSub = NewObject<UWatabouFeaturesCollection>(InCollection);
			NewSub->Id = FeatureId;
			Build(FeatureObj, NewSub, InAsset);
			if (NewSub->IsValidCollection())
			{
				NewSub->SetFlags(RF_Transactional);
				InCollection->SubCollections.Add(NewSub);
				++InAsset->Identifiers.FindOrAdd(FWatabouFeatureIdentifier(Type, FeatureId), 0);
			}
			continue;
		}

		// Non-recursive feature kinds below never insert into InAsset->Identifiers between
		// here and the matching ++Count, so holding this reference is safe.
		int32& Count = InAsset->Identifiers.FindOrAdd(FWatabouFeatureIdentifier(Type, FeatureId), 0);

		const TArray<TSharedPtr<FJsonValue>>* Coordinates = nullptr;
		if (!FeatureObj->TryGetArrayField(TEXT("coordinates"), Coordinates)) { continue; }
		const int32 NumCoordinates = Coordinates ? Coordinates->Num() : 0;
		if (NumCoordinates <= 0) { continue; }

		if (FeatureType == TypeMultiPoint)
		{
			if (NumCoordinates == 1)
			{
				FVector2D P;
				if (GetVector2D((*Coordinates)[0], P))
				{
					FWatabouFeature& Pt = InCollection->Elements.Emplace_GetRef(EWatabouFeatureType::Point, FeatureId);
					Pt.Coordinates.Add(P);
					++Count;
				}
			}
			else
			{
				FWatabouFeature& Multi = InCollection->Elements.Emplace_GetRef(EWatabouFeatureType::MultiPoints, FeatureId);
				Multi.Coordinates.Reserve(NumCoordinates);
				if (BuildCoordinates(Multi, *Coordinates) == 0) { RemoveLastFeature(); continue; }
				BuildDetails(FeatureObj, InCollection);
				++Count;
			}
		}
		else if (FeatureType == TypeLineString)
		{
			FWatabouFeature& Line = InCollection->Elements.Emplace_GetRef(EWatabouFeatureType::LineString, FeatureId);
			Line.Coordinates.Reserve(NumCoordinates);
			if (BuildCoordinates(Line, *Coordinates) == 0) { RemoveLastFeature(); continue; }
			BuildDetails(FeatureObj, InCollection);
			++Count;
		}
		else if (FeatureType == TypeMultiLineString)
		{
			for (const TSharedPtr<FJsonValue>& PathValue : *Coordinates)
			{
				if (!PathValue.IsValid()) { continue; }
				const TArray<TSharedPtr<FJsonValue>>& PathPoints = PathValue->AsArray();
				if (PathPoints.Num() <= 0) { continue; }
				FWatabouFeature& Line = InCollection->Elements.Emplace_GetRef(EWatabouFeatureType::LineString, FeatureId);
				Line.Coordinates.Reserve(PathPoints.Num());
				if (BuildCoordinates(Line, PathPoints) == 0) { RemoveLastFeature(); continue; }
				BuildDetails(FeatureObj, InCollection);
				++Count;
			}
		}
		else if (FeatureType == TypePolygon)
		{
			for (const TSharedPtr<FJsonValue>& VertsValue : *Coordinates)
			{
				if (!VertsValue.IsValid()) { continue; }
				const TArray<TSharedPtr<FJsonValue>>& Verts = VertsValue->AsArray();
				if (Verts.Num() <= 0) { continue; }
				FWatabouFeature& Poly = InCollection->Elements.Emplace_GetRef(EWatabouFeatureType::Polygon, FeatureId);
				Poly.Coordinates.Reserve(Verts.Num());
				if (BuildCoordinates(Poly, Verts) == 0) { RemoveLastFeature(); continue; }
				BuildDetails(FeatureObj, InCollection);
				++Count;

				// Special: "earth" polygon represents world bounds.
				if (FeatureIdStr == TEXT("earth") && InAsset)
				{
					FBox Bounds(ForceInit);
					for (const FVector2D& P : Poly.Coordinates) { Bounds += FVector(P, 0); }
					Bounds += FVector(0, 0, -1);
					Bounds += FVector(0, 0, 1);
					InAsset->Bounds = Bounds;
				}
			}
		}
		else if (FeatureType == TypeMultiPolygon)
		{
			for (const TSharedPtr<FJsonValue>& PolysValue : *Coordinates)
			{
				if (!PolysValue.IsValid()) { continue; }
				const TArray<TSharedPtr<FJsonValue>>& Polys = PolysValue->AsArray();
				for (const TSharedPtr<FJsonValue>& VertsValue : Polys)
				{
					if (!VertsValue.IsValid()) { continue; }
					const TArray<TSharedPtr<FJsonValue>>& Verts = VertsValue->AsArray();
					if (Verts.Num() <= 0) { continue; }
					FWatabouFeature& Poly = InCollection->Elements.Emplace_GetRef(EWatabouFeatureType::Polygon, FeatureId);
					Poly.Coordinates.Reserve(Verts.Num());
					if (BuildCoordinates(Poly, Verts) == 0) { RemoveLastFeature(); continue; }
					BuildDetails(FeatureObj, InCollection);
					++Count;
				}
			}
		}
	}
}
