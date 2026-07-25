// Copyright 2026 Timothé Lapetite

#include "WatabouFeatureMap.h"

#include "WatabouAssetBase.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouPCGMetaHelpers.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Templates/Function.h"

// File-local recursion in a NAMED namespace (Unity-build safe).
namespace WatabouFeatureMapInternal
{
	void EnumerateRecursive(
		const UWatabouFeaturesCollection* Collection,
		int32& FlatIndex,
		TFunctionRef<void(const UWatabouFeaturesCollection*, int32, int32)> Visit)
	{
		if (!Collection) { return; }

		const int32 NumElements = Collection->Elements.Num();
		for (int32 i = 0; i < NumElements; i++)
		{
			Visit(Collection, i, FlatIndex++);
		}

		for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : Collection->SubCollections)
		{
			EnumerateRecursive(Sub, FlatIndex, Visit);
		}
	}
}

namespace WatabouPCG
{
	void EnumerateFeatures(
		const UWatabouAssetBase* Asset,
		TFunctionRef<void(const UWatabouFeaturesCollection*, int32, int32)> Visit)
	{
		if (!Asset || !Asset->Features) { return; }

		int32 FlatIndex = 0;
		WatabouFeatureMapInternal::EnumerateRecursive(Asset->Features, FlatIndex, Visit);
	}

	int64 MakeFeatureKey(const uint32 AssetId, const int32 FlatIndex)
	{
		return (static_cast<int64>(AssetId) << 32) | static_cast<int64>(static_cast<uint32>(FlatIndex));
	}

	void BreakFeatureKey(const int64 Key, uint32& OutAssetId, int32& OutFlatIndex)
	{
		const uint64 Bits = static_cast<uint64>(Key);
		OutAssetId = static_cast<uint32>(Bits >> 32);
		OutFlatIndex = static_cast<int32>(static_cast<uint32>(Bits & 0xffffffffULL));
	}

	EWatabouKeyDomain GetFeatureKeyDomain(const UPCGBasePointData* Points)
	{
		if (!Points) { return EWatabouKeyDomain::None; }

		const UPCGMetadata* Metadata = Points->ConstMetadata();
		if (!Metadata) { return EWatabouKeyDomain::None; }

		const FPCGAttributeIdentifier PerPointKeyId(FeatureMapAttributes::Key);
		if (WatabouPCGMeta::HasAttribute(Metadata, PerPointKeyId)) { return EWatabouKeyDomain::PerPoint; }

		const FPCGAttributeIdentifier DataKeyId(FeatureMapAttributes::Key, EPCGMetadataDomainFlag::Data);
		if (WatabouPCGMeta::TryGetConstAttribute<int64>(Metadata, DataKeyId)) { return EWatabouKeyDomain::Data; }

		return EWatabouKeyDomain::None;
	}

	bool TryReadDataFeatureKey(const UPCGBasePointData* Points, int64& OutKey)
	{
		if (!Points) { return false; }

		const FPCGAttributeIdentifier DataKeyId(FeatureMapAttributes::Key, EPCGMetadataDomainFlag::Data);
		if (const FPCGMetadataAttribute<int64>* Attr = WatabouPCGMeta::TryGetConstAttribute<int64>(Points->ConstMetadata(), DataKeyId))
		{
			// Parent-chain walker: a duplicated @Data attribute has no local entries (value lives in
			// the parent), so a raw local-entry read would warn.
			OutKey = WatabouPCGMeta::ReadDataValue<int64>(Attr);
			return true;
		}
		return false;
	}

	int32 ForEachPerPointFeatureKey(const UPCGBasePointData* Points, TFunctionRef<void(int32, int64)> Visit)
	{
		if (!Points) { return 0; }

		FPCGAttributePropertyInputSelector KeySelector;
		KeySelector.SetAttributeName(FeatureMapAttributes::Key);

		const TUniquePtr<const IPCGAttributeAccessor> KeyAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Points, KeySelector);
		const TUniquePtr<const IPCGAttributeAccessorKeys> KeyKeys = PCGAttributeAccessorHelpers::CreateConstKeys(Points, KeySelector);
		if (!KeyAccessor.IsValid() || !KeyKeys.IsValid()) { return 0; }

		int32 Visited = 0;
		const int32 NumPoints = Points->GetNumPoints();
		for (int32 i = 0; i < NumPoints; i++)
		{
			int64 Key = 0;
			if (KeyAccessor->Get<int64>(Key, i, *KeyKeys))
			{
				Visit(i, Key);
				++Visited;
			}
		}
		return Visited;
	}

	void UnpackFeatureMaps(FPCGContext* Context, FWatabouFeatureMapUnpacker& Unpacker, const FName PinLabel)
	{
		if (!Context) { return; }

		const TArray<FPCGTaggedData> MapInputs = Context->InputData.GetInputsByPin(PinLabel);
		for (const FPCGTaggedData& MapInput : MapInputs)
		{
			if (const UPCGParamData* MapData = Cast<UPCGParamData>(MapInput.Data)) { Unpacker.Unpack(MapData); }
		}
	}

	uint32 FWatabouFeatureMapPacker::RegisterAsset(const UWatabouAssetBase* Asset)
	{
		if (!Asset) { return 0; }

		const FSoftObjectPath Path(Asset);
		const uint32 AssetId = GetTypeHash(Path);

		if (!AssetPaths.Contains(AssetId))
		{
			AssetPaths.Add(AssetId, Path);

			TMap<TPair<const UWatabouFeaturesCollection*, int32>, int32>& Flat = FlatIndices.Add(AssetId);
			EnumerateFeatures(Asset, [&Flat](const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, const int32 FlatIndex)
			{
				Flat.Add(TPair<const UWatabouFeaturesCollection*, int32>(Collection, ElementIndex), FlatIndex);
			});
		}

		return AssetId;
	}

	int64 FWatabouFeatureMapPacker::KeyFor(const UWatabouAssetBase* Asset, const UWatabouFeaturesCollection* Collection, const int32 ElementIndex) const
	{
		if (!Asset) { return 0; }

		const uint32 AssetId = GetTypeHash(FSoftObjectPath(Asset));
		const TMap<TPair<const UWatabouFeaturesCollection*, int32>, int32>* Flat = FlatIndices.Find(AssetId);
		if (!Flat) { return 0; }

		const int32* FlatIndex = Flat->Find(TPair<const UWatabouFeaturesCollection*, int32>(Collection, ElementIndex));
		return FlatIndex ? MakeFeatureKey(AssetId, *FlatIndex) : 0;
	}

	UPCGParamData* FWatabouFeatureMapPacker::BuildMap(FPCGContext* Context) const
	{
		UPCGParamData* Param = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
		UPCGMetadata* Metadata = Param ? Param->MutableMetadata() : nullptr;
		if (!Metadata) { return Param; }

		FPCGMetadataAttribute<int32>* IdAttr = Metadata->FindOrCreateAttribute<int32>(
			FPCGAttributeIdentifier(FeatureMapAttributes::AssetId), 0, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
		FPCGMetadataAttribute<FSoftObjectPath>* PathAttr = Metadata->FindOrCreateAttribute<FSoftObjectPath>(
			FPCGAttributeIdentifier(FeatureMapAttributes::Asset), FSoftObjectPath(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);

		if (!IdAttr || !PathAttr) { return Param; }

		for (const TPair<uint32, FSoftObjectPath>& Pair : AssetPaths)
		{
			const PCGMetadataEntryKey Entry = Metadata->AddEntry();
			IdAttr->SetValue(Entry, static_cast<int32>(Pair.Key));
			PathAttr->SetValue(Entry, Pair.Value);
		}

		return Param;
	}

	bool FWatabouFeatureMapUnpacker::Unpack(const UPCGParamData* MapData)
	{
		if (!MapData) { return false; }

		FPCGAttributePropertyInputSelector IdSelector;
		IdSelector.SetAttributeName(FeatureMapAttributes::AssetId);
		FPCGAttributePropertyInputSelector PathSelector;
		PathSelector.SetAttributeName(FeatureMapAttributes::Asset);

		const TUniquePtr<const IPCGAttributeAccessor> IdAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(MapData, IdSelector);
		const TUniquePtr<const IPCGAttributeAccessor> PathAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(MapData, PathSelector);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(MapData, IdSelector);
		if (!IdAccessor.IsValid() || !PathAccessor.IsValid() || !Keys.IsValid()) { return false; }

		bool bAdded = false;
		const int32 Num = Keys->GetNum();
		for (int32 i = 0; i < Num; i++)
		{
			int32 AssetId = 0;
			FSoftObjectPath Path;
			if (IdAccessor->Get<int32>(AssetId, i, *Keys) && PathAccessor->Get<FSoftObjectPath>(Path, i, *Keys))
			{
				AssetPaths.Add(static_cast<uint32>(AssetId), Path);
				bAdded = true;
			}
		}

		return bAdded;
	}

	void FWatabouFeatureMapUnpacker::GetAssetPaths(TArray<FSoftObjectPath>& OutPaths) const
	{
		OutPaths.Reserve(OutPaths.Num() + AssetPaths.Num());
		for (const TPair<uint32, FSoftObjectPath>& Pair : AssetPaths) { OutPaths.Add(Pair.Value); }
	}

	void FWatabouFeatureMapUnpacker::ResolveAssets()
	{
		for (const TPair<uint32, FSoftObjectPath>& Pair : AssetPaths)
		{
			if (const UWatabouAssetBase* Asset = Cast<UWatabouAssetBase>(Pair.Value.ResolveObject()))
			{
				ResolvedAssets.Add(Pair.Key, Asset);
			}
		}
	}

	bool FWatabouFeatureMapUnpacker::ResolveFeature(const int64 Key, const UWatabouAssetBase*& OutAsset, const UWatabouFeaturesCollection*& OutCollection, int32& OutElementIndex) const
	{
		uint32 AssetId = 0;
		int32 FlatIndex = 0;
		BreakFeatureKey(Key, AssetId, FlatIndex);

		const UWatabouAssetBase* const* AssetPtr = ResolvedAssets.Find(AssetId);
		if (!AssetPtr || !*AssetPtr) { return false; }
		const UWatabouAssetBase* Asset = *AssetPtr;

		// Build (and cache) flat index -> (collection, element) for this asset on first use.
		TMap<int32, TPair<const UWatabouFeaturesCollection*, int32>>* Lookup = FlatLookup.Find(AssetId);
		if (!Lookup)
		{
			Lookup = &FlatLookup.Add(AssetId);
			EnumerateFeatures(Asset, [Lookup](const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, const int32 Flat)
			{
				Lookup->Add(Flat, TPair<const UWatabouFeaturesCollection*, int32>(Collection, ElementIndex));
			});
		}

		const TPair<const UWatabouFeaturesCollection*, int32>* Found = Lookup->Find(FlatIndex);
		if (!Found) { return false; }

		OutAsset = Asset;
		OutCollection = Found->Key;
		OutElementIndex = Found->Value;
		return true;
	}
}
