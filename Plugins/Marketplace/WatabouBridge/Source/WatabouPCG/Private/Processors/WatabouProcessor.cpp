// Copyright 2026 Timothé Lapetite

#include "Processors/WatabouProcessor.h"

#include "WatabouAssetBase.h"
#include "WatabouFacing.h"
#include "WatabouForward.h"
#include "WatabouFeatureMap.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouFeatureTypes.h"
#include "WatabouOrientedBox.h"
#include "WatabouMinAreaBox2D.h"
#include "WatabouPCGData.h"

#include "PCGContext.h"
#include "PCGData.h"
#include "Data/PCGBasePointData.h"
#include "Math/Box2D.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Async/ParallelFor.h"

#pragma region FWatabouEmitSink

FWatabouEmitSink::FWatabouEmitSink(
	const FWatabouProcessParams& InParams,
	const WatabouPCG::FWatabouFeatureMapPacker& InPacker,
	const FWatabouProcessTarget& InTarget,
	FWatabouComputedTarget& InOut)
	: Params(InParams)
	, Packer(InPacker)
	, Target(InTarget)
	, Out(InOut)
{
}

int64 FWatabouEmitSink::KeyFor(const UWatabouFeaturesCollection* Collection, const int32 ElementIndex) const
{
	return Packer.KeyFor(Target.Asset, Collection, ElementIndex);
}

void FWatabouEmitSink::RecordFeature(const FName Label, TArray<FTransform>&& Transforms, TArray<FVector>&& Extents, const int64 Key, const bool bAsPath, const bool bClosed, TArray<FName> ExtraTags)
{
	if (Transforms.IsEmpty()) { return; }

	FWatabouComputedEmission& Em = Out.Emissions.Emplace_GetRef();
	Em.Pin = ResolvePin(Label);
	Em.Label = Label;
	Em.Transforms = MoveTemp(Transforms);
	Em.Extents = MoveTemp(Extents);
	Em.DataKey = Key;
	Em.bPerPointKeys = false;
	Em.bIsPath = bAsPath;
	Em.bClosed = bClosed;
	Em.Tags = MoveTemp(ExtraTags);
}

void FWatabouEmitSink::RecordCloud(const FName Label, TArray<FTransform>&& Transforms, TArray<FVector>&& Extents, TArray<int64>&& Keys)
{
	if (Transforms.IsEmpty()) { return; }

	FWatabouComputedEmission& Em = Out.Emissions.Emplace_GetRef();
	Em.Pin = ResolvePin(Label);
	Em.Label = Label;
	Em.Transforms = MoveTemp(Transforms);
	Em.Extents = MoveTemp(Extents);
	Em.Keys = MoveTemp(Keys);
	Em.bPerPointKeys = true;
}

void FWatabouEmitSink::AddConsolidated(const FName Label, const FTransform& Transform, const FVector& Extents, const int64 Key)
{
	FConsolidatedCloud& Cloud = ConsolidatedByLabel.FindOrAdd(Label);
	Cloud.Transforms.Add(Transform);
	Cloud.Extents.Add(Extents);
	Cloud.Keys.Add(Key);
}

void FWatabouEmitSink::FlushConsolidated()
{
	for (TPair<FName, FConsolidatedCloud>& Pair : ConsolidatedByLabel)
	{
		FConsolidatedCloud& Cloud = Pair.Value;
		if (Cloud.Transforms.IsEmpty()) { continue; }

		// One cloud per id, per-point feature keys; the fill phase applies any seed-attribute forwarding.
		RecordCloud(Pair.Key, MoveTemp(Cloud.Transforms), MoveTemp(Cloud.Extents), MoveTemp(Cloud.Keys));
	}
	ConsolidatedByLabel.Reset();
}

FName FWatabouEmitSink::ResolvePin(const FName Label) const
{
	if (const FName* Pin = Params.LabelToPin.Find(Label)) { return *Pin; }
	return Params.DefaultPin;
}

#pragma endregion

#pragma region FWatabouProcessorBase

void FWatabouProcessorBase::GetOutputLabels(const UWatabouAssetBase* Asset, TArray<FName>& OutLabels) const
{
	if (!Asset) { return; }

	// Don't advertise default-skipped (metadata-only) ids as routable labels -- they aren't emitted.
	TSet<FName> DefaultSkip;
	GetDefaultSkipIds(DefaultSkip);

	// Distinct feature ids present in the asset (the labels a user can route to pins).
	for (const TPair<FWatabouFeatureIdentifier, int32>& Pair : Asset->Identifiers)
	{
		if (Pair.Value <= 0 || Pair.Key.Id.IsNone() || DefaultSkip.Contains(Pair.Key.Id)) { continue; }
		OutLabels.AddUnique(Pair.Key.Id);
	}
}

bool FWatabouProcessorBase::UsesFootprintAlignment() const
{
	// Footprint-aligned generators are exactly those that participate in the major-axis align bake: their
	// stamp is fitted to the footprint frame and is subject to the 90deg align + 180deg disambiguation.
	return EnumHasAnyFlags(SupportedBakes(), EWatabouBake::MajorAxisAlign);
}

void FWatabouProcessorBase::Process(FWatabouEmitSink& Sink) const
{
	const UWatabouAssetBase* Asset = Sink.GetAsset();
	if (!Asset || !Asset->Features) { return; }

	const FWatabouProcessParams& Params = Sink.GetParams();
	const EWatabouBake Supported = SupportedBakes();

	// Resolve bakes once per target: node toggle AND processor capability. The identity instance
	// supports none, so the city-family path stays a pure passthrough.
	FEmitContext Ctx;
	Ctx.LocalTransform = Params.LocalTransform;
	Ctx.FloorHeight = Sink.GetFloorHeight();
	Ctx.bFloorZ = Params.bApplyFloorHeight && EnumHasAnyFlags(Supported, EWatabouBake::FloorHeight);
	Ctx.bFacing = Params.bApplyFacing && EnumHasAnyFlags(Supported, EWatabouBake::Facing);
	const bool bAlign = Params.bApplyMajorAxisAlign && EnumHasAnyFlags(Supported, EWatabouBake::MajorAxisAlign);
	// Per-asset geometry (anchor + occupancy cells): precomputed once by the element and shared across
	// every target that stamps this asset, so the parallel compute never re-walks the asset. Fall back to
	// computing it inline if the element didn't (e.g. a direct caller) -- both paths use PrecomputeAssetGeometry.
	FWatabouAssetGeom LocalGeom;
	const FWatabouAssetGeom* Geom = Sink.GetAssetGeom();
	if (!Geom)
	{
		PrecomputeAssetGeometry(Asset, Params, LocalGeom);
		Geom = &LocalGeom;
	}

	// Anchor first: best-fit overlap places the occupancy cells (anchor-subtracted) to score orientations.
	Ctx.Anchor = Geom->Anchor;
	Ctx.Placement = ComputeAlignedPlacement(Sink, bAlign, *Geom, Ctx.bMirrorX);

	// Effective skip set: node SkipIds + this generator's default-skipped ids. A Process-local (not processor
	// state) so concurrent per-target Process() calls stay independent; Ctx points at it for the walk below.
	TSet<FName> EffectiveSkip = Params.SkipIds;
	GetDefaultSkipIds(EffectiveSkip);
	Ctx.SkipIds = &EffectiveSkip;

	ProcessCollection(Asset->Features, Sink, Ctx, FInherited{});

	// Consolidating generators (Dwellings) accumulate per-id during the walk -> stage the per-target
	// clouds now (one data set per id) instead of one per feature. No-op for the non-consolidating family.
	Sink.FlushConsolidated();
}

FTransform FWatabouProcessorBase::ComputeAlignedPlacement(const FWatabouEmitSink& Sink, const bool bAlignEnabled, const FWatabouAssetGeom& Geom, bool& bOutMirrorX) const
{
	bOutMirrorX = false;

	const FTransform& Placement = Sink.GetPlacement();
	if (!bAlignEnabled) { return Placement; }

	const UWatabouAssetBase* Asset = Sink.GetAsset();
	if (!Asset || !Asset->Bounds.IsValid) { return Placement; } // no footprint -> nothing to align against

	// Best-fit overlap: when the generator defines an occupancy id AND we have the seed footprint, pick the
	// orientation (of the box's 8: 4 rotations x mirror) that lands the most occupancy cells inside the
	// footprint. This resolves the near-square 90deg (axis), the 180deg (sign), AND the per-generator plan
	// chirality (mirror) -- the min-area box only pins the axis LINES, not which way they point or face.
	if (!FootprintFitOccupancyId().IsNone() && Sink.GetFootprintWorldXY().Num() >= 3)
	{
		return ComputeBestFitPlacement(Sink, Placement, Geom, bOutMirrorX);
	}

	// Fallback (no footprint to fit against, e.g. per-point stamps): major-axis 90deg by asset aspect.
	// Placement local +X = the footprint's longest axis; if the asset is longer along its native Y than X,
	// rotate -90deg so its long side maps onto +X. Near-square (within epsilon) is ambiguous -> left as-is.
	const FVector Size = Asset->Bounds.GetSize();
	constexpr double AspectEpsilon = 0.01; // ignore sub-percent differences as "square"
	if (Size.Y > Size.X * (1.0 + AspectEpsilon))
	{
		return FTransform(FRotator(0.0, -90.0, 0.0)) * Placement;
	}
	return Placement;
}

FTransform FWatabouProcessorBase::ComputeBestFitPlacement(const FWatabouEmitSink& Sink, const FTransform& BasePlacement, const FWatabouAssetGeom& Geom, bool& bOutMirrorX) const
{
	bOutMirrorX = false;

	const TArray<FVector2D>& Footprint = Sink.GetFootprintWorldXY();

	// Occupancy cell centers in projected-local space (precomputed once per asset). Anchor-subtracted
	// inline in the scoring loop to match the actual stamp (MakePointTransform shifts by the same anchor),
	// without copying the shared array. Z is irrelevant to the 2D overlap.
	const TArray<FVector2D>& Cells = Geom.OccupancyProjected;
	if (Cells.IsEmpty()) { return BasePlacement; }
	const FVector2D Anchor = Geom.Anchor;

	// Extents of both shapes in the SAME placement-local frame. BasePlacement already aligns the footprint's
	// long axis to local +X (its yaw came from the seed's min-area box long axis), so the footprint box is
	// axis-aligned here; the cells live in projected-local (anchor-subtracted) space. The inverse-BasePlacement
	// also strips its own P0 scale, so both extents are in the same metric and their ratio is scale-free.
	FBox2D CellBox(ForceInit);
	for (const FVector2D& C : Cells) { CellBox += (C - Anchor); }

	FBox2D FootBox(ForceInit);
	for (const FVector2D& F : Footprint)
	{
		const FVector L = BasePlacement.InverseTransformPosition(FVector(F.X, F.Y, 0.0));
		FootBox += FVector2D(L.X, L.Y);
	}

	const FVector2D CellSize = CellBox.GetSize();
	const FVector2D FootSize = FootBox.GetSize();

	// 1) AXIS (the 90deg choice) -- decided by ASPECT, never by counting points. This is the part that MUST be
	// scale-invariant: orientation tracks the dwelling's proportions, not the node's ScaleFactor. The previous
	// approach inferred the axis from a binary point-in-polygon count, which collapsed to near-ties at the
	// footprint boundary (discrete cell grid) and flipped the long axis on simple cases. Aspect is exact: the
	// footprint's long axis is local +X, so rotate the cells 90deg iff they are longer along their Y than X --
	// that lays their long side onto +X too. Near-square cells (within epsilon) carry no axis preference.
	constexpr double AspectEps = 0.02; // sub-2% difference reads as "square" -> no rotation signal
	const int32 BaseK = (CellSize.Y > CellSize.X * (1.0 + AspectEps)) ? 1 : 0;

	// 2) SIGN + MIRROR (the residual) -- THIS is what overlap counting is genuinely good at: the 180deg flip
	// and the plan chirality matter only for asymmetric (e.g. L-shaped) footprints, where the wrong sign drops
	// the cells' "foot" into the empty notch (outside the polygon). Score only the two axis-PRESERVING rotations
	// {BaseK, BaseK+180} x optional local-X mirror, at a NORMALIZED scale (cloud refit to the footprint so size
	// cancels out). Since the axis is already locked, boundary noise can at worst pick the wrong 180deg/mirror
	// on an asymmetric footprint -- never a perpendicular grid. Base + no-mirror is tried first with a strict
	// '>' so symmetric footprints and chiral-correct stamps stay on the base orientation.
	const double CellDiag = CellSize.Size();
	const double FootDiag = FootSize.Size();
	const double NormScale = (CellDiag > KINDA_SMALL_NUMBER) ? (FootDiag / CellDiag) : 1.0;
	FTransform ScoringLocal = Sink.GetParams().LocalTransform;
	ScoringLocal.SetScale3D(FVector(NormScale)); // node rotation/translation kept; scale normalized away

	int32 BestInside = -1;
	int32 BestK = BaseK;
	bool bBestMirror = false;
	for (int32 MirrorPass = 0; MirrorPass < 2; MirrorPass++)
	{
		const bool bMirror = (MirrorPass == 1);
		for (int32 Step = 0; Step < 2; Step++)
		{
			const int32 k = (BaseK + Step * 2) % 4; // BaseK then BaseK+180 -- both keep the long axis on +X
			const FTransform Composite = ScoringLocal * (FTransform(FRotator(0.0, 90.0 * k, 0.0)) * BasePlacement);

			int32 Inside = 0;
			for (const FVector2D& C : Cells)
			{
				const double LX = C.X - Anchor.X;
				const double LY = C.Y - Anchor.Y;
				const FVector World = Composite.TransformPosition(FVector(bMirror ? -LX : LX, LY, 0.0));
				if (WatabouMath::PolygonContainsPoint(Footprint, FVector2D(World.X, World.Y))) { ++Inside; }
			}

			if (Inside > BestInside) { BestInside = Inside; BestK = k; bBestMirror = bMirror; }
		}
	}

	bOutMirrorX = bBestMirror;
	return FTransform(FRotator(0.0, 90.0 * BestK, 0.0)) * BasePlacement;
}

void FWatabouProcessorBase::ProcessCollection(const UWatabouFeaturesCollection* Collection, FWatabouEmitSink& Sink, const FEmitContext& Ctx, FInherited Inherited) const
{
	if (!Collection) { return; }

	// Overlay this collection's inheritable semantics onto the inherited context (generic: any ancestor
	// collection may set them). Floor-z reads "level" -- Dwellings puts it on each floor_<n> collection.
	if (const double* Level = Collection->Details.NumericValues.Find(FName(TEXT("level"))))
	{
		Inherited.Level = *Level;
		Inherited.bHasLevel = true;
	}

	const FWatabouProcessParams& Params = Sink.GetParams();

	// Consolidating generators (Dwellings) fold every feature into per-id, per-target clouds via the sink
	// accumulator (ConsolidateFeature) rather than one data set per feature -- the per-feature explosion fix.
	const bool bConsolidate = ConsolidatesById();

	// Precomputed single-point emissions (native Points + OBB-reduced features) flagged for merge
	// accumulate here per id, then emit as one cloud. (Non-consolidating path only.)
	TMap<FName, TArray<FMergedPoint>> MergeGroups;

	const int32 NumElements = Collection->Elements.Num();
	for (int32 i = 0; i < NumElements; i++)
	{
		const FWatabouFeature& Feature = Collection->Elements[i];
		if ((Ctx.SkipIds && Ctx.SkipIds->Contains(Feature.Id)) || Feature.Coordinates.IsEmpty()) { continue; }

		// Consolidating generators (Dwellings) deliberately bypass the per-rule bMergePoints /
		// bReduceToOrientedBox routing below: a dwelling emits hundreds of per-cell / per-edge features, and
		// one data set per feature was generating thousands of data objects (the City->Dwellings freeze).
		// Folding everything into a handful of per-id clouds is the whole point of this path, so those
		// per-feature toggles don't apply here -- Dwellings gets this special treatment by design.
		if (bConsolidate)
		{
			ConsolidateFeature(Collection, i, Sink, Ctx, Inherited);
			continue;
		}

		const bool bMerge = Params.MergeSinglePoints.Contains(Feature.Id);

		// Reduce-to-OBB collapses an areal feature (Polygon / LineString) to one oriented-box point.
		const bool bReduce = Params.ReduceToOrientedBoxIds.Contains(Feature.Id)
			&& (Feature.Type == EWatabouFeatureType::Polygon || Feature.Type == EWatabouFeatureType::LineString);

		if (bReduce)
		{
			const FMergedPoint Point = MakeOrientedBoxPoint(Collection, i, Ctx, Inherited, Sink);
			if (bMerge) { MergeGroups.FindOrAdd(Feature.Id).Add(Point); }
			else { EmitSinglePoint(Feature.Id, Point, Sink); }
			continue;
		}

		switch (Feature.Type)
		{
		case EWatabouFeatureType::MultiPoints:
			EmitGeometry(Collection, i, Sink, Ctx, Inherited, /*bAsPath=*/false, /*bClosed=*/false);
			break;

		case EWatabouFeatureType::LineString:
			EmitGeometry(Collection, i, Sink, Ctx, Inherited, /*bAsPath=*/true, /*bClosed=*/false);
			break;

		case EWatabouFeatureType::Polygon:
			EmitGeometry(Collection, i, Sink, Ctx, Inherited, /*bAsPath=*/true, /*bClosed=*/true);
			break;

		case EWatabouFeatureType::Point:
			if (bMerge)
			{
				// Precompute now (with floor-z + facing) so the merge cloud is homogeneous payloads.
				FVector2D FacingDir = FVector2D::ZeroVector;
				const bool bFacing = Ctx.bFacing && ResolveFacing(Collection, i, FacingDir);

				FMergedPoint Point;
				Point.Transform = MakePointTransform(Feature.Coordinates[0], Ctx, Inherited, bFacing ? &FacingDir : nullptr);
				Point.Key = Sink.KeyFor(Collection, i);
				MergeGroups.FindOrAdd(Feature.Id).Add(Point);
			}
			else
			{
				EmitGeometry(Collection, i, Sink, Ctx, Inherited, /*bAsPath=*/false, /*bClosed=*/false);
			}
			break;

		default:
			break; // Unknown / Collection types never appear as leaf elements.
		}
	}

	// Emit each merge group (one cloud per flagged id), with per-point feature keys + bounds.
	for (const TPair<FName, TArray<FMergedPoint>>& Group : MergeGroups)
	{
		EmitMergedPoints(Group.Key, Group.Value, Sink);
	}

	for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : Collection->SubCollections)
	{
		ProcessCollection(Sub, Sink, Ctx, Inherited);
	}
}

void FWatabouProcessorBase::EmitGeometry(const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, FWatabouEmitSink& Sink, const FEmitContext& Ctx, const FInherited& Inherited, const bool bAsPath, const bool bClosed) const
{
	const FWatabouFeature& Feature = Collection->Elements[ElementIndex];
	const int32 N = Feature.Coordinates.Num();
	if (N <= 0) { return; }

	// Facing applies only to dir-bearing single-point features (doors / windows / stairs / exit) --
	// rooms (MultiPoints) and paths carry no direction.
	FVector2D FacingDir = FVector2D::ZeroVector;
	const bool bFacing = Ctx.bFacing && Feature.Type == EWatabouFeatureType::Point && ResolveFacing(Collection, ElementIndex, FacingDir);

	TArray<FTransform> Transforms;
	Transforms.Reserve(N);
	for (int32 k = 0; k < N; k++)
	{
		Transforms.Add(MakePointTransform(Feature.Coordinates[k], Ctx, Inherited, bFacing ? &FacingDir : nullptr));
	}

	TArray<FVector> Extents;
	Extents.Init(FVector(0.5), Transforms.Num());

	// Generator-specific data-level tags for this single feature (e.g. OPD tags rotunda outlines "rotunda").
	TArray<FName> FeatureTags;
	GetFeatureTags(Collection, ElementIndex, FeatureTags);

	Sink.RecordFeature(Feature.Id, MoveTemp(Transforms), MoveTemp(Extents), Sink.KeyFor(Collection, ElementIndex), bAsPath, bClosed, MoveTemp(FeatureTags));
}

void FWatabouProcessorBase::ConsolidateFeature(const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, FWatabouEmitSink& Sink, const FEmitContext& Ctx, const FInherited& Inherited) const
{
	const FWatabouFeature& Feature = Collection->Elements[ElementIndex];
	const int64 Key = Sink.KeyFor(Collection, ElementIndex);
	const FName Bucket = ConsolidationBucket(Feature.Id); // several ids may fold into one cloud (e.g. tiles / features)

	switch (Feature.Type)
	{
	case EWatabouFeatureType::Point:
	{
		FVector2D Dir = FVector2D::ZeroVector;
		const bool bHasDir = (Ctx.bFacing || UsesEdgeOffset()) && ResolveFacing(Collection, ElementIndex, Dir);

		// Edge feature (door / window) with endpoint vertices: place it at the wall MIDPOINT and face it
		// outward from its interior room cell. This is robust to the exported dir flipping on upper-floor
		// courtyard / setback edges -- the very bug that landed openings on interior walls.
		FVector2D Va, Vb;
		if (UsesEdgeOffset() && ResolveEdgeEndpoints(Collection, ElementIndex, Va, Vb))
		{
			// Coordinates[0] is the opening's attached cell (edge2cell = the interior room cell).
			const FTransform Transform = MakeEdgeTransform(Va, Vb, Feature.Coordinates[0], Ctx, Inherited);
			Sink.AddConsolidated(Bucket, Transform, FVector(0.5), Key);
			break;
		}

		// Fallback (stairs / exit, or a pre-endpoint import): land on the cell boundary via the dir offset
		// (EdgeOffsetLocal) and -- when facing is on -- face along it. Two edge features on one cell then
		// separate onto their own edges instead of coinciding.
		const FTransform Transform = MakePointTransform(Feature.Coordinates[0], Ctx, Inherited, bHasDir ? &Dir : nullptr);
		Sink.AddConsolidated(Bucket, Transform, FVector(0.5), Key);
		break;
	}

	case EWatabouFeatureType::MultiPoints:
		// Tile feature (rooms): one point per occupied cell, every cell sharing the room's feature key.
		for (const FVector2D& Coord : Feature.Coordinates)
		{
			const FTransform Transform = MakePointTransform(Coord, Ctx, Inherited, nullptr);
			Sink.AddConsolidated(Bucket, Transform, FVector(0.5), Key);
		}
		break;

	case EWatabouFeatureType::LineString:
		// Areal features have no point reduction here -> emit standalone. Dwellings carries none; this
		// keeps the path total for other consolidating generators (e.g. One Page Dungeon) that may add them.
		EmitGeometry(Collection, ElementIndex, Sink, Ctx, Inherited, /*bAsPath=*/true, /*bClosed=*/false);
		break;

	case EWatabouFeatureType::Polygon:
		EmitGeometry(Collection, ElementIndex, Sink, Ctx, Inherited, /*bAsPath=*/true, /*bClosed=*/true);
		break;

	default:
		break;
	}
}

void FWatabouProcessorBase::EmitSinglePoint(const FName Id, const FMergedPoint& Point, FWatabouEmitSink& Sink) const
{
	TArray<FTransform> Transforms;
	Transforms.Add(Point.Transform);
	TArray<FVector> Extents;
	Extents.Add(Point.Extents);

	// Standalone feature -> one @Data key (not a path).
	Sink.RecordFeature(Id, MoveTemp(Transforms), MoveTemp(Extents), Point.Key, /*bAsPath=*/false, /*bClosed=*/false);
}

void FWatabouProcessorBase::EmitMergedPoints(const FName Id, const TArray<FMergedPoint>& Points, FWatabouEmitSink& Sink) const
{
	const int32 N = Points.Num();
	if (N <= 0) { return; }

	TArray<FTransform> Transforms;
	TArray<FVector> Extents;
	TArray<int64> Keys;
	Transforms.Reserve(N);
	Extents.Reserve(N);
	Keys.Reserve(N);
	for (const FMergedPoint& Point : Points)
	{
		Transforms.Add(Point.Transform);
		Extents.Add(Point.Extents);
		Keys.Add(Point.Key);
	}

	Sink.RecordCloud(Id, MoveTemp(Transforms), MoveTemp(Extents), MoveTemp(Keys));
}

FWatabouProcessorBase::FMergedPoint FWatabouProcessorBase::MakeOrientedBoxPoint(const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, const FEmitContext& Ctx, const FInherited& Inherited, const FWatabouEmitSink& Sink) const
{
	const FWatabouFeature& Feature = Collection->Elements[ElementIndex];

	// Compute the OBB in the PROJECTED LOCAL space (post-ProjectPoint), so the box + its yaw share one
	// frame with the rest of the emitted geometry before LocalTransform / Placement.
	TArray<FVector2D> Local;
	Local.Reserve(Feature.Coordinates.Num());
	for (const FVector2D& C : Feature.Coordinates) { Local.Add(ProjectPoint(C)); }

	const WatabouPCG::FOrientedBox2D Box = WatabouPCG::ComputeOrientedBox2D(Local);

	const double Z = (Ctx.bFloorZ && Inherited.bHasLevel) ? Inherited.Level * Ctx.FloorHeight : 0.0;

	FMergedPoint Point;
	Point.Key = Sink.KeyFor(Collection, ElementIndex);

	// Anchor-shift the OBB center (the whole asset, reduced points included, shifts uniformly), then apply
	// the best-fit mirror exactly as MakePointTransform does for every other point -- reflect about local X
	// (negate the anchor-subtracted X; yaw -> 180-yaw, the box being centrally symmetric) so a reduced
	// point stays coherent with the rest of a mirrored stamp. No current generator both fits-to-footprint
	// (the only source of bMirrorX) and reduces-to-OBB, but the base exposes both, so keep them consistent.
	double CenterX  = Box.Center.X - Ctx.Anchor.X;
	const double CenterY = Box.Center.Y - Ctx.Anchor.Y;
	double Yaw = Box.YawDegrees;
	if (Ctx.bMirrorX) { CenterX = -CenterX; Yaw = 180.0 - Yaw; }
	const FTransform Coord(FRotator(0.0, Yaw, 0.0), FVector(CenterX, CenterY, Z));
	Point.Transform = Coord * Ctx.LocalTransform * Ctx.Placement;
	Point.Extents = FVector(Box.Extents.X, Box.Extents.Y, 0.5); // XY from OBB, Z half-extent = unit (0.5)
	return Point;
}

FTransform FWatabouProcessorBase::MakePointTransform(const FVector2D& NativeCoord, const FEmitContext& Ctx, const FInherited& Inherited, const FVector2D* FacingDir) const
{
	// Anchor (asset reference point) is subtracted in projected-local space so the chosen point lands at
	// local origin, then LocalTransform / Placement position it. Z is unaffected (asset z=0 = ground).
	FVector2D LocalXY = ProjectPoint(NativeCoord) - Ctx.Anchor;

	// Dir-bearing features carry a local edge offset for cell-model generators (doors / windows sit on the
	// cell boundary, not its center). Zero for the base / city processors, so their points are unmoved.
	if (FacingDir) { LocalXY += EdgeOffsetLocal(*FacingDir); }

	// Best-fit may reflect the asset about its local X when the footprint match needs a MIRROR, not just a
	// rotation (e.g. Neighbourhood's plan is sampled mirror-handed vs City). Mirror the position here and
	// the facing below, together, so the whole stamp stays self-consistent (no negative-scale transform).
	if (Ctx.bMirrorX) { LocalXY.X = -LocalXY.X; }

	const double Z = (Ctx.bFloorZ && Inherited.bHasLevel) ? Inherited.Level * Ctx.FloorHeight : 0.0;

	FRotator LocalRot = FRotator::ZeroRotator;
	if (Ctx.bFacing && FacingDir)
	{
		// OrientDir applies the SAME handedness flip as ProjectPoint, so facing stays consistent with
		// positions. Yaw about local +Z; a feature's local +X then points along its (flipped) direction.
		FVector2D Oriented = OrientDir(*FacingDir);
		if (Ctx.bMirrorX) { Oriented.X = -Oriented.X; } // keep facing consistent with the mirrored position
		if (!Oriented.IsNearlyZero())
		{
			LocalRot.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Oriented.Y, Oriented.X));
		}
	}

	// world = Placement o LocalTransform o (local rotation/translation). Placement already carries the
	// major-axis alignment; LocalTransform folds the node Transform + ScaleFactor.
	const FTransform Coord(LocalRot, FVector(LocalXY.X, LocalXY.Y, Z));
	return Coord * Ctx.LocalTransform * Ctx.Placement;
}

FVector2D FWatabouProcessorBase::ResolveEdgeOutwardLocal(const FVector2D& Va, const FVector2D& Vb, const FVector2D& InteriorCellNative) const
{
	// Outward = from the interior room cell center to the wall midpoint. The bundle resolves each contour
	// opening's room via getRoom(edge2cell), so the attached cell (InteriorCellNative) is the interior side --
	// per floor, so there is no cross-floor confusion (lower floors share the same grid but a different cell).
	// ProjectEdgePoint maps the vertex midpoint without cell-centering; ProjectPoint centers the cell.
	const FVector2D Mid = ProjectEdgePoint((Va + Vb) * 0.5);
	const FVector2D Interior = ProjectPoint(InteriorCellNative);
	FVector2D Outward = Mid - Interior;
	Outward.Normalize(); // -> zero only if degenerate (midpoint coincides with the cell center)
	return Outward;
}

FTransform FWatabouProcessorBase::MakeEdgeTransform(const FVector2D& Va, const FVector2D& Vb, const FVector2D& InteriorCellNative, const FEmitContext& Ctx, const FInherited& Inherited) const
{
	// Position = the wall MIDPOINT (the exact segment the bundle drew the opening on), projected like a vertex
	// (no cell centering) so it lands between the room cell centers. Immune to the exported dir / winding flip.
	const FVector2D MidNative = (Va + Vb) * 0.5;
	FVector2D LocalXY = ProjectEdgePoint(MidNative) - Ctx.Anchor;

	// Facing = outward, from the interior room cell toward the wall (see ResolveEdgeOutwardLocal).
	FVector2D OutwardLocal = ResolveEdgeOutwardLocal(Va, Vb, InteriorCellNative);

	// Mirror position + facing together (see MakePointTransform): a best-fit reflection negates local X.
	if (Ctx.bMirrorX) { LocalXY.X = -LocalXY.X; }

	const double Z = (Ctx.bFloorZ && Inherited.bHasLevel) ? Inherited.Level * Ctx.FloorHeight : 0.0;

	FRotator LocalRot = FRotator::ZeroRotator;
	if (Ctx.bFacing && !OutwardLocal.IsNearlyZero())
	{
		if (Ctx.bMirrorX) { OutwardLocal.X = -OutwardLocal.X; } // keep facing consistent with the mirrored position
		LocalRot.Yaw = FMath::RadiansToDegrees(FMath::Atan2(OutwardLocal.Y, OutwardLocal.X));
	}

	const FTransform Coord(LocalRot, FVector(LocalXY.X, LocalXY.Y, Z));
	return Coord * Ctx.LocalTransform * Ctx.Placement;
}

bool FWatabouProcessorBase::ResolveFacing(const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, FVector2D& OutDir)
{
	const FWatabouFeatureDetails* Details = Collection->ElementsDetails.Find(ElementIndex);
	if (!Details) { return false; }
	return WatabouFacing::ReadDirection(*Details, OutDir);
}

bool FWatabouProcessorBase::ResolveEdgeEndpoints(const UWatabouFeaturesCollection* Collection, const int32 ElementIndex, FVector2D& OutVa, FVector2D& OutVb)
{
	const FWatabouFeatureDetails* Details = Collection->ElementsDetails.Find(ElementIndex);
	if (!Details) { return false; }

	const double* Ai = Details->NumericValues.Find(FName(TEXT("va_i")));
	const double* Aj = Details->NumericValues.Find(FName(TEXT("va_j")));
	const double* Bi = Details->NumericValues.Find(FName(TEXT("vb_i")));
	const double* Bj = Details->NumericValues.Find(FName(TEXT("vb_j")));
	if (!Ai || !Aj || !Bi || !Bj) { return false; }

	OutVa = FVector2D(*Ai, *Aj);
	OutVb = FVector2D(*Bi, *Bj);
	return true;
}

void FWatabouProcessorBase::PrecomputeAssetGeometry(const UWatabouAssetBase* Asset, const FWatabouProcessParams& Params, FWatabouAssetGeom& Out) const
{
	Out.Anchor = FVector2D::ZeroVector;
	Out.OccupancyProjected.Reset();
	Out.bValid = true;

	if (!Asset || !Asset->Features) { return; }

	const FName OccupancyId = FootprintFitOccupancyId();

	// Origin anchor needs no walk, and a non-fitting generator (no occupancy id) has nothing to collect:
	// skip the walk entirely then (matches the old ComputeAssetAnchor Origin early-out).
	if (Params.AssetAnchor == EWatabouAssetAnchor::Origin && OccupancyId.IsNone()) { return; }

	// One walk: full-footprint bounds/sum/count (for the anchor) AND the occupancy cells (for best-fit).
	// The anchor reflects the FULL footprint (not filtered by Skip / routing rules) so it stays a stable,
	// intrinsic asset property regardless of node config.
	FBox2D Bounds(ForceInit);
	FVector2D Sum = FVector2D::ZeroVector;
	int32 Count = 0;
	GatherAssetGeometry(Asset->Features, OccupancyId, Bounds, Sum, Count, Out.OccupancyProjected);

	if (Count <= 0) { return; } // anchor stays zero (matches the old empty-asset case)

	switch (Params.AssetAnchor)
	{
	case EWatabouAssetAnchor::Centroid:     Out.Anchor = Sum / static_cast<double>(Count); break;
	case EWatabouAssetAnchor::BoundsCenter: Out.Anchor = Bounds.bIsValid ? Bounds.GetCenter() : FVector2D::ZeroVector; break;
	default:                                Out.Anchor = FVector2D::ZeroVector; break; // Origin
	}
}

void FWatabouProcessorBase::GatherAssetGeometry(const UWatabouFeaturesCollection* Collection, const FName OccupancyId, FBox2D& OutBounds, FVector2D& OutSum, int32& OutCount, TArray<FVector2D>& OutOccupancy) const
{
	if (!Collection) { return; }

	for (const FWatabouFeature& Feature : Collection->Elements)
	{
		const bool bOccupancy = !OccupancyId.IsNone() && Feature.Id == OccupancyId;
		for (const FVector2D& Coord : Feature.Coordinates)
		{
			const FVector2D Projected = ProjectPoint(Coord);
			OutBounds += Projected;
			OutSum += Projected;
			++OutCount;
			if (bOccupancy) { OutOccupancy.Add(Projected); }
		}
	}

	for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : Collection->SubCollections)
	{
		GatherAssetGeometry(Sub, OccupancyId, OutBounds, OutSum, OutCount, OutOccupancy);
	}
}

#pragma endregion

#pragma region EmitComputedTargets

void WatabouPCG::EmitComputedTargets(FPCGContext* Context, const FWatabouProcessParams& Params, const TArray<FWatabouComputedTarget>& Targets)
{
	if (!Context) { return; }

	// One staged item per emission: a freshly allocated (but unfilled) point data, a pointer back to its
	// computed payload, and the target's forwarding context. The payloads stay owned by Targets, which the
	// caller keeps alive until this returns.
	struct FStaged
	{
		UPCGBasePointData* Data = nullptr;
		const FWatabouComputedEmission* Emission = nullptr;
		const FWatabouForwardHandler* Forward = nullptr;
		int32 SeedPointIndex = INDEX_NONE;
	};

	int32 Total = 0;
	for (const FWatabouComputedTarget& Target : Targets) { Total += Target.Emissions.Num(); }

	TArray<FStaged> Staged;
	Staged.Reserve(Total);

	// Phase 2 -- single-threaded: allocate each data set (NewObject) and append it to the output. These are
	// the only steps that must be serial (UObject creation + the shared output array). Force-create the
	// metadata sub-object here too, so the parallel fill never creates a UObject off the game thread.
	for (const FWatabouComputedTarget& Target : Targets)
	{
		for (const FWatabouComputedEmission& Em : Target.Emissions)
		{
			if (Em.Transforms.IsEmpty()) { continue; }

			UPCGBasePointData* Data = FPCGContext::NewPointData_AnyThread(Context);
			if (!Data) { continue; }
			Data->MutableMetadata();

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
			Output.Data = Data;
			Output.Pin = Em.Pin;
			if (!Em.Label.IsNone()) { Output.Tags.Add(Em.Label.ToString()); }
			for (const FName& Tag : Em.Tags) { if (!Tag.IsNone()) { Output.Tags.Add(Tag.ToString()); } }
			if (Em.bIsPath && !Params.PathlikeTag.IsEmpty()) { Output.Tags.Add(Params.PathlikeTag); }

			Staged.Add({ Data, &Em, Target.Forward, Target.SeedPointIndex });
		}
	}

	// Phase 3 -- parallel: size + fill each data set. Each iteration writes only its own data (and reads the
	// shared, const seed metadata for forwarding), so the fills are independent: no UObject creation, no
	// shared mutation.
	ParallelFor(Staged.Num(), [&Staged](const int32 Index)
	{
		const FStaged& S = Staged[Index];
		const FWatabouComputedEmission& Em = *S.Emission;
		UPCGBasePointData* Data = S.Data;

		const int32 N = Em.Transforms.Num();
		Data->SetNumPoints(N);
		Data->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);

		TPCGValueRange<FTransform> Transforms = Data->GetTransformValueRange();
		TPCGValueRange<FVector> BoundsMin = Data->GetBoundsMinValueRange();
		TPCGValueRange<FVector> BoundsMax = Data->GetBoundsMaxValueRange();
		const int32 Num = FMath::Min(Transforms.Num(), N);
		for (int32 k = 0; k < Num; k++)
		{
			Transforms[k] = Em.Transforms[k];
			const FVector Ext = Em.Extents.IsValidIndex(k) ? Em.Extents[k] : FVector(0.5);
			BoundsMin[k] = -Ext;
			BoundsMax[k] = Ext;
		}

		if (Em.bPerPointKeys)
		{
			// Per-point feature keys (Elements domain) -- a consolidated / merged cloud.
			WatabouPCG::EnsureMetadataEntries(Data, /*bConservative=*/false);
			if (UPCGMetadata* Metadata = Data->MutableMetadata())
			{
				if (FPCGMetadataAttribute<int64>* KeyAttr = Metadata->FindOrCreateAttribute<int64>(
					FPCGAttributeIdentifier(WatabouPCG::FeatureMapAttributes::Key), 0, /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true))
				{
					TPCGValueRange<int64> Entries = Data->GetMetadataEntryValueRange();
					const int32 KeyNum = FMath::Min(Entries.Num(), Em.Keys.Num());
					for (int32 k = 0; k < KeyNum; k++) { KeyAttr->SetValue(Entries[k], Em.Keys[k]); }
				}
			}
		}
		else
		{
			// Single feature -> one @Data key (+ IsClosed for paths).
			WatabouPCG::SetDataValue(Data, WatabouPCG::FeatureMapAttributes::Key, Em.DataKey);
			if (Em.bIsPath) { WatabouPCG::SetClosedLoop(Data, Em.bClosed); }
		}

		// Seed -> target attribute forwarding (relocated from the old per-target StageData; per-object, so
		// safe to run concurrently). SeedPointIndex selects per-point vs @Data-only mode.
		if (S.Forward)
		{
			if (UPCGMetadata* Metadata = Data->MutableMetadata())
			{
				const bool bDataDomainOnly = S.SeedPointIndex == INDEX_NONE;
				S.Forward->ForwardToData(bDataDomainOnly ? 0 : S.SeedPointIndex, Metadata, bDataDomainOnly);
			}
		}
	});
}

#pragma endregion
