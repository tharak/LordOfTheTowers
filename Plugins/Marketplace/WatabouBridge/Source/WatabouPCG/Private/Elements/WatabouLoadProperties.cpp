// Copyright 2026 Timothé Lapetite

#include "Elements/WatabouLoadProperties.h"

#include "WatabouAssetBase.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouFeatureTypes.h"
#include "WatabouFeatureMap.h"
#include "WatabouPCGData.h"
#include "WatabouPCGMetaHelpers.h"

#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#define LOCTEXT_NAMESPACE "WatabouPCG"

namespace WatabouLoadProperties
{
	const FName PointsPinLabel = TEXT("Points");
	const FName FeatureMapPinLabel = TEXT("Feature Map");
	const FName OutPinLabel = TEXT("Out");

	// Attribute name for a feature's kind (its FWatabouFeature::Id). Fixed: it is the back-reference the
	// consolidating-generator path relies on to recover the kind off a merged cloud, so downstream graphs
	// can count on it. Deliberately not "type" -- that name is already taken by a door's subtype detail.
	const FName FeatureIdAttributeName = TEXT("FeatureId");

	// Per-point (Elements domain) detail write, at one metadata entry. Used for merged clouds, where
	// each point is its own feature.
	void WriteDetailsPerPoint(UPCGMetadata* Metadata, const PCGMetadataEntryKey Entry, const FWatabouFeatureDetails& Details, const TSet<FName>* Filter)
	{
		auto Allowed = [Filter](const FName Key) { return !Filter || Filter->Contains(Key); };

		for (const TPair<FName, FString>& Pair : Details.StringValues)
		{
			if (!Allowed(Pair.Key)) { continue; }
			const FName SafeName = WatabouPCGMeta::SanitizeAttributeName(Pair.Key);
			if (SafeName.IsNone()) { continue; }
			if (FPCGMetadataAttribute<FString>* Attr = Metadata->FindOrCreateAttribute<FString>(WatabouPCGMeta::MakeElementIdentifier(SafeName), FString(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true))
			{
				Attr->SetValue(Entry, Pair.Value);
			}
		}
		for (const TPair<FName, FName>& Pair : Details.NameValues)
		{
			if (!Allowed(Pair.Key)) { continue; }
			const FName SafeName = WatabouPCGMeta::SanitizeAttributeName(Pair.Key);
			if (SafeName.IsNone()) { continue; }
			if (FPCGMetadataAttribute<FName>* Attr = Metadata->FindOrCreateAttribute<FName>(WatabouPCGMeta::MakeElementIdentifier(SafeName), NAME_None, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true))
			{
				Attr->SetValue(Entry, Pair.Value);
			}
		}
		for (const TPair<FName, double>& Pair : Details.NumericValues)
		{
			if (!Allowed(Pair.Key)) { continue; }
			const FName SafeName = WatabouPCGMeta::SanitizeAttributeName(Pair.Key);
			if (SafeName.IsNone()) { continue; }
			if (FPCGMetadataAttribute<double>* Attr = Metadata->FindOrCreateAttribute<double>(WatabouPCGMeta::MakeElementIdentifier(SafeName), 0.0, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true))
			{
				Attr->SetValue(Entry, Pair.Value);
			}
		}
	}

	// Per-point (Elements domain) feature-kind write. The kind is the feature's Id, not a detail, so it
	// flows outside the detail-key filter -- it's the one attribute that survives consolidation by design.
	void WriteFeatureIdPerPoint(UPCGMetadata* Metadata, const PCGMetadataEntryKey Entry, const FName FeatureId)
	{
		if (FeatureId.IsNone()) { return; }
		if (FPCGMetadataAttribute<FName>* Attr = Metadata->FindOrCreateAttribute<FName>(WatabouPCGMeta::MakeElementIdentifier(FeatureIdAttributeName), NAME_None, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true))
		{
			Attr->SetValue(Entry, FeatureId);
		}
	}

	// @Data-domain feature-kind write (single-feature data: one kind for the whole data set).
	void WriteFeatureIdData(UPCGData* Data, const FName FeatureId)
	{
		if (FeatureId.IsNone()) { return; }
		WatabouPCG::SetDataValue(Data, FeatureIdAttributeName, FeatureId);
	}

	// @Data-domain detail write (one value for the whole data set). Used for single-feature data
	// (a line / polygon / multipoint / standalone point), so a point never carries an attribute its
	// feature does not own.
	void WriteDetailsData(UPCGData* Data, const FWatabouFeatureDetails& Details, const TSet<FName>* Filter)
	{
		auto Allowed = [Filter](const FName Key) { return !Filter || Filter->Contains(Key); };

		for (const TPair<FName, FString>& Pair : Details.StringValues)
		{
			if (!Allowed(Pair.Key)) { continue; }
			const FName SafeName = WatabouPCGMeta::SanitizeAttributeName(Pair.Key);
			if (!SafeName.IsNone()) { WatabouPCG::SetDataValue(Data, SafeName, Pair.Value); }
		}
		for (const TPair<FName, FName>& Pair : Details.NameValues)
		{
			if (!Allowed(Pair.Key)) { continue; }
			const FName SafeName = WatabouPCGMeta::SanitizeAttributeName(Pair.Key);
			if (!SafeName.IsNone()) { WatabouPCG::SetDataValue(Data, SafeName, Pair.Value); }
		}
		for (const TPair<FName, double>& Pair : Details.NumericValues)
		{
			if (!Allowed(Pair.Key)) { continue; }
			const FName SafeName = WatabouPCGMeta::SanitizeAttributeName(Pair.Key);
			if (!SafeName.IsNone()) { WatabouPCG::SetDataValue(Data, SafeName, Pair.Value); }
		}
	}
}

#pragma region UWatabouLoadPropertiesSettings

#if WITH_EDITOR
FText UWatabouLoadPropertiesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("WatabouLoadPropertiesTitle", "Watabou | Load Properties");
}

FText UWatabouLoadPropertiesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("WatabouLoadPropertiesTooltip", "Resolve each keyed point back to its Watabou source feature (via the Feature Map) and write that feature's details as point attributes.");
}
#endif

TArray<FPCGPinProperties> UWatabouLoadPropertiesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(WatabouLoadProperties::PointsPinLabel, FPCGDataTypeIdentifier{EPCGDataType::Point});

	FPCGPinProperties& MapPin = Pins.Emplace_GetRef(WatabouLoadProperties::FeatureMapPinLabel, FPCGDataTypeIdentifier{EPCGDataType::Param});
	MapPin.SetRequiredPin();

	return Pins;
}

TArray<FPCGPinProperties> UWatabouLoadPropertiesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(WatabouLoadProperties::OutPinLabel, FPCGDataTypeIdentifier{EPCGDataType::Point});
	return Pins;
}

FPCGElementPtr UWatabouLoadPropertiesSettings::CreateElement() const
{
	return MakeShared<FWatabouLoadPropertiesElement>();
}

#pragma endregion

#pragma region FWatabouLoadPropertiesElement

bool FWatabouLoadPropertiesElement::PrepareDataInternal(FPCGContext* InContext) const
{
	// PrepareData is pinned to the game thread (see CanExecuteOnlyOnMainThread): unpack the
	// Feature Map(s) and kick off the streamable-manager async load here; attribute writing
	// happens off-thread in ExecuteInternal once the assets are resident.
	FWatabouLoadPropertiesContext* Context = static_cast<FWatabouLoadPropertiesContext*>(InContext);
	check(Context);

	if (Context->WasLoadRequested()) { return true; }

	WatabouPCG::UnpackFeatureMaps(Context, Context->Unpacker, WatabouLoadProperties::FeatureMapPinLabel);

	TArray<FSoftObjectPath> ToLoad;
	Context->Unpacker.GetAssetPaths(ToLoad);

	return Context->RequestResourceLoad(Context, MoveTemp(ToLoad), /*bAsynchronous=*/true);
}

bool FWatabouLoadPropertiesElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWatabouLoadPropertiesElement::Execute);
	check(InContext);

	FWatabouLoadPropertiesContext* Context = static_cast<FWatabouLoadPropertiesContext*>(InContext);

	const UWatabouLoadPropertiesSettings* Settings = Context->GetInputSettings<UWatabouLoadPropertiesSettings>();
	check(Settings);

	// Assets streamed in by PrepareDataInternal -> resolve to live pointers.
	Context->Unpacker.ResolveAssets();

	const TSet<FName> DetailFilter(Settings->DetailKeys);
	const TSet<FName>* Filter = Settings->bLoadAllKeys ? nullptr : &DetailFilter;

	const TArray<FPCGTaggedData> PointInputs = Context->InputData.GetInputsByPin(WatabouLoadProperties::PointsPinLabel);
	for (const FPCGTaggedData& Input : PointInputs)
	{
		if (!Cast<UPCGBasePointData>(Input.Data)) { continue; }

		// Passthrough: output is a copy of the input points + the detail attributes.
		UPCGBasePointData* OutPoints = Cast<UPCGBasePointData>(Input.Data->DuplicateData(Context));
		if (!OutPoints) { continue; }

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
		Output.Data = OutPoints;
		Output.Pin = WatabouLoadProperties::OutPinLabel;

		if (Context->Unpacker.IsEmpty()) { continue; }

		UPCGMetadata* OutMeta = OutPoints->MutableMetadata();
		if (!OutMeta) { continue; }

		const WatabouPCG::EWatabouKeyDomain Domain = WatabouPCG::GetFeatureKeyDomain(OutPoints);
		if (Domain == WatabouPCG::EWatabouKeyDomain::PerPoint)
		{
			// Per-point key (merged cloud): each point is its own feature -> write details per point.
			WatabouPCG::EnsureMetadataEntries(OutPoints, /*bConservative=*/true);
			TPCGValueRange<int64> Entries = OutPoints->GetMetadataEntryValueRange();

			// TODO (perf): FindOrCreateAttribute runs per detail per point. For dense clouds, pre-resolve
			// the attribute pointer per key once (mirror PCGEx StagingLoadProperties' FPropertyCache).
			WatabouPCG::ForEachPerPointFeatureKey(OutPoints, [&](const int32 i, const int64 Key)
			{
				const UWatabouAssetBase* Asset = nullptr;
				const UWatabouFeaturesCollection* Collection = nullptr;
				int32 ElementIndex = INDEX_NONE;
				if (!Context->Unpacker.ResolveFeature(Key, Asset, Collection, ElementIndex)) { return; }

				if (Settings->bLoadFeatureType && Collection->Elements.IsValidIndex(ElementIndex))
				{
					WatabouLoadProperties::WriteFeatureIdPerPoint(OutMeta, Entries[i], Collection->Elements[ElementIndex].Id);
				}
				if (const FWatabouFeatureDetails* ElementDetails = Collection->ElementsDetails.Find(ElementIndex))
				{
					WatabouLoadProperties::WriteDetailsPerPoint(OutMeta, Entries[i], *ElementDetails, Filter);
				}
				if (Settings->bLoadCollectionDetails)
				{
					WatabouLoadProperties::WriteDetailsPerPoint(OutMeta, Entries[i], Collection->Details, Filter);
				}
			});
		}
		else if (Domain == WatabouPCG::EWatabouKeyDomain::Data)
		{
			// @Data key (single feature): resolve once -> write details once at @Data.
			int64 Key = 0;
			const UWatabouAssetBase* Asset = nullptr;
			const UWatabouFeaturesCollection* Collection = nullptr;
			int32 ElementIndex = INDEX_NONE;
			if (WatabouPCG::TryReadDataFeatureKey(OutPoints, Key)
				&& Context->Unpacker.ResolveFeature(Key, Asset, Collection, ElementIndex))
			{
				if (Settings->bLoadFeatureType && Collection->Elements.IsValidIndex(ElementIndex))
				{
					WatabouLoadProperties::WriteFeatureIdData(OutPoints, Collection->Elements[ElementIndex].Id);
				}
				if (const FWatabouFeatureDetails* ElementDetails = Collection->ElementsDetails.Find(ElementIndex))
				{
					WatabouLoadProperties::WriteDetailsData(OutPoints, *ElementDetails, Filter);
				}
				if (Settings->bLoadCollectionDetails)
				{
					WatabouLoadProperties::WriteDetailsData(OutPoints, Collection->Details, Filter);
				}
			}
		}
		// else: not Watabou-keyed geometry -> passthrough unchanged.
	}

	return true;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
