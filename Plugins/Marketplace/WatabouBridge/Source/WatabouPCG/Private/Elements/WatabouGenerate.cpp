// Copyright 2026 Timothé Lapetite

#include "Elements/WatabouGenerate.h"

#include "WatabouAssetBase.h"
#include "WatabouSeedRef.h"
#include "WatabouSeedCache.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouFeatureTypes.h"
#include "WatabouGeneratorRegistry.h"
#include "WatabouGeneratorInfo.h"
#include "WatabouFeatureMap.h"
#include "WatabouForward.h"
#include "WatabouOrientedBox.h"
#include "WatabouMinAreaBox2D.h"
#include "Processors/WatabouProcessor.h"
#include "Processors/WatabouGeoJSONProcessor.h"

#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGData.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Templates/Function.h"
#include "Async/ParallelFor.h"

#define LOCTEXT_NAMESPACE "WatabouPCG"

// File-local pin labels + processor resolution (named namespace -> Unity-build safe).
namespace WatabouGenerate
{
	const FName SeedsPinLabel = TEXT("Seeds");
	const FName OutPinLabel = TEXT("Out");
	const FName FeatureMapPinLabel = TEXT("Feature Map");

	TSharedPtr<IWatabouProcessor> ResolveProcessor(const UWatabouAssetBase* Asset)
	{
		if (!Asset) { return nullptr; }

		// Generator-specific processor (registered on the generator's info), else generic fallback.
		if (const TSharedPtr<FWatabouGeneratorInfo> Info = FWatabouGeneratorRegistry::Get().FindById(Asset->SourceSeed.GeneratorId))
		{
			if (TSharedPtr<IWatabouProcessor> Processor = Info->CreateProcessor()) { return Processor; }
		}

		return MakeShared<FWatabouGeoJSONProcessor>();
	}

	// True if the asset's processor orients its content to the seed footprint (Dwellings). Gates the
	// min-area-box placement + footprint asymmetry cue so the city-family passthrough stays on the generic
	// oriented box. Cheap: processors are stateless lightweight shared instances.
	bool AssetUsesFootprintAlignment(const UWatabouAssetBase* Asset)
	{
		const TSharedPtr<IWatabouProcessor> Processor = ResolveProcessor(Asset);
		return Processor.IsValid() && Processor->UsesFootprintAlignment();
	}

	// Read a seed's asset soft path from the resolution attribute (FSoftObjectPath, FString fallback).
	FSoftObjectPath ReadAttributePath(const IPCGAttributeAccessor& Accessor, const IPCGAttributeAccessorKeys& Keys, const int32 Index)
	{
		FSoftObjectPath Path;
		if (!Accessor.Get<FSoftObjectPath>(Path, Index, Keys))
		{
			FString PathString;
			if (Accessor.Get<FString>(PathString, Index, Keys)) { Path = FSoftObjectPath(PathString); }
		}
		return Path;
	}

	// Single placement for an @Data (single-feature) Seed Ref input: all points share one key, so the one
	// resolved child is stamped once. Mode (node setting) chooses how location/rotation are derived;
	// scale (and any component a mode leaves unspecified) comes from the first point.
	FTransform ComputeDataPlacement(const TConstPCGValueRange<FTransform>& Transforms, const int32 NumPoints, const EWatabouDataOrigin Mode, const bool bUseMinAreaBox, TArray<FVector2D>& OutFootprintWorldXY)
	{
		OutFootprintWorldXY.Reset();
		if (NumPoints <= 0) { return FTransform::Identity; }

		FTransform Placement = Transforms[0]; // base: first point's rotation + scale + location
		const FVector FirstLocation = Transforms[0].GetLocation();

		auto Centroid = [&Transforms, NumPoints]() -> FVector
		{
			FVector C = FVector::ZeroVector;
			for (int32 i = 0; i < NumPoints; i++) { C += Transforms[i].GetLocation(); }
			return C / static_cast<double>(NumPoints);
		};

		// Centroid XY + first point's Z (P0 rotation/scale kept). Shared by the CentroidXYFirstZ mode and
		// the degenerate fallbacks of the OrientedBox mode.
		auto SetCentroidXYFirstZ = [&Placement, &Centroid, &FirstLocation]()
		{
			const FVector C = Centroid();
			Placement.SetLocation(FVector(C.X, C.Y, FirstLocation.Z));
		};

		switch (Mode)
		{
		case EWatabouDataOrigin::FirstPoint:
			break; // location/rotation/scale all from the first point

		case EWatabouDataOrigin::Centroid:
			Placement.SetLocation(Centroid());
			break;

		case EWatabouDataOrigin::CentroidXYFirstZ:
			SetCentroidXYFirstZ();
			break;

		case EWatabouDataOrigin::OrientedBox:
			{
				// OBB over the points' XY: center + in-plane yaw (longest axis = X). Z from the first
				// point -- basements push the OBB's center Z away from the entry level. Scale from P0.
				TArray<FVector2D> XY;
				XY.Reserve(NumPoints);
				for (int32 i = 0; i < NumPoints; i++)
				{
					const FVector L = Transforms[i].GetLocation();
					XY.Add(FVector2D(L.X, L.Y));
				}

				if (bUseMinAreaBox)
				{
					// Footprint-aligned generators (Dwellings) fit the SAME min-area box the import-time
					// encoder used (WatabouMath), so the stamp frame matches the plan frame -- no skew on
					// skewed / non-rectangular footprints (unlike DiTO, which snaps to its own axes). The
					// footprint outline is forwarded so the processor can best-fit the orientation to it.
					WatabouMath::FMinAreaBox2D Box;
					if (WatabouMath::ComputeMinAreaBox2D(XY, Box))
					{
						const FVector2D Center = Box.Center();
						const FVector2D LongDir = Box.LongAxisDir();
						Placement.SetLocation(FVector(Center.X, Center.Y, FirstLocation.Z));
						Placement.SetRotation(FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(LongDir.Y, LongDir.X)), 0.0).Quaternion());
						OutFootprintWorldXY = XY; // world footprint outline -> processor best-fit overlap
					}
					else
					{
						SetCentroidXYFirstZ(); // degenerate -> centroid XY + first Z (keep P0 rotation)
					}
				}
				else
				{
					// Generic oriented box (DiTO) for non-footprint-aligned @Data stamps -- unchanged.
					const WatabouPCG::FOrientedBox2D Box = WatabouPCG::ComputeOrientedBox2D(XY);
					if (Box.bValid)
					{
						Placement.SetLocation(FVector(Box.Center.X, Box.Center.Y, FirstLocation.Z));
						Placement.SetRotation(FRotator(0.0, Box.YawDegrees, 0.0).Quaternion());
					}
					else
					{
						SetCentroidXYFirstZ(); // degenerate -> centroid XY + first Z (keep P0 rotation)
					}
				}
			}
			break;
		}

		return Placement;
	}

	// Append Path to OutPaths if non-null and not already present (dedup via Seen, single hash probe).
	void AddUniquePath(TSet<FSoftObjectPath>& Seen, TArray<FSoftObjectPath>& OutPaths, const FSoftObjectPath& Path)
	{
		if (Path.IsNull()) { return; }
		bool bAlready = false;
		Seen.Add(Path, &bAlready);
		if (!bAlready) { OutPaths.Add(Path); }
	}

	// Resolve a soft path to a UWatabouAssetBase*, caching the (possibly null) result. Assets are
	// already resident (streamed in by PrepareDataInternal), so this resolves only, never loads.
	UWatabouAssetBase* ResolveAssetCached(TMap<FSoftObjectPath, UWatabouAssetBase*>& Cache, const FSoftObjectPath& Path)
	{
		if (UWatabouAssetBase** Found = Cache.Find(Path)) { return *Found; }
		UWatabouAssetBase* Resolved = Cast<UWatabouAssetBase>(Path.ResolveObject());
		Cache.Add(Path, Resolved);
		return Resolved;
	}

	// Per-seed Floor Height reader for one input: the FloorHeightAttribute (attribute > constant) when
	// enabled and resolvable, else the node constant. The returned closure owns its accessor and is
	// valid for the lifetime of Points. Index out of an attribute that fails to read falls back to the
	// constant, so a partially-populated attribute still produces a sane height everywhere.
	TFunction<double(int32)> MakeFloorHeightReader(const UPCGBasePointData* Points, const UWatabouGenerateSettings* Settings)
	{
		const double Constant = Settings->FloorHeight;
		if (!Settings->bApplyFloorHeight || !Settings->bFloorHeightFromAttribute)
		{
			return [Constant](int32) { return Constant; };
		}

		const FPCGAttributePropertyInputSelector Source = Settings->FloorHeightAttribute.CopyAndFixLast(Points);
		TSharedPtr<const IPCGAttributeAccessor> Accessor(PCGAttributeAccessorHelpers::CreateConstAccessor(Points, Source).Release());
		TSharedPtr<const IPCGAttributeAccessorKeys> Keys(PCGAttributeAccessorHelpers::CreateConstKeys(Points, Source).Release());
		if (!Accessor.IsValid() || !Keys.IsValid())
		{
			return [Constant](int32) { return Constant; };
		}

		return [Accessor, Keys, Constant](const int32 Index) -> double
		{
			double Value = Constant;
			if (!Accessor->Get<double>(Value, Index, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				Value = Constant;
			}
			return Value;
		};
	}
}

#pragma region UWatabouGenerateSettings

#if WITH_EDITOR
FText UWatabouGenerateSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("WatabouGenerateTitle", "Watabou | Generate");
}

FText UWatabouGenerateSettings::GetNodeTooltipText() const
{
	return LOCTEXT("WatabouGenerateTooltip", "Stamp a Watabou data asset's geometry onto input seed points, with a Feature Map for on-demand details / recursion.");
}
#endif

TArray<FPCGPinProperties> UWatabouGenerateSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(WatabouGenerate::SeedsPinLabel, FPCGDataTypeIdentifier{EPCGDataType::Point});

	// Seed Ref consumes the upstream node's Feature Map to resolve each seed's child asset.
	if (SourceMode == EWatabouAssetSource::SeedRef)
	{
		FPCGPinProperties& MapPin = Pins.Emplace_GetRef(WatabouGenerate::FeatureMapPinLabel, FPCGDataTypeIdentifier{EPCGDataType::Param});
		MapPin.SetRequiredPin();
	}

	return Pins;
}

TArray<FPCGPinProperties> UWatabouGenerateSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;

	// Default geometry pin: catch-all for ids without a Named-Pin rule.
	Pins.Emplace(WatabouGenerate::OutPinLabel, FPCGDataTypeIdentifier{EPCGDataType::Point});

	// One pin per distinct Named-Pin rule (pin defaults to the feature id).
	for (const FWatabouFeatureRule& Rule : Rules)
	{
		if (Rule.Output != EWatabouFeatureOutput::Pin) { continue; }

		const FName PinName = Rule.Pin.IsNone() ? Rule.Id : Rule.Pin;
		if (PinName.IsNone() || PinName == WatabouGenerate::OutPinLabel) { continue; }

		const bool bExists = Pins.ContainsByPredicate([&PinName](const FPCGPinProperties& Pin) { return Pin.Label == PinName; });
		if (!bExists) { Pins.Emplace(PinName, FPCGDataTypeIdentifier{EPCGDataType::Point}); }
	}

	// Feature Map: asset id -> path, for downstream property / recursion nodes.
	Pins.Emplace(WatabouGenerate::FeatureMapPinLabel, FPCGDataTypeIdentifier{EPCGDataType::Param});

	return Pins;
}

FPCGElementPtr UWatabouGenerateSettings::CreateElement() const
{
	return MakeShared<FWatabouGenerateElement>();
}

#if WITH_EDITOR
void UWatabouGenerateSettings::EDITOR_PopulateRules()
{
	UWatabouAssetBase* LoadedAsset = Asset.LoadSynchronous();
	if (!LoadedAsset) { return; }

	const TSharedPtr<IWatabouProcessor> Processor = WatabouGenerate::ResolveProcessor(LoadedAsset);
	if (!Processor) { return; }

	TArray<FName> Labels;
	Processor->GetOutputLabels(LoadedAsset, Labels);
	if (Labels.IsEmpty()) { return; }

	Modify();
	for (const FName& Label : Labels)
	{
		const bool bExists = Rules.ContainsByPredicate([&Label](const FWatabouFeatureRule& Rule) { return Rule.Id == Label; });
		if (!bExists)
		{
			FWatabouFeatureRule Rule;
			Rule.Id = Label;
			Rules.Add(Rule);
		}
	}
}
#endif

#pragma endregion

#pragma region FWatabouGenerateElement

bool FWatabouGenerateElement::PrepareDataInternal(FPCGContext* InContext) const
{
	// Runs in the PrepareData phase, which CanExecuteOnlyOnMainThread pins to the game thread:
	// the streamable manager (RequestResourceLoad) must be invoked there. Emission then runs
	// off-thread in ExecuteInternal once the assets are resident.
	FWatabouGenerateContext* Context = static_cast<FWatabouGenerateContext*>(InContext);
	check(Context);

	if (Context->WasLoadRequested()) { return true; }

	const UWatabouGenerateSettings* Settings = Context->GetInputSettings<UWatabouGenerateSettings>();
	check(Settings);

	if (Settings->SourceMode == EWatabouAssetSource::ConstructFromAttribute)
	{
		// Scaffold only: no live seed-ref construction yet -> nothing to load, nothing to emit.
		PCGE_LOG(Warning, GraphAndLog, LOCTEXT("ConstructFromAttrUnimplemented", "Watabou | Generate: 'Construct From Attribute' is not yet implemented; no geometry is emitted."));
		return true;
	}

	if (Settings->SourceMode == EWatabouAssetSource::SeedRef)
	{
		// No label -> no seed ref can be found; warn and emit nothing rather than fail silently.
		if (Settings->SeedRefLabel.IsNone())
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("SeedRefNoLabel", "Watabou | Generate: Seed Ref mode needs a Seed Ref Label, but none is set -- no children are stamped."));
			return true;
		}

		// Unpack the upstream node's Feature Map(s) -- the parent assets that produced the seed points.
		WatabouPCG::UnpackFeatureMaps(Context, Context->Unpacker, WatabouGenerate::FeatureMapPinLabel);

		if (Context->Unpacker.IsEmpty())
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("SeedRefEmptyMap", "Watabou | Generate: Seed Ref mode got an empty Feature Map -- wire the upstream Watabou Generate node's 'Feature Map' output into this node's 'Feature Map' input."));
			return true;
		}

		// Sync-load the parents here (this phase is game-thread-pinned). Their seed refs are read and
		// the child soft paths extracted in this same synchronous pass -- before any GC can run -- so
		// the parents need no pinning; only the children stream in asynchronously (one round) below.
		TArray<FSoftObjectPath> ParentPaths;
		Context->Unpacker.GetAssetPaths(ParentPaths);
		for (const FSoftObjectPath& ParentPath : ParentPaths)
		{
			if (!ParentPath.TryLoad())
			{
				PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("SeedRefParentLoadFailed", "Watabou | Generate: parent asset '{0}' could not be loaded; its seeds are skipped."), FText::FromString(ParentPath.ToString())));
			}
		}
		Context->Unpacker.ResolveAssets();

		// Pass 1: for each DISTINCT upstream key, find the seed ref under SeedRefLabel on its feature.
		TSet<int64> SeenKeys;
		TMap<int64, FWatabouSeedRef> KeyToRef;
		auto GatherRef = [&](const int64 Key)
		{
			bool bAlreadySeen = false;
			SeenKeys.Add(Key, &bAlreadySeen);
			if (bAlreadySeen) { return; }

			const UWatabouAssetBase* Asset = nullptr;
			const UWatabouFeaturesCollection* Collection = nullptr;
			int32 ElementIndex = INDEX_NONE;
			if (!Context->Unpacker.ResolveFeature(Key, Asset, Collection, ElementIndex)) { return; }

			// Record the parent asset path for the processor's parent hook (parents are resident here;
			// resolved best-effort in ExecuteInternal). Cheap -- one entry per distinct resolvable key.
			if (Asset) { Context->KeyToParentPath.Add(Key, FSoftObjectPath(Asset)); }

			// Per-feature only: a seed ref is a property of a specific feature (its ElementsDetails).
			const FWatabouFeatureDetails* ElementDetails = Collection->ElementsDetails.Find(ElementIndex);
			if (!ElementDetails) { return; }

			const FInstancedStruct* Struct = ElementDetails->StructValues.Find(Settings->SeedRefLabel);
			if (!Struct) { return; }

			const FWatabouSeedRef* Ref = Struct->GetPtr<FWatabouSeedRef>();
			if (Ref && Ref->IsValid()) { KeyToRef.Add(Key, *Ref); }
		};

		const TArray<FPCGTaggedData> SeedInputs = Context->InputData.GetInputsByPin(WatabouGenerate::SeedsPinLabel);
		for (const FPCGTaggedData& Input : SeedInputs)
		{
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Input.Data);
			if (!Points) { continue; }

			int64 DataKey = 0;
			switch (WatabouPCG::GetFeatureKeyDomain(Points))
			{
			case WatabouPCG::EWatabouKeyDomain::PerPoint:
				WatabouPCG::ForEachPerPointFeatureKey(Points, [&GatherRef](const int32, const int64 Key) { GatherRef(Key); });
				break;
			case WatabouPCG::EWatabouKeyDomain::Data:
				if (WatabouPCG::TryReadDataFeatureKey(Points, DataKey)) { GatherRef(DataKey); }
				break;
			default:
				break;
			}
		}

		// Resolve each distinct seed ref to its imported child. The canonical-key registry match is
		// AUTHORITATIVE (the asset provably exists), so resolve every ref in one batched query and prefer
		// it. Fall back to the ref's own ResolvedAsset pointer ONLY when the key no longer resolves (key
		// drift) -- a stale pointer to a deleted/moved child must never shadow a live same-key asset.
		TArray<FWatabouSeedRef> Refs;
		KeyToRef.GenerateValueArray(Refs);
		TMap<FString, TSoftObjectPtr<UWatabouAssetBase>> ChildByCanonical;
		FWatabouSeedCache::FindMany(Refs, ChildByCanonical);

		// Pass 2: map each key to its resolved child path; dedup the set of children to stream in.
		TSet<FSoftObjectPath> UniqueChild;
		TArray<FSoftObjectPath> ChildToLoad;
		for (const TPair<int64, FWatabouSeedRef>& Pair : KeyToRef)
		{
			FSoftObjectPath ChildPath;
			if (const TSoftObjectPtr<UWatabouAssetBase>* Child = ChildByCanonical.Find(Pair.Value.GetCanonicalKey()))
			{
				ChildPath = Child->ToSoftObjectPath();
			}
			else if (!Pair.Value.ResolvedAsset.IsNull())
			{
				ChildPath = Pair.Value.ResolvedAsset.ToSoftObjectPath();
			}
			if (ChildPath.IsNull()) { continue; } // No imported child for this seed -> point will be skipped.

			Context->KeyToChildPath.Add(Pair.Key, ChildPath);
			WatabouGenerate::AddUniquePath(UniqueChild, ChildToLoad, ChildPath);
		}

		// Nothing resolved -> point at the stage that came up empty (each is a common setup slip).
		if (ChildToLoad.IsEmpty())
		{
			if (SeenKeys.IsEmpty())
			{
				PCGE_LOG(Warning, GraphAndLog, LOCTEXT("SeedRefNoKeys", "Watabou | Generate: Seed Ref mode found no WatabouKey on the Seeds input -- those points must be the keyed geometry from a Watabou Generate node, not raw points."));
			}
			else if (KeyToRef.IsEmpty())
			{
				PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("SeedRefNoRefs", "Watabou | Generate: Seed Ref mode saw {0} keyed seed(s), but none carry a seed ref under label '{1}'. Check the Seed Ref Label (building footprints use 'dwellings')."), FText::AsNumber(SeenKeys.Num()), FText::FromName(Settings->SeedRefLabel)));
			}
			else
			{
				// Refs is non-empty here (KeyToRef is). Log the exact canonical key we are matching on:
				// if it differs from an imported asset's SourceSeed key (e.g. a provenance 'from' param,
				// or params recorded from the browser state URL), that divergence is the cause.
				const FString WantKey = Refs.Num() > 0 ? Refs[0].GetCanonicalKey() : FString();
				PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("SeedRefNoChildren", "Watabou | Generate: Seed Ref mode found {0} '{1}' seed ref(s), but none resolve to an imported asset. Matching on canonical key e.g. '{2}' -- if an imported asset's SourceSeed differs (e.g. a 'from' provenance param, or params taken from the browser URL), that mismatch is why. Otherwise import the referenced children first."), FText::AsNumber(KeyToRef.Num()), FText::FromName(Settings->SeedRefLabel), FText::FromString(WantKey)));
			}
		}

		// One async round: stream in the resolved children. Parents may unload now (we hold only paths).
		return Context->RequestResourceLoad(Context, MoveTemp(ChildToLoad), /*bAsynchronous=*/true);
	}

	const bool bAttributeMode = Settings->SourceMode == EWatabouAssetSource::Attribute;

	// TODO (perf): in Attribute mode the resolution attribute is read twice -- here (gather paths
	// for the async load) and again during emission. Attribute reads can be slow. Mirror PCGEx's
	// TAssetLoader (Plugins/PCGExtendedToolkit/Source/PCGExCore/Public/Helpers/PCGExAssetLoader.h):
	// build, per input, a 1:1 array of per-point path keys plus a deduped path->asset map once.
	// Deferred: inputs are seeds, so counts are expected to be small.

	TArray<FSoftObjectPath> ToLoad;
	TSet<FSoftObjectPath> Unique;

	if (bAttributeMode)
	{
		const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(WatabouGenerate::SeedsPinLabel);
		for (const FPCGTaggedData& Input : Inputs)
		{
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Input.Data);
			if (!Points) { continue; }

			const FPCGAttributePropertyInputSelector Source = Settings->AssetAttribute.CopyAndFixLast(Points);
			const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Points, Source);
			const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Points, Source);
			if (!Accessor.IsValid() || !Keys.IsValid())
			{
				PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("AssetAttrNotFound", "Asset source attribute '{0}' was not found on a seed input; those seeds are skipped."), Source.GetDisplayText()));
				continue;
			}

			const int32 NumPoints = Points->GetNumPoints();
			for (int32 i = 0; i < NumPoints; i++)
			{
				WatabouGenerate::AddUniquePath(Unique, ToLoad, WatabouGenerate::ReadAttributePath(*Accessor, *Keys, i));
			}
		}
	}
	else if (!Settings->Asset.IsNull())
	{
		WatabouGenerate::AddUniquePath(Unique, ToLoad, Settings->Asset.ToSoftObjectPath());
	}

	// Returns false (pause) while loading, true once resident / nothing to load.
	return Context->RequestResourceLoad(Context, MoveTemp(ToLoad), /*bAsynchronous=*/true);
}

bool FWatabouGenerateElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWatabouGenerateElement::Execute);
	check(InContext);

	FWatabouGenerateContext* Context = static_cast<FWatabouGenerateContext*>(InContext);

	const UWatabouGenerateSettings* Settings = Context->GetInputSettings<UWatabouGenerateSettings>();
	check(Settings);

	if (Settings->SourceMode == EWatabouAssetSource::ConstructFromAttribute)
	{
		// Scaffold only: PrepareDataInternal already warned -> just emit nothing.
		return true;
	}

	// Build emit params (flatten Rules; unlisted ids keep the implicit Default behavior).
	FWatabouProcessParams Params;
	{
		FTransform Local = Settings->Transform;
		Local.SetScale3D(Local.GetScale3D() * Settings->ScaleFactor);
		Params.LocalTransform = Local;
	}
	Params.PathlikeTag = Settings->PathlikeTag;
	Params.DefaultPin = WatabouGenerate::OutPinLabel;
	Params.bApplyFloorHeight = Settings->bApplyFloorHeight;
	Params.bApplyFacing = Settings->bApplyFacing;
	Params.bApplyMajorAxisAlign = Settings->bApplyMajorAxisAlign;
	Params.AssetAnchor = Settings->AssetAnchor;
	for (const FWatabouFeatureRule& Rule : Settings->Rules)
	{
		if (Rule.Id.IsNone()) { continue; }

		if (Rule.Output == EWatabouFeatureOutput::Skip)
		{
			Params.SkipIds.Add(Rule.Id);
		}
		else if (Rule.Output == EWatabouFeatureOutput::Pin)
		{
			Params.LabelToPin.Add(Rule.Id, Rule.Pin.IsNone() ? Rule.Id : Rule.Pin);
		}

		if (Rule.bMergePoints) { Params.MergeSinglePoints.Add(Rule.Id); }
		if (Rule.bReduceToOrientedBox) { Params.ReduceToOrientedBoxIds.Add(Rule.Id); }
	}

	WatabouPCG::FWatabouFeatureMapPacker Packer;

	// Phase 0 accumulation: collect every target, its (stateless, reused) processor, and the per-input
	// forward handlers. Handlers live in a stable container so the targets' back-pointers survive the later
	// parallel phases. Compute + emit happen after all targets are gathered.
	TArray<FWatabouProcessTarget> Targets;
	TArray<TSharedPtr<IWatabouProcessor>> Processors;
	TArray<TUniquePtr<FWatabouForwardHandler>> ForwardHandlers;
	TMap<FName, TSharedPtr<IWatabouProcessor>> ProcessorByGenerator;

	// Collect one target: pin its processor (one instance per generator id, reused) and register its asset
	// so the compute phase's KeyFor reads stay race-free. A null asset / processor is a no-op.
	auto Stamp = [&](const FWatabouProcessTarget& Target)
	{
		if (!Target.Asset) { return; }

		const FName GeneratorId(*Target.Asset->SourceSeed.GeneratorId);
		TSharedPtr<IWatabouProcessor> Processor;
		if (const TSharedPtr<IWatabouProcessor>* Found = ProcessorByGenerator.Find(GeneratorId))
		{
			Processor = *Found;
		}
		else
		{
			Processor = WatabouGenerate::ResolveProcessor(Target.Asset);
			ProcessorByGenerator.Add(GeneratorId, Processor);
		}
		if (!Processor) { return; }

		Packer.RegisterAsset(Target.Asset);
		Processors.Add(Processor);
		Targets.Add(Target);
	};

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(WatabouGenerate::SeedsPinLabel);

	if (Settings->SourceMode == EWatabouAssetSource::SeedRef)
	{
		// Children were streamed in by PrepareDataInternal -> resolve only (no sync load). Each key maps
		// to its child via KeyToChildPath (built there); keys with no resolved child stamp nothing.
		TMap<FSoftObjectPath, UWatabouAssetBase*> ChildCache;
		auto ResolveChild = [Context, &ChildCache](const int64 Key) -> UWatabouAssetBase*
		{
			const FSoftObjectPath* ChildPath = Context->KeyToChildPath.Find(Key);
			return ChildPath ? WatabouGenerate::ResolveAssetCached(ChildCache, *ChildPath) : nullptr;
		};

		// Parent hook (forward-scaffolding): resolve the recorded parent path best-effort -- null if the
		// parent has unloaded since PrepareDataInternal. No required consumer yet.
		TMap<FSoftObjectPath, UWatabouAssetBase*> ParentCache;
		auto ResolveParent = [Context, &ParentCache](const int64 Key) -> UWatabouAssetBase*
		{
			const FSoftObjectPath* ParentPath = Context->KeyToParentPath.Find(Key);
			return ParentPath ? WatabouGenerate::ResolveAssetCached(ParentCache, *ParentPath) : nullptr;
		};

		for (const FPCGTaggedData& Input : Inputs)
		{
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Input.Data);
			if (!Points) { continue; }

			const int32 NumPoints = Points->GetNumPoints();
			if (NumPoints <= 0) { continue; }

			const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
			const TFunction<double(int32)> FloorHeightReader = WatabouGenerate::MakeFloorHeightReader(Points, Settings);
			const FWatabouForwardHandler* ForwardHandler = ForwardHandlers.Emplace_GetRef(MakeUnique<FWatabouForwardHandler>(Settings->ForwardSeedAttributes, Points)).Get();

			switch (WatabouPCG::GetFeatureKeyDomain(Points))
			{
			case WatabouPCG::EWatabouKeyDomain::PerPoint:
				// Each point is its own feature -> stamp its child at the point's transform.
				WatabouPCG::ForEachPerPointFeatureKey(Points, [&](const int32 i, const int64 Key)
				{
					UWatabouAssetBase* Child = ResolveChild(Key);
					if (!Child) { return; }

					FWatabouProcessTarget Target;
					Target.Asset = Child;
					Target.Placement = Transforms[i];
					Target.SeedPointIndex = i;
					Target.FloorHeight = FloorHeightReader(i);
					Target.ParentAsset = ResolveParent(Key);
					Target.ParentFeatureKey = Key;
					Target.Forward = ForwardHandler;
					Stamp(Target);
				});
				break;
			case WatabouPCG::EWatabouKeyDomain::Data:
				{
					// Single feature -> all points share one key -> one child, stamped once (centroid).
					// Resolve the child first so the full-cloud centroid walk is skipped when there's none.
					int64 DataKey = 0;
					if (WatabouPCG::TryReadDataFeatureKey(Points, DataKey))
					{
						if (UWatabouAssetBase* Child = ResolveChild(DataKey))
						{
							FWatabouProcessTarget Target;
							Target.Asset = Child;
							// Footprint-aligned generators (Dwellings) fit the encoder's min-area box so the stamp
							// frame matches the plan frame; this also yields the footprint asymmetry cue the
							// processor uses to resolve the long-axis 180deg flip.
							const bool bMinAreaBox = WatabouGenerate::AssetUsesFootprintAlignment(Child);
							TArray<FVector2D> FootprintWorldXY;
							Target.Placement = WatabouGenerate::ComputeDataPlacement(Transforms, NumPoints, Settings->DataOrigin, bMinAreaBox, FootprintWorldXY);
							Target.FootprintWorldXY = MoveTemp(FootprintWorldXY);
							Target.SeedPointIndex = INDEX_NONE;
							Target.FloorHeight = FloorHeightReader(0); // whole-cloud stamp -> representative point 0
							Target.ParentAsset = ResolveParent(DataKey);
							Target.ParentFeatureKey = DataKey;
							Target.Forward = ForwardHandler;
							Stamp(Target);
						}
					}
				}
				break;
			default:
				break; // Not Watabou-keyed -> nothing to follow.
			}
		}
	}
	else
	{
		// Picker / Attribute: assets were streamed in by PrepareDataInternal -> resolve only.
		const bool bAttributeMode = Settings->SourceMode == EWatabouAssetSource::Attribute;
		UWatabouAssetBase* PickerAsset = bAttributeMode ? nullptr : Cast<UWatabouAssetBase>(Settings->Asset.ToSoftObjectPath().ResolveObject());
		TMap<FSoftObjectPath, UWatabouAssetBase*> ResolvedCache;

		for (const FPCGTaggedData& Input : Inputs)
		{
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Input.Data);
			if (!Points) { continue; }

			const int32 NumPoints = Points->GetNumPoints();
			if (NumPoints <= 0) { continue; }

			const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
			const TFunction<double(int32)> FloorHeightReader = WatabouGenerate::MakeFloorHeightReader(Points, Settings);
			const FWatabouForwardHandler* ForwardHandler = ForwardHandlers.Emplace_GetRef(MakeUnique<FWatabouForwardHandler>(Settings->ForwardSeedAttributes, Points)).Get();

			TUniquePtr<const IPCGAttributeAccessor> Accessor;
			TUniquePtr<const IPCGAttributeAccessorKeys> Keys;
			if (bAttributeMode)
			{
				const FPCGAttributePropertyInputSelector Source = Settings->AssetAttribute.CopyAndFixLast(Points);
				Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Points, Source);
				Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Points, Source);
			}
			const bool bUseAttribute = bAttributeMode && Accessor.IsValid() && Keys.IsValid();

			for (int32 i = 0; i < NumPoints; i++)
			{
				UWatabouAssetBase* Asset = nullptr;

				if (bAttributeMode)
				{
					if (!bUseAttribute) { continue; } // attribute missing (warned in PrepareData) -> skip seed

					const FSoftObjectPath Path = WatabouGenerate::ReadAttributePath(*Accessor, *Keys, i);
					if (!Path.IsValid()) { continue; } // unresolved -> skip seed (no picker fallback)

					Asset = WatabouGenerate::ResolveAssetCached(ResolvedCache, Path);
				}
				else
				{
					Asset = PickerAsset;
				}

				if (!Asset) { continue; } // no picked asset / unresolved -> skip seed

				FWatabouProcessTarget Target;
				Target.Asset = Asset;
				Target.Placement = Transforms[i];
				Target.SeedPointIndex = i;
				Target.FloorHeight = FloorHeightReader(i);
				Target.Forward = ForwardHandler;
				Stamp(Target);
			}
		}
	}

	// Phase 0.5 -- precompute per-DISTINCT-asset geometry (anchor + occupancy cells) single-threaded, so
	// the parallel compute never re-walks the asset and targets sharing an asset compute it once. The map
	// outlives the parallel phase and takes no further inserts after this, so each target's pointer into it
	// stays stable; Reserve avoids a rehash mid-loop.
	TMap<const UWatabouAssetBase*, FWatabouAssetGeom> AssetGeomByAsset;
	AssetGeomByAsset.Reserve(Targets.Num());
	for (int32 i = 0; i < Targets.Num(); i++)
	{
		const UWatabouAssetBase* Asset = Targets[i].Asset;
		if (AssetGeomByAsset.Contains(Asset)) { continue; }
		Processors[i]->PrecomputeAssetGeometry(Asset, Params, AssetGeomByAsset.Add(Asset));
	}
	for (FWatabouProcessTarget& Target : Targets) { Target.AssetGeom = AssetGeomByAsset.Find(Target.Asset); }

	// Phase 1 -- parallel compute: each target walks its asset and records its emissions into a per-target
	// buffer (no UObjects, no output writes), so the walks run concurrently. Reads the fully-registered
	// Packer (KeyFor) read-only.
	TArray<FWatabouComputedTarget> Computed;
	Computed.SetNum(Targets.Num());
	ParallelFor(Targets.Num(), [&Targets, &Processors, &Computed, &Params, &Packer](const int32 i)
	{
		Computed[i].Forward = Targets[i].Forward;
		Computed[i].SeedPointIndex = Targets[i].SeedPointIndex;
		FWatabouEmitSink Sink(Params, Packer, Targets[i], Computed[i]);
		Processors[i]->Process(Sink);
	});

	// Phases 2-3 -- allocate (single-threaded) then fill (parallel) the computed geometry.
	WatabouPCG::EmitComputedTargets(Context, Params, Computed);

	// Stage the Feature Map (one entry per unique asset; for Seed Ref this is the CHILD map -> chainable).
	if (!Packer.IsEmpty())
	{
		if (UPCGParamData* Map = Packer.BuildMap(Context))
		{
			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
			Output.Data = Map;
			Output.Pin = WatabouGenerate::FeatureMapPinLabel;
		}
	}

	return true;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
