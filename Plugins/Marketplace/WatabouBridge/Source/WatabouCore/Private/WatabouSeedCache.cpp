// Copyright 2026 Timothé Lapetite

#include "WatabouSeedCache.h"
#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouFeatureTypes.h"
#include "WatabouFeaturesCollection.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"

#include "StructUtils/InstancedStruct.h"

namespace WatabouSeedCache_Internal
{
	// Must match the tag added in UWatabouAssetBase::GetAssetRegistryTags.
	static const FName CanonicalKeyTag = TEXT("CanonicalKey");

	static void QueryByCanonicalKey(const FString& CanonicalKey, TArray<FAssetData>& OutAssets)
	{
		IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();

		FARFilter Filter;
		Filter.ClassPaths.Add(UWatabouAssetBase::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.TagsAndValues.Add(CanonicalKeyTag, CanonicalKey);

		Registry.GetAssets(Filter, OutAssets);
	}
}

TSoftObjectPtr<UWatabouAssetBase> FWatabouSeedCache::Find(const FWatabouSeedRef& Seed)
{
	if (!Seed.IsValid()) { return nullptr; }

	TArray<TSoftObjectPtr<UWatabouAssetBase>> All;
	FindAll(Seed, All);
	return All.Num() > 0 ? All[0] : nullptr;
}

void FWatabouSeedCache::FindAll(const FWatabouSeedRef& Seed, TArray<TSoftObjectPtr<UWatabouAssetBase>>& Out)
{
	using namespace WatabouSeedCache_Internal;

	if (!Seed.IsValid()) { return; }

	TArray<FAssetData> Assets;
	QueryByCanonicalKey(Seed.GetCanonicalKey(), Assets);

	Out.Reserve(Out.Num() + Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		Out.Add(TSoftObjectPtr<UWatabouAssetBase>(Asset.GetSoftObjectPath()));
	}
}

void FWatabouSeedCache::FindMany(const TArray<FWatabouSeedRef>& Seeds, TMap<FString, TSoftObjectPtr<UWatabouAssetBase>>& OutByCanonicalKey)
{
	using namespace WatabouSeedCache_Internal;

	// Collect the distinct canonical keys to resolve.
	TSet<FString> CanonicalKeys;
	for (const FWatabouSeedRef& Seed : Seeds)
	{
		if (Seed.IsValid()) { CanonicalKeys.Add(Seed.GetCanonicalKey()); }
	}
	if (CanonicalKeys.IsEmpty()) { return; }

	// One filter, all keys ORed on the CanonicalKey tag -> a single registry scan.
	IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
	FARFilter Filter;
	Filter.ClassPaths.Add(UWatabouAssetBase::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	for (const FString& Key : CanonicalKeys) { Filter.TagsAndValues.Add(CanonicalKeyTag, Key); }

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	OutByCanonicalKey.Reserve(OutByCanonicalKey.Num() + Assets.Num());
	for (const FAssetData& Asset : Assets)
	{
		FString Key;
		if (Asset.GetTagValue(CanonicalKeyTag, Key) && !OutByCanonicalKey.Contains(Key))
		{
			OutByCanonicalKey.Add(Key, TSoftObjectPtr<UWatabouAssetBase>(Asset.GetSoftObjectPath()));
		}
	}
}

namespace WatabouSeedCache_Internal
{
	static void HarvestDetails(
		const FWatabouFeatureDetails& Details,
		const TSet<FName>& AllowedTargetGenerators,
		TArray<FWatabouSeedRef>& OutRefs)
	{
		for (const TPair<FName, FInstancedStruct>& Pair : Details.StructValues)
		{
			if (Pair.Value.GetScriptStruct() != FWatabouSeedRef::StaticStruct()) { continue; }

			const FWatabouSeedRef& Ref = Pair.Value.Get<FWatabouSeedRef>();
			if (!Ref.IsValid()) { continue; }

			if (AllowedTargetGenerators.Num() > 0
				&& !AllowedTargetGenerators.Contains(FName(*Ref.GeneratorId)))
			{
				continue;
			}

			OutRefs.Add(Ref);
		}
	}

	static void HarvestCollection(
		const UWatabouFeaturesCollection* Collection,
		const TSet<FName>& AllowedTargetGenerators,
		TArray<FWatabouSeedRef>& OutRefs)
	{
		if (!Collection) { return; }

		HarvestDetails(Collection->Details, AllowedTargetGenerators, OutRefs);
		for (const TPair<int32, FWatabouFeatureDetails>& Pair : Collection->ElementsDetails)
		{
			HarvestDetails(Pair.Value, AllowedTargetGenerators, OutRefs);
		}
		for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : Collection->SubCollections)
		{
			HarvestCollection(Sub, AllowedTargetGenerators, OutRefs);
		}
	}
}

void FWatabouSeedCache::GatherRefs(
	const UWatabouAssetBase* InAsset,
	const TSet<FName>& AllowedTargetGenerators,
	TArray<FWatabouSeedRef>& OutRefs)
{
	using namespace WatabouSeedCache_Internal;

	if (!InAsset || !InAsset->Features) { return; }
	HarvestCollection(InAsset->Features, AllowedTargetGenerators, OutRefs);
}

namespace WatabouSeedCache_Internal
{
	// Collect mutable pointers to every nested FWatabouSeedRef in the tree. The pointers stay valid as
	// long as the tree isn't restructured (FindMany below doesn't touch it), so the caller resolves all
	// keys in ONE batched query and writes each ResolvedAsset back through them.
	static void CollectSeedRefs(UWatabouFeaturesCollection* Collection, TArray<FWatabouSeedRef*>& Out)
	{
		if (!Collection) { return; }

		auto Harvest = [&Out](FWatabouFeatureDetails& Details)
		{
			for (TPair<FName, FInstancedStruct>& Pair : Details.StructValues)
			{
				if (Pair.Value.GetScriptStruct() != FWatabouSeedRef::StaticStruct()) { continue; }
				FWatabouSeedRef& Ref = Pair.Value.GetMutable<FWatabouSeedRef>();
				if (Ref.IsValid()) { Out.Add(&Ref); }
			}
		};

		Harvest(Collection->Details);
		for (TPair<int32, FWatabouFeatureDetails>& Pair : Collection->ElementsDetails) { Harvest(Pair.Value); }
		for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : Collection->SubCollections) { CollectSeedRefs(Sub, Out); }
	}
}

bool FWatabouSeedCache::RelinkAsset(UWatabouAssetBase* InAsset)
{
	using namespace WatabouSeedCache_Internal;

	if (!InAsset || !InAsset->Features) { return false; }

	TArray<FWatabouSeedRef*> Refs;
	CollectSeedRefs(InAsset->Features, Refs);
	if (Refs.IsEmpty()) { return false; }   // leaf asset / no nested refs -- nothing to link

	// One batched registry query for every nested key, instead of one Find per ref. Re-resolves from
	// identity (NOT the existing pointer), so a stale pointer self-heals.
	TArray<FWatabouSeedRef> Keys;
	Keys.Reserve(Refs.Num());
	for (const FWatabouSeedRef* Ref : Refs) { Keys.Add(*Ref); }
	TMap<FString, TSoftObjectPtr<UWatabouAssetBase>> ByKey;
	FindMany(Keys, ByKey);

	// Decide first so we Modify() (transact + dirty) only on a real change, and BEFORE mutating so undo
	// captures the pre-edit state.
	bool bChanged = false;
	for (const FWatabouSeedRef* Ref : Refs)
	{
		const TSoftObjectPtr<UWatabouAssetBase>* Found = ByKey.Find(Ref->GetCanonicalKey());
		const FSoftObjectPath NewPath = Found ? Found->ToSoftObjectPath() : FSoftObjectPath();
		if (Ref->ResolvedAsset.ToSoftObjectPath() != NewPath) { bChanged = true; break; }
	}
	if (!bChanged) { return false; }

	InAsset->Modify();
	for (FWatabouSeedRef* Ref : Refs)
	{
		const TSoftObjectPtr<UWatabouAssetBase>* Found = ByKey.Find(Ref->GetCanonicalKey());
		Ref->ResolvedAsset = Found ? *Found : TSoftObjectPtr<UWatabouAssetBase>();
	}
	return true;
}
