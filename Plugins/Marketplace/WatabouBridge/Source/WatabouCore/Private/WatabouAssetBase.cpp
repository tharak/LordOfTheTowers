// Copyright 2026 Timothé Lapetite

#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouEditorBridge.h"
#include "WatabouFeaturesCollection.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/UObjectGlobals.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UScriptStruct* FWatabouDetailSlot::ResolveStructType() const
{
	if (Kind != EWatabouDetailKind::Struct || StructTypeName.IsNone()) { return nullptr; }
	if (UScriptStruct* Cached = CachedStructType.Get()) { return Cached; }
	UScriptStruct* Resolved = FindFirstObject<UScriptStruct>(*StructTypeName.ToString(), EFindFirstObjectOptions::NativeFirst);
	CachedStructType = Resolved;
	return Resolved;
}

UWatabouAssetBase::UWatabouAssetBase()
{
	Features = CreateDefaultSubobject<UWatabouFeaturesCollection>(TEXT("Features"));
}

void UWatabouAssetBase::Reset()
{
	Title.Empty();
	GenerationTags.Reset();
	Bounds = FBox(ForceInit);
	Identifiers.Reset();
	DiscoveredDetailKeys.Reset();
	if (Features) { Features->Reset(); }
}

void UWatabouAssetBase::BumpIdentifier(EWatabouFeatureType InType, FName InId)
{
	++Identifiers.FindOrAdd(FWatabouFeatureIdentifier(InType, InId));
}

namespace WatabouAssetBase_Internal
{
	/**
	 * Record one (Key, Kind [, StructType]) entry into the manifest; warn on collision.
	 * Collision = same key seen with a different Kind, or same Kind=Struct with a
	 * different StructTypeName. First-seen wins; subsequent disagreements log warnings.
	 */
	void RecordKey(UWatabouAssetBase* Asset, FName Key, EWatabouDetailKind Kind, FName StructTypeName = NAME_None)
	{
		FWatabouDetailSlot& Stored = Asset->DiscoveredDetailKeys.FindOrAdd(Key);
		if (Stored.Kind == EWatabouDetailKind::None)
		{
			Stored.Kind = Kind;
			Stored.StructTypeName = StructTypeName;
			return;
		}
		if (Stored.Kind != Kind)
		{
			UE_LOG(LogWatabou, Warning,
				TEXT("DiscoveredDetailKeys: key '%s' seen with multiple kinds (kept %d, ignored %d) -- parser hygiene drift?"),
				*Key.ToString(), static_cast<int32>(Stored.Kind), static_cast<int32>(Kind));
			return;
		}
		if (Kind == EWatabouDetailKind::Struct && Stored.StructTypeName != StructTypeName)
		{
			UE_LOG(LogWatabou, Warning,
				TEXT("DiscoveredDetailKeys: key '%s' seen with multiple struct types (kept '%s', ignored '%s') -- parser hygiene drift?"),
				*Key.ToString(), *Stored.StructTypeName.ToString(), *StructTypeName.ToString());
		}
	}

	void HarvestDetails(const FWatabouFeatureDetails& Details, UWatabouAssetBase* Asset)
	{
		for (const TPair<FName, FString>& Pair : Details.StringValues)  { RecordKey(Asset, Pair.Key, EWatabouDetailKind::String);  }
		for (const TPair<FName, FName>&   Pair : Details.NameValues)    { RecordKey(Asset, Pair.Key, EWatabouDetailKind::Name);    }
		for (const TPair<FName, double>&  Pair : Details.NumericValues) { RecordKey(Asset, Pair.Key, EWatabouDetailKind::Numeric); }

		for (const TPair<FName, FInstancedStruct>& Pair : Details.StructValues)
		{
			const UScriptStruct* ScriptStruct = Pair.Value.GetScriptStruct();
			const FName StructTypeName = ScriptStruct ? ScriptStruct->GetFName() : NAME_None;
			RecordKey(Asset, Pair.Key, EWatabouDetailKind::Struct, StructTypeName);
		}
	}

	void HarvestCollection(const UWatabouFeaturesCollection* Collection, UWatabouAssetBase* Asset)
	{
		if (!Collection) { return; }

		HarvestDetails(Collection->Details, Asset);
		for (const TPair<int32, FWatabouFeatureDetails>& Pair : Collection->ElementsDetails)
		{
			HarvestDetails(Pair.Value, Asset);
		}
		for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : Collection->SubCollections)
		{
			HarvestCollection(Sub, Asset);
		}
	}
}

void UWatabouAssetBase::AggregateMetadata(UWatabouAssetBase* InAsset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWatabouAssetBase::AggregateMetadata);

	if (!InAsset) { return; }
	InAsset->DiscoveredDetailKeys.Reset();
	WatabouAssetBase_Internal::HarvestCollection(InAsset->Features, InAsset);
}

#if WITH_EDITOR
void UWatabouAssetBase::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);
	Context.AddTag(FAssetRegistryTag(
		TEXT("CanonicalKey"),
		SourceSeed.GetCanonicalKey(),
		FAssetRegistryTag::TT_Hidden));
	Context.AddTag(FAssetRegistryTag(
		TEXT("CanonicalHash"),
		SourceSeed.GetCanonicalHash(),
		FAssetRegistryTag::TT_Hidden));
	Context.AddTag(FAssetRegistryTag(
		TEXT("GeneratorId"),
		SourceSeed.GeneratorId,
		FAssetRegistryTag::TT_Alphabetical));
}

void UWatabouAssetBase::OpenInImportWindow()
{
	if (SourceSeed.GeneratorId.IsEmpty())
	{
		UE_LOG(LogWatabou, Warning,
			TEXT("OpenInImportWindow: '%s' has no SourceSeed generator -- nothing to open."), *GetName());
		return;
	}

	// Fire the editor-bound hook (see WatabouEditorBridge). Unbound in cooked/runtime builds.
	FWatabouEditorBridge::OnOpenInImporter.ExecuteIfBound(SourceSeed);
}
#endif
