// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "WatabouPlacementTypes.h"

class UWatabouAssetBase;
class UWatabouFeaturesCollection;
class UPCGData;
class UPCGBasePointData;
class FWatabouForwardHandler;
struct FPCGContext;
namespace WatabouPCG { class FWatabouFeatureMapPacker; }

/**
 * Which generic transform bakes a concrete processor participates in. ANDed with the node's per-bake
 * toggles (FWatabouProcessParams) so a bake fires only when BOTH the user enabled it AND the
 * generator's model supports it. The base processor returns None (pure projection), which keeps the
 * GeoJSON identity instance byte-identical to the legacy passthrough; generators opt in.
 */
enum class EWatabouBake : uint8
{
	None           = 0,
	FloorHeight    = 1 << 0,
	Facing         = 1 << 1,
	MajorAxisAlign = 1 << 2,
};
ENUM_CLASS_FLAGS(EWatabouBake)

/**
 * Per-asset geometry computed ONCE (single-threaded) and shared by every target that stamps the same
 * asset: the resolved asset anchor and the projected occupancy-cell centers for best-fit overlap. Both
 * depend only on the asset + node config (never the per-target placement), so the element precomputes one
 * per distinct asset before the parallel compute phase instead of re-walking the asset per target.
 * OccupancyProjected is in projected-local space (NOT anchor-subtracted; the fit subtracts the anchor).
 */
struct FWatabouAssetGeom
{
	FVector2D Anchor = FVector2D::ZeroVector;
	TArray<FVector2D> OccupancyProjected;
	bool bValid = false;
};

/**
 * One placement instance: a resolved asset to emit at a spatial target.
 *
 * The Load node turns each input "seed" point into a target -- the asset resolved
 * for that point (via picker or per-point attribute) plus the point's transform.
 * SeedPointIndex is the source input-point index, available for diagnostics.
 */
struct FWatabouProcessTarget
{
	const UWatabouAssetBase* Asset = nullptr;
	FTransform Placement = FTransform::Identity;
	int32 SeedPointIndex = INDEX_NONE;

	/**
	 * The seed footprint outline in WORLD XY (Seed Ref @Data stamps with a footprint-aligned generator;
	 * empty otherwise). The processor fits the stamp to it via best-fit overlap -- trying the min-area
	 * box's 4 quadrant orientations and keeping the one that lands the most occupancy cells inside this
	 * polygon -- which resolves both the near-square 90deg (axis choice) and the 180deg (sign) by content.
	 */
	TArray<FVector2D> FootprintWorldXY;

	/**
	 * Per-floor height for the floor-z bake, resolved per seed by the element (attribute > constant).
	 * NATIVE units: folded into the local coordinate before LocalTransform, so it scales with the
	 * node's ScaleFactor and stacks along the placement's up axis. Ignored unless the asset carries
	 * floor levels and the bake is enabled.
	 */
	double FloorHeight = 1.0;

	/**
	 * Forward-scaffolding for child processors: the parent asset whose feature produced this seed
	 * (Seed Ref mode only; null in Picker / Attribute). BEST-EFFORT -- the parent may have been
	 * unloaded after the children streamed in, so this can be null even when ParentFeatureKey is set.
	 * A future consumer needing the parent resident must pin it in PrepareData.
	 */
	const UWatabouAssetBase* ParentAsset = nullptr;

	/** Upstream feature key that resolved to this target's asset (Seed Ref mode); 0 otherwise. */
	int64 ParentFeatureKey = 0;

	/**
	 * Seed -> target attribute forwarding for this stamp (null when disabled). Owned by the element
	 * (one per seed input); the sink applies it to every emitted data's @Data domain. SeedPointIndex
	 * selects the mode: a valid index forwards per-point seed attrs; INDEX_NONE (an @Data-keyed stamp)
	 * forwards only @Data-domain seed attrs.
	 */
	const FWatabouForwardHandler* Forward = nullptr;

	/**
	 * Per-asset geometry (anchor + occupancy cells) precomputed once by the element and shared across
	 * every target that stamps this asset. Null -> Process() computes it inline as a fallback.
	 */
	const FWatabouAssetGeom* AssetGeom = nullptr;
};

/**
 * Emit-pass knobs shared across every target in one node execution. Owned by the
 * element and passed by const-ref to each per-target sink.
 *
 * A processor is shape + mapping ONLY: it emits geometry plus a per-feature key.
 * It writes NO detail attributes -- the "hard data" (details, seed-ref recursion)
 * is pulled on demand from the Feature Map by separate, generator-agnostic nodes.
 * So there is no DataScaleFactor here; numeric scaling lives on the property loader.
 *
 * Geometry placement composes as: world = Target.Placement o LocalTransform o (coord.X, coord.Y, 0),
 * where LocalTransform folds the node's Transform and uniform ScaleFactor.
 */
struct FWatabouProcessParams
{
	/** Node transform with ScaleFactor folded in; applied to raw 2D coordinates before placement. */
	FTransform LocalTransform = FTransform::Identity;

	/** Semantic label -> output pin. Labels absent from the map fall through to DefaultPin. */
	TMap<FName, FName> LabelToPin;

	/** Feature ids to skip entirely. */
	TSet<FName> SkipIds;

	/**
	 * Single-point feature ids to merge into one point cloud per id (per-point keys,
	 * Elements domain). Ids NOT listed here stay as standalone single-point data sets
	 * (one data, @Data key). Mirrors the old node's pointify-by-id control. Single-point results
	 * include native Point features AND OBB-reduced features (see ReduceToOrientedBoxIds).
	 */
	TSet<FName> MergeSinglePoints;

	/**
	 * Feature ids whose areal features (Polygon / LineString) reduce to a single oriented-bounding-box
	 * point: transform = OBB center + longest-axis yaw, bounds = OBB half-extents. Orthogonal to
	 * MergeSinglePoints -- combine both to gather an id's footprint OBBs into one cloud. No effect on
	 * Point / MultiPoints features.
	 */
	TSet<FName> ReduceToOrientedBoxIds;

	/** Tag added to path-like data (open/closed paths) as a downstream failsafe. */
	FString PathlikeTag = TEXT("path");

	/** Catch-all pin for labels not present in LabelToPin. */
	FName DefaultPin = NAME_None;

	/**
	 * Opt-in transform bakes (node toggles), applied at point-generation time and folded into the
	 * emitted transforms. Each is ANDed with the processor's SupportedBakes(), so e.g. the major-axis
	 * align never touches the city-family GeoJSON passthrough, and floor-z is a no-op on single-floor
	 * generators.
	 */
	bool bApplyFloorHeight = true;
	bool bApplyFacing = true;
	bool bApplyMajorAxisAlign = true;

	/**
	 * Which point of the asset's projected footprint lands on the placement (every stamp). Bounds Center
	 * centers the asset; Origin is the legacy (0,0) corner-ish behavior. Computed per target by the
	 * processor (it owns the projection).
	 */
	EWatabouAssetAnchor AssetAnchor = EWatabouAssetAnchor::BoundsCenter;
};

/**
 * One fully computed emission == one future point data set, produced during the parallel COMPUTE phase
 * (no UObjects allocated yet). Either a single feature (one @Data key, optionally a path) or a
 * per-point-keyed cloud (Elements-domain keys). The element allocates + fills it afterwards.
 */
struct FWatabouComputedEmission
{
	FName Pin = NAME_None;
	FName Label = NAME_None;
	TArray<FTransform> Transforms;
	TArray<FVector> Extents;        // per-point local half-extents (bounds); one per transform
	TArray<int64> Keys;             // per-point feature keys (cloud only)
	int64 DataKey = 0;              // @Data feature key (single-feature emission only)
	bool bPerPointKeys = false;     // true -> cloud (Keys); false -> single @Data key (DataKey)
	bool bIsPath = false;           // pathlike tag + @Data IsClosed
	bool bClosed = false;
	TArray<FName> Tags;             // extra data-level tags (e.g. "rotunda"), added alongside the label tag
};

/**
 * One target's full computed output: every emission it produced plus the seed-forwarding context the
 * fill phase needs. Filled by one processor in the parallel compute phase (isolated per target), then
 * consumed by the single-threaded allocate + parallel fill phases (WatabouPCG::EmitComputedTargets).
 */
struct FWatabouComputedTarget
{
	TArray<FWatabouComputedEmission> Emissions;
	const FWatabouForwardHandler* Forward = nullptr;
	int32 SeedPointIndex = INDEX_NONE;
};

/**
 * Compute recorder handed to a processor during the parallel COMPUTE phase. It owns no UObjects and
 * never touches FPCGContext::OutputData: the processor walks its target asset and RECORDS fully
 * computed emissions (transforms / bounds / keys) into the per-target FWatabouComputedTarget. The
 * element then batch-allocates (single-threaded) and fills (parallel) from those records. One recorder
 * per target; pure compute over read-only inputs, so many recorders run concurrently.
 */
class WATABOUPCG_API FWatabouEmitSink
{
public:
	FWatabouEmitSink(
		const FWatabouProcessParams& InParams,
		const WatabouPCG::FWatabouFeatureMapPacker& InPacker,
		const FWatabouProcessTarget& InTarget,
		FWatabouComputedTarget& InOut);

	const UWatabouAssetBase* GetAsset() const { return Target.Asset; }
	const FTransform& GetPlacement() const { return Target.Placement; }
	const FWatabouProcessParams& GetParams() const { return Params; }

	/** Per-seed floor height for the floor-z bake (native units). */
	double GetFloorHeight() const { return Target.FloorHeight; }

	/** The seed footprint outline (world XY) for best-fit overlap (see FWatabouProcessTarget::FootprintWorldXY). */
	const TArray<FVector2D>& GetFootprintWorldXY() const { return Target.FootprintWorldXY; }

	/** Per-asset precomputed geometry (anchor + occupancy), or null -> Process() computes it inline. */
	const FWatabouAssetGeom* GetAssetGeom() const { return Target.AssetGeom; }

	/** Parent asset (Seed Ref mode, best-effort; may be null) -- see FWatabouProcessTarget. */
	const UWatabouAssetBase* GetParentAsset() const { return Target.ParentAsset; }

	/** Upstream feature key that produced this target (Seed Ref mode; 0 otherwise). */
	int64 GetParentFeatureKey() const { return Target.ParentFeatureKey; }

	/** Stable feature key for a feature in this target's asset (Feature Map back-reference). */
	int64 KeyFor(const UWatabouFeaturesCollection* Collection, int32 ElementIndex) const;

	/** Record a single-feature emission (one @Data key, optionally a path). Pin resolved from Label; payload arrays moved in. ExtraTags are added to the data alongside the label tag. */
	void RecordFeature(FName Label, TArray<FTransform>&& Transforms, TArray<FVector>&& Extents, int64 Key, bool bAsPath, bool bClosed, TArray<FName> ExtraTags = {});

	/** Record a per-point-keyed cloud (Elements-domain keys). Pin resolved from Label; payload arrays moved in. */
	void RecordCloud(FName Label, TArray<FTransform>&& Transforms, TArray<FVector>&& Extents, TArray<int64>&& Keys);

	/**
	 * Accumulate one precomputed point under Label for this target's consolidated output. The
	 * consolidating-generator path (Dwellings) gathers every feature of an id here during the walk;
	 * FlushConsolidated then records one cloud per id -- per TARGET, so each cloud has a single source
	 * seed and the seed-attribute forwarding still applies coherently in the fill phase.
	 */
	void AddConsolidated(FName Label, const FTransform& Transform, const FVector& Extents, int64 Key);

	/** Record one cloud per accumulated id, then clear. Call once at end of Process. */
	void FlushConsolidated();

private:
	FName ResolvePin(FName Label) const;

	/** One target's consolidated cloud for an id: parallel arrays of point transform / local half-extents / feature key. */
	struct FConsolidatedCloud
	{
		TArray<FTransform> Transforms;
		TArray<FVector> Extents;
		TArray<int64> Keys;
	};

	/** Per-target accumulation: id -> its consolidated cloud, filled during the walk, flushed once at end of Process. */
	TMap<FName, FConsolidatedCloud> ConsolidatedByLabel;

	const FWatabouProcessParams& Params;
	const WatabouPCG::FWatabouFeatureMapPacker& Packer;
	const FWatabouProcessTarget& Target;
	FWatabouComputedTarget& Out;
};

/**
 * Per-generator consumption strategy: turns a UWatabouAssetBase's feature tree into PCG
 * point / path geometry plus per-feature keys. Writes no details (those are loaded on
 * demand against the Feature Map).
 *
 * Produced lazily by FWatabouGeneratorInfo::CreateProcessor(); the Load node falls back to
 * FWatabouGeoJSONProcessor when a generator has none. Concrete processors own their
 * coordinate projection and geometry reconstruction (hex / cell / rect), which is why one
 * emitter cannot serve every generator.
 */
class WATABOUPCG_API IWatabouProcessor
{
public:
	virtual ~IWatabouProcessor() = default;

	/**
	 * Semantic output labels this processor can emit for the given asset. Drives the node's
	 * "populate label pins" editor action; runtime pins come from the user-configured mapping.
	 */
	virtual void GetOutputLabels(const UWatabouAssetBase* Asset, TArray<FName>& OutLabels) const = 0;

	/** Walk the sink's target asset and emit tagged geometry + feature keys through Sink. */
	virtual void Process(FWatabouEmitSink& Sink) const = 0;

	/**
	 * True if this generator orients its content to the seed footprint -- its @Data stamp placement is
	 * fitted with the footprint's min-area box (matching the import-time encoder frame) and is subject to
	 * the major-axis align + 180deg disambiguation. False (default) keeps the generic oriented-box
	 * placement, so the city-family GeoJSON passthrough is unaffected; cell-model generators (Dwellings)
	 * return true.
	 */
	virtual bool UsesFootprintAlignment() const { return false; }

	/**
	 * Precompute the per-asset geometry (anchor + occupancy cells) the placement bakes need, into Out.
	 * Called once per DISTINCT asset by the element -- single-threaded, before the parallel compute --
	 * so targets that share an asset don't each re-walk it. Default: no-op (Out stays invalid, and
	 * Process() falls back to computing it inline).
	 */
	virtual void PrecomputeAssetGeometry(const UWatabouAssetBase* /*Asset*/, const FWatabouProcessParams& /*Params*/, FWatabouAssetGeom& /*Out*/) const {}
};

/**
 * Reusable processor skeleton shared by every generator: the generic depth-first walk + per-type
 * emission + single-point merge, plus the three opt-in transform bakes (floor-z, facing yaw, and the
 * per-target major-axis 90deg alignment). Concrete generators override ONLY the coordinate
 * projection; the semantics machinery is generic.
 *
 * Bakes are computed at point-generation time and folded into the emitted transforms -- the sink
 * stays pure (no detail attributes; those are still loaded on demand against the Feature Map). Each
 * bake is gated by (node toggle AND SupportedBakes()), so the identity instance
 * (FWatabouGeoJSONProcessor) applies none of them (no floor-z / facing / alignment). It still gets the
 * generic placement semantics every stamp shares -- the asset anchor (default Bounds Center) and unit
 * point bounds -- so it is NOT byte-for-byte the old PCGExLoadWatabouData emission; that legacy path was
 * only ever a reference to kickstart the rewrite.
 *
 * The projection hooks map a generator's NATIVE 2D space to a local 2D space; the base then folds in
 * Z (floor level x FloorHeight), the per-feature facing rotation, and the per-target alignment before
 * composing world = Placement o LocalTransform o local. ProjectPoint and OrientDir MUST share the
 * same handedness flip so positions and facings stay consistent.
 */
class WATABOUPCG_API FWatabouProcessorBase : public IWatabouProcessor
{
public:
	virtual void GetOutputLabels(const UWatabouAssetBase* Asset, TArray<FName>& OutLabels) const override;
	virtual void Process(FWatabouEmitSink& Sink) const override;

	/** Footprint-aligned iff this generator participates in the MajorAxisAlign bake. */
	virtual bool UsesFootprintAlignment() const override;

	/** One combined walk over the asset: resolves the anchor (per AssetAnchor mode) and collects the occupancy cells. */
	virtual void PrecomputeAssetGeometry(const UWatabouAssetBase* Asset, const FWatabouProcessParams& Params, FWatabouAssetGeom& Out) const override;

protected:
	/** Map one native 2D coordinate to local 2D (handedness flip / cell-centering / ...). Identity by default. */
	virtual FVector2D ProjectPoint(const FVector2D& Native) const { return Native; }

	/**
	 * Map one native GRID VERTEX (edge endpoint) to local 2D. Unlike ProjectPoint, this carries NO cell
	 * centering -- a vertex already sits at a cell corner. Cell-model generators that emit edge endpoints
	 * (Dwellings) override this to drop the +0.5 centering ProjectPoint applies, so an edge midpoint lands on
	 * the wall between cells (consistent with the room cell centers). Default routes through ProjectPoint,
	 * which is correct for identity-projection generators (they have no centering to undo).
	 */
	virtual FVector2D ProjectEdgePoint(const FVector2D& Vertex) const { return ProjectPoint(Vertex); }

	/** Map one native 2D direction to local 2D (the linear/flip part of ProjectPoint, no offset). Identity by default. */
	virtual FVector2D OrientDir(const FVector2D& NativeDir) const { return NativeDir; }

	/** Which bakes this generator participates in. None by default (pure projection). */
	virtual EWatabouBake SupportedBakes() const { return EWatabouBake::None; }

	/**
	 * Local-space positional offset applied to a dir-bearing feature (added to its projected coordinate
	 * before the anchor shift). Zero by default -- the point sits exactly at its projected position.
	 * Cell-model generators override this so edge features (doors / windows) land on the cell boundary
	 * rather than its center. MUST share ProjectPoint's handedness flip so the offset and facing agree.
	 */
	virtual FVector2D EdgeOffsetLocal(const FVector2D& NativeDir) const { return FVector2D::ZeroVector; }

	/** True if this generator offsets dir-bearing features onto cell edges (so the dir is read even when facing yaw is disabled). False by default. */
	virtual bool UsesEdgeOffset() const { return false; }

	/**
	 * True if this generator collapses its features into consolidated point clouds (one cloud per id)
	 * instead of one data set per feature. False by default -- the GeoJSON / city family keeps its
	 * one-data-per-feature layout unchanged. Cell-model generators (Dwellings) opt in to fold their
	 * hundreds of per-element single points (doors / windows / cells) into a handful of per-id clouds.
	 */
	virtual bool ConsolidatesById() const { return false; }

	/**
	 * Bucket label a feature's id consolidates under. The consolidated cloud is keyed by this bucket, not the
	 * raw id, so a generator can fold several ids into one cloud (e.g. Dwellings -> "tiles" / "features") while
	 * each point keeps its own feature key for downstream load-by-key. Identity by default (one cloud per id).
	 */
	virtual FName ConsolidationBucket(FName FeatureId) const { return FeatureId; }

	/**
	 * Feature id whose cells define this generator's OCCUPANCY for best-fit overlap (e.g. Dwellings
	 * "rooms"). NAME_None by default -> the generator does not fit to the footprint (the city family keeps
	 * the generic oriented-box placement + aspect align). Only consulted when UsesFootprintAlignment().
	 */
	virtual FName FootprintFitOccupancyId() const { return NAME_None; }

	/**
	 * Feature ids excluded from emission BY DEFAULT (merged with the node's SkipIds) and hidden from
	 * GetOutputLabels -- for metadata-only features kept in the asset but not stamped (e.g. OPD "notes").
	 */
	virtual void GetDefaultSkipIds(TSet<FName>& OutIds) const {}

	/**
	 * Extra data-level tags for one feature's emission, added alongside the label tag (e.g. OPD tags rotunda
	 * outlines "rotunda"). Collected only for SINGLE-feature emissions via EmitGeometry -- a consolidated
	 * cloud mixes features, so per-feature tags there would be ambiguous.
	 */
	virtual void GetFeatureTags(const UWatabouFeaturesCollection* /*Collection*/, int32 /*ElementIndex*/, TArray<FName>& /*OutTags*/) const {}

private:
	/** Per-target emit constants: effective placement (alignment folded in) + resolved bake flags. */
	struct FEmitContext
	{
		FTransform LocalTransform = FTransform::Identity;
		FTransform Placement = FTransform::Identity;
		double FloorHeight = 1.0;
		bool bFloorZ = false;
		bool bFacing = false;

		/** Reflect the asset about its local X (best-fit chose a mirror, not just a rotation -- see ComputeBestFitPlacement). */
		bool bMirrorX = false;

		/** Asset anchor in projected-local XY, subtracted from every projected position so the chosen point sits at local origin. */
		FVector2D Anchor = FVector2D::ZeroVector;

		/** Effective skip set (node SkipIds + the processor's GetDefaultSkipIds). Points at a Process-local; valid for the whole walk. */
		const TSet<FName>* SkipIds = nullptr;
	};

	/** Semantics inherited down the collection tree, overlaid from each collection's Details. */
	struct FInherited
	{
		double Level = 0.0;
		bool bHasLevel = false;
	};

	/**
	 * One precomputed single-point emission: a world transform, local bounds half-extents, and feature
	 * key. Unifies native merged Points (extents = unit 0.5) and OBB-reduced features (extents = OBB),
	 * so both flow through the same merge-into-cloud / standalone paths.
	 */
	struct FMergedPoint
	{
		FTransform Transform = FTransform::Identity;
		FVector Extents = FVector(0.5);
		int64 Key = 0;
	};

	void ProcessCollection(const UWatabouFeaturesCollection* Collection, FWatabouEmitSink& Sink, const FEmitContext& Ctx, FInherited Inherited) const;
	void EmitGeometry(const UWatabouFeaturesCollection* Collection, int32 ElementIndex, FWatabouEmitSink& Sink, const FEmitContext& Ctx, const FInherited& Inherited, bool bAsPath, bool bClosed) const;

	/**
	 * Route one feature into the per-target consolidator by id: Point -> one edge-offset point;
	 * MultiPoints -> one point per cell (all sharing the feature key); areal -> standalone path.
	 * Used by generators that ConsolidatesById() (Dwellings); collapses the per-feature data explosion.
	 */
	void ConsolidateFeature(const UWatabouFeaturesCollection* Collection, int32 ElementIndex, FWatabouEmitSink& Sink, const FEmitContext& Ctx, const FInherited& Inherited) const;

	/** Stage one precomputed point as a standalone single-feature data set (@Data key, custom bounds). */
	void EmitSinglePoint(FName Id, const FMergedPoint& Point, FWatabouEmitSink& Sink) const;

	/** Stage a cloud of precomputed points under one id (per-point keys, per-point bounds). */
	void EmitMergedPoints(FName Id, const TArray<FMergedPoint>& Points, FWatabouEmitSink& Sink) const;

	/** Reduce a Polygon / LineString feature to an oriented-bounding-box point (in local-projected space). */
	FMergedPoint MakeOrientedBoxPoint(const UWatabouFeaturesCollection* Collection, int32 ElementIndex, const FEmitContext& Ctx, const FInherited& Inherited, const FWatabouEmitSink& Sink) const;

	/** Build one feature point's world transform: ProjectPoint -> (x, y, level*FloorHeight) -> optional facing yaw, then o LocalTransform o Placement. */
	FTransform MakePointTransform(const FVector2D& NativeCoord, const FEmitContext& Ctx, const FInherited& Inherited, const FVector2D* FacingDir) const;

	/**
	 * Endpoint-precise transform for an edge feature (door / window): position = the wall MIDPOINT of the two
	 * native vertices (Va, Vb); facing = OUTWARD, from the interior cell toward the wall. Replaces the legacy
	 * cell + half-cell-dir path for openings that carry endpoints -- robust to the upper-floor winding flips
	 * that mislead the exported dir. InteriorCellNative is the opening's attached cell (edge2cell = the room
	 * cell, per floor), used to orient the facing.
	 */
	FTransform MakeEdgeTransform(const FVector2D& Va, const FVector2D& Vb, const FVector2D& InteriorCellNative, const FEmitContext& Ctx, const FInherited& Inherited) const;

	/**
	 * Outward facing (projected-local unit vector) for an edge: the wall midpoint minus the interior room
	 * cell center. The bundle resolves each contour opening's room via getRoom(edge2cell), so the attached
	 * cell is the interior side -- this points away from it, per floor, with no dependence on the exported dir.
	 */
	FVector2D ResolveEdgeOutwardLocal(const FVector2D& Va, const FVector2D& Vb, const FVector2D& InteriorCellNative) const;

	/**
	 * Effective placement with footprint orientation folded in (when enabled): best-fit overlap against the
	 * seed footprint for generators that define an occupancy id, else the major-axis 90deg aspect align.
	 * Anchor is the asset's projected-local anchor (the same one the stamp subtracts), needed to place the
	 * occupancy cells for the overlap test. bOutMirrorX reports whether the fit chose a reflection.
	 */
	FTransform ComputeAlignedPlacement(const FWatabouEmitSink& Sink, bool bAlignEnabled, const FWatabouAssetGeom& Geom, bool& bOutMirrorX) const;

	/**
	 * Pick, among the placement's 8 orientations (4 quadrant rotations x optional local-X mirror), the one
	 * that lands the most FootprintFitOccupancyId cells inside the seed footprint. The mirror is needed
	 * because generators differ in plan chirality (e.g. Neighbourhood samples mirror-handed vs City), which
	 * a rotation alone cannot undo. Ties prefer no-mirror then the lowest rotation, so chiral-correct stamps
	 * (City) never get spuriously reflected and symmetric footprints stay put. Sets bOutMirrorX accordingly.
	 */
	FTransform ComputeBestFitPlacement(const FWatabouEmitSink& Sink, const FTransform& BasePlacement, const FWatabouAssetGeom& Geom, bool& bOutMirrorX) const;

	/**
	 * One recursive walk over the asset: accumulate the full-footprint projected-coord bounds + sum/count
	 * (for the anchor) AND collect the OccupancyId cells (for best-fit overlap). Replaces the separate
	 * anchor / occupancy walks, so PrecomputeAssetGeometry passes over the asset once.
	 */
	void GatherAssetGeometry(const UWatabouFeaturesCollection* Collection, FName OccupancyId, FBox2D& OutBounds, FVector2D& OutSum, int32& OutCount, TArray<FVector2D>& OutOccupancy) const;

	/** Read a feature's facing into a native-space direction; false when the feature has no dir. */
	static bool ResolveFacing(const UWatabouFeaturesCollection* Collection, int32 ElementIndex, FVector2D& OutDir);

	/**
	 * Read an edge feature's two endpoint vertices (va / vb, native grid coords) from its details. False when
	 * absent -- stairs / exit, or an unpatched / pre-endpoint bundle -- so the caller uses the legacy path.
	 */
	static bool ResolveEdgeEndpoints(const UWatabouFeaturesCollection* Collection, int32 ElementIndex, FVector2D& OutVa, FVector2D& OutVb);
};

namespace WatabouPCG
{
	/**
	 * Allocate, stage, and fill the computed targets produced by the parallel compute phase.
	 *   Phase 2 (single-threaded): NewObject each emission's point data + append it to the context output.
	 *   Phase 3 (parallel):        size + fill transforms / bounds / feature keys / path flags, then apply
	 *                              seed-attribute forwarding.
	 * NewObject and the output append are the only serial steps; the per-object fills run concurrently
	 * (each touches only its own data, reading the shared, const seed metadata for forwarding).
	 */
	WATABOUPCG_API void EmitComputedTargets(FPCGContext* Context, const FWatabouProcessParams& Params, const TArray<FWatabouComputedTarget>& Targets);
}
