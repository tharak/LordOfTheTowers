// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "PCGContext.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "UObject/SoftObjectPtr.h"
#include "WatabouFeatureMap.h"
#include "WatabouForward.h"
#include "WatabouPlacementTypes.h"
#include "WatabouGenerate.generated.h"

class UWatabouAssetBase;

/** How each seed's source asset is chosen. */
UENUM(BlueprintType)
enum class EWatabouAssetSource : uint8
{
	Picker    UMETA(DisplayName = "Picker", ToolTip = "Stamp the node's picked asset at every seed."),
	Attribute UMETA(DisplayName = "Attribute", ToolTip = "Resolve a per-seed asset from a soft object path attribute. Seeds that do not resolve are skipped."),
	SeedRef   UMETA(DisplayName = "Seed Ref", ToolTip = "Follow each upstream point's seed ref (under Seed Ref Label) to an imported child asset and stamp the child. Requires the upstream node's Feature Map. Points whose feature has no ref are skipped."),
	ConstructFromAttribute UMETA(Hidden, DisplayName = "Construct From Attribute", ToolTip = "Build a seed ref per seed from point attributes (URL key {value} = {attribute name}). Not yet implemented -- hidden from selection until it is."),
};

/** Where a feature id's geometry is routed. */
UENUM(BlueprintType)
enum class EWatabouFeatureOutput : uint8
{
	Default UMETA(DisplayName = "Default Pin", ToolTip = "Route to the default output pin."),
	Pin     UMETA(DisplayName = "Named Pin", ToolTip = "Route to a named pin (defaults to the feature id)."),
	Skip    UMETA(DisplayName = "Skip", ToolTip = "Drop features with this id."),
};

/**
 * How the single placement is derived for an @Data-keyed seed input (one feature spanning many points,
 * e.g. a footprint path that resolves to one child in Seed Ref mode). Per-point-keyed inputs ignore
 * this -- each point is its own placement.
 */
UENUM(BlueprintType)
enum class EWatabouDataOrigin : uint8
{
	OrientedBox      UMETA(DisplayName = "Oriented Box + First Z", ToolTip = "Location XY + rotation from the points' oriented bounding box (longest axis = X); Z from the first point (basement-safe). Scale from the first point."),
	CentroidXYFirstZ UMETA(DisplayName = "Centroid XY + First Z", ToolTip = "Location XY = centroid of points, Z = first point; rotation + scale from the first point."),
	FirstPoint       UMETA(DisplayName = "First Point", ToolTip = "Location, rotation, and scale all taken from the first point."),
	Centroid         UMETA(DisplayName = "Centroid (all axes)", ToolTip = "Location = centroid of all points (incl. Z); rotation + scale from the first point."),
};

/**
 * Per-feature-id rule: routing + merge behavior for one feature id. Ids without a rule
 * fall through to the default output pin, unmerged. Consolidates what used to be the
 * separate IdToPins / SkipIds / MergeSinglePoints arrays.
 */
USTRUCT(BlueprintType)
struct FWatabouFeatureRule
{
	GENERATED_BODY()

	/** Feature id this rule applies to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName Id = NAME_None;

	/** Where features with this id are routed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EWatabouFeatureOutput Output = EWatabouFeatureOutput::Default;

	/** Target pin when Output is Named Pin. Empty -> use the feature id as the pin name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "Output == EWatabouFeatureOutput::Pin", EditConditionHides))
	FName Pin = NAME_None;

	/**
	 * Merge this id's single-point results into one per-point-keyed cloud. Applies to native Point
	 * features AND features reduced via bReduceToOrientedBox. No effect on un-reduced paths / clouds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bMergePoints = false;

	/**
	 * Reduce this id's areal features (Polygon / LineString) to a single oriented-bounding-box point
	 * (transform = OBB center + longest-axis rotation, bounds = OBB extents). Orthogonal to Merge Points
	 * -- enable both to gather all footprints of this id into one OBB-per-feature cloud. No effect on
	 * Point / MultiPoints features.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bReduceToOrientedBox = false;
};

/**
 * One URL-param binding for the ConstructFromAttribute source mode (scaffold): a Watabou URL
 * query key fed from a per-point attribute, so a seed ref can be assembled live from point data
 * (url key {value} = {attribute name}). Not yet consumed -- forward-scaffolding for that mode.
 */
USTRUCT(BlueprintType)
struct FWatabouUrlParamMapping
{
	GENERATED_BODY()

	/** Watabou URL query key (e.g. "seed", "tags", "size"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FString ParamKey;

	/** Per-point attribute supplying the value for ParamKey. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName AttributeName = NAME_None;
};

/**
 * Load Watabou Data: stamp a Watabou data asset's geometry onto spatial "seed" points.
 *
 * Each input point is a placement target. The asset is resolved per SourceMode -- the
 * picker asset (Picker), a soft object path attribute (Attribute, where seeds that do not
 * resolve are skipped), or, for chaining, each upstream point's seed ref followed to an
 * imported child asset (SeedRef, consuming that node's Feature Map). ConstructFromAttribute
 * is a forward-scaffolded mode (not yet implemented). The generator-specific processor
 * (selected by the asset's generator id, with a generic GeoJSON fallback) emits geometry +
 * a per-feature key; a Feature Map param output lets downstream agnostic nodes load details
 * / follow seed-ref recursion on demand. Per-id routing and merge come from Rules.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class WATABOUPCG_API UWatabouGenerateSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("WatabouGenerate")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/**
	 * How each seed's source asset is chosen. Not PCG_Overridable on purpose: Seed Ref mode adds a
	 * required Feature Map input pin, and pin topology is resolved before per-execution overrides
	 * apply, so an overridden mode could not change the node's pins (it would silently get the wrong
	 * pin set). Like Rules (which drive output pins), this is a structural setting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	EWatabouAssetSource SourceMode = EWatabouAssetSource::Picker;

	/** Asset stamped at every seed (SourceMode = Picker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable, EditCondition = "SourceMode == EWatabouAssetSource::Picker", EditConditionHides))
	TSoftObjectPtr<UWatabouAssetBase> Asset;

	/** Per-seed attribute holding the asset to load -- soft object path or string (SourceMode = Attribute). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable, EditCondition = "SourceMode == EWatabouAssetSource::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector AssetAttribute;

	/**
	 * Feature-details label whose seed ref to follow on each upstream point's source feature
	 * (SourceMode = Seed Ref). Generator-specific (e.g. "town", "danger", "dwellings"). Points whose
	 * resolved feature has no valid seed ref under this label are skipped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable, EditCondition = "SourceMode == EWatabouAssetSource::SeedRef", EditConditionHides))
	FName SeedRefLabel = NAME_None;

	/**
	 * How to derive the single placement for an @Data-keyed seed input (one feature -> one child, e.g. a
	 * dwelling stamped on its footprint path). Per-point-keyed inputs ignore this. Seed Ref mode only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable, EditCondition = "SourceMode == EWatabouAssetSource::SeedRef", EditConditionHides))
	EWatabouDataOrigin DataOrigin = EWatabouDataOrigin::OrientedBox;

	/** URL key -> attribute bindings used to assemble a seed ref per seed (SourceMode = Construct From Attribute). Scaffold: not yet implemented. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "SourceMode == EWatabouAssetSource::ConstructFromAttribute", EditConditionHides))
	TArray<FWatabouUrlParamMapping> UrlParamMappings;

	/** Per-feature-id routing / merge rules. Ids without a rule use the default output pin, unmerged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FWatabouFeatureRule> Rules;

	/** Extra transform applied to all emitted geometry, composed under each seed's transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	FTransform Transform;

	/** Uniform scale folded into Transform before placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	double ScaleFactor = 1.0;

	/**
	 * Which point of the stamped asset's footprint lands on the placement (applies to every stamp).
	 * Bounds Center keeps the asset centered on its target; Origin is the legacy (0,0) corner behavior.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	EWatabouAssetAnchor AssetAnchor = EWatabouAssetAnchor::BoundsCenter;

	/**
	 * Stack floors along the placement's up axis: a feature's Z becomes floor level x Floor Height.
	 * Only generators that carry floor levels (e.g. Dwellings) participate; single-floor generators
	 * ignore it. Defaults on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Transform Semantics", meta = (PCG_Overridable))
	bool bApplyFloorHeight = true;

	/** Per-floor height in NATIVE units (scaled by Scale Factor). 1 => one floor is one cell tall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Transform Semantics", meta = (PCG_Overridable, EditCondition = "bApplyFloorHeight", ClampMin = "0"))
	double FloorHeight = 1.0;

	/** Read Floor Height from a per-seed attribute instead of the constant (falls back to the constant where missing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Transform Semantics", meta = (PCG_Overridable, EditCondition = "bApplyFloorHeight"))
	bool bFloorHeightFromAttribute = false;

	/** Per-seed attribute supplying Floor Height (read at the seed point). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Transform Semantics", meta = (PCG_Overridable, EditCondition = "bApplyFloorHeight && bFloorHeightFromAttribute", EditConditionHides))
	FPCGAttributePropertyInputSelector FloorHeightAttribute;

	/** Rotate dir-bearing features (doors / windows / stairs / exit) to face their direction. Defaults on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Transform Semantics", meta = (PCG_Overridable))
	bool bApplyFacing = true;

	/**
	 * Rotate the whole stamp 90 degrees when the asset's long side disagrees with the placement's major
	 * axis (the placement's local +X is the footprint's longest extent). Near-square footprints are left
	 * as-is. Only generators with a fitted footprint participate. Defaults on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Transform Semantics", meta = (PCG_Overridable))
	bool bApplyMajorAxisAlign = true;

	/** Tag added to path-like data (open / closed paths). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Tagging")
	FString PathlikeTag = TEXT("path");

	/**
	 * Forward attributes from each seed onto the stamped geometry's @Data domain. Per-point seeds forward
	 * the seed point's attributes; @Data-keyed seeds (one footprint -> one stamp) forward only the seed's
	 * @Data-domain attributes. WatabouKey / IsClosed / IsHole are never forwarded.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Forwarding", meta = (PCG_Overridable))
	FWatabouForwardDetails ForwardSeedAttributes;

#if WITH_EDITOR
	/** Populate Rules from the picker asset's processor labels (existing rules are kept). */
	UFUNCTION(CallInEditor, Category = "Settings", meta = (DisplayName = "Populate Rules"))
	void EDITOR_PopulateRules();
#endif
};

/**
 * Custom context: mixes in IPCGAsyncLoadingContext so the seed assets stream in
 * asynchronously -- the element pauses during load and resumes once they're resident,
 * instead of blocking the game thread on a synchronous load.
 */
struct FWatabouGenerateContext : public FPCGContext, public IPCGAsyncLoadingContext
{
	/** Seed Ref mode: the upstream Feature Map (parent assets), unpacked + resolved in PrepareData. */
	WatabouPCG::FWatabouFeatureMapUnpacker Unpacker;

	/**
	 * Seed Ref mode: distinct upstream feature key -> resolved child asset path. Built in
	 * PrepareDataInternal (parents are loaded + their seed refs read there, synchronously, before any
	 * GC can run), then consumed in ExecuteInternal once the children have streamed in.
	 */
	TMap<int64, FSoftObjectPath> KeyToChildPath;

	/**
	 * Seed Ref mode: upstream feature key -> the parent asset path that produced it. Recorded in
	 * PrepareDataInternal (parents are resident there); resolved best-effort in ExecuteInternal to fill
	 * the processor's parent hook (FWatabouProcessTarget::ParentAsset). May resolve to null if the
	 * parent has since unloaded -- the hook is forward-scaffolding with no required consumer yet.
	 */
	TMap<int64, FSoftObjectPath> KeyToParentPath;
};

class FWatabouGenerateElement : public IPCGElementWithCustomContext<FWatabouGenerateContext>
{
public:
	// The async-load request goes through the game-thread-only streamable manager, so the
	// load-issuing phase (PrepareData) is pinned to the game thread; emission (Execute) runs
	// off-thread. The load stays non-blocking -- the element pauses while assets stream in.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return Context && Context->CurrentPhase == EPCGExecutionPhase::PrepareData;
	}

protected:
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
