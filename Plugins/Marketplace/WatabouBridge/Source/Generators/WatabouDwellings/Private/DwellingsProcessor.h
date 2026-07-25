// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Processors/WatabouProcessor.h"

/**
 * Processor for Watabou Dwellings (cell / floor-stack model).
 *
 * Native coordinates are integer cell indices (i, j): rooms are MultiPoints of cell centers;
 * doors / windows / stairs / exit are dir-bearing Points. The ONLY generator-specific behavior is
 * the projection -- a cell maps to its UNIT-CELL CENTER. NO X flip: the plan is already baked in UE X
 * handedness upstream (DwellingsPlanEncoder NegateX), so a second flip here would mirror the dwelling.
 * One cell is one unit; the node's ScaleFactor (folded into LocalTransform) sets its world size.
 *
 * Everything else is generic: floor levels are carried down the tree by FWatabouProcessorBase from
 * each floor_<level> collection (-> floor-z), facing yaw and the major-axis 90deg alignment are
 * computed by the base. This class therefore declares the bakes it participates in and nothing more.
 */
class FDwellingsProcessor : public FWatabouProcessorBase
{
protected:
	virtual FVector2D ProjectPoint(const FVector2D& Native) const override
	{
		// Cell (i, j) -> unit-cell center. No X flip: an extra flip here mirrored the dwelling -- the cells
		// already arrive in UE X handedness from the plan.
		return FVector2D(Native.X + 0.5, Native.Y + 0.5);
	}

	virtual FVector2D OrientDir(const FVector2D& NativeDir) const override
	{
		// Linear part of ProjectPoint (no centering). Identity, matching ProjectPoint's no-flip handedness --
		// keeps facing + edge offsets consistent with positions.
		return FVector2D(NativeDir.X, NativeDir.Y);
	}

	virtual FVector2D ProjectEdgePoint(const FVector2D& Vertex) const override
	{
		// Edge endpoints are grid VERTICES (cell corners), not cell indices, so they get NO +0.5 centering --
		// a vertex (i, j) sits at the corner shared by the cells whose centers ProjectPoint maps to (i+-0.5,
		// j+-0.5). Identity (no flip), so an edge midpoint lands exactly on the wall between room cells.
		return Vertex;
	}

	virtual EWatabouBake SupportedBakes() const override
	{
		return EWatabouBake::FloorHeight | EWatabouBake::Facing | EWatabouBake::MajorAxisAlign;
	}

	virtual FName FootprintFitOccupancyId() const override
	{
		// Best-fit overlap scores orientations by how many ROOM cells land inside the footprint -- the rooms
		// are the dwelling's occupied mass (doors / windows / stairs are sparse edge features, excluded).
		return FName(TEXT("rooms"));
	}

	virtual bool UsesEdgeOffset() const override { return true; }

	virtual FVector2D EdgeOffsetLocal(const FVector2D& NativeDir) const override
	{
		// Edge/hint features (doors / windows / stairs / exit) carry a cell + a dir but live on the cell
		// BOUNDARY, not its center: shove half a cell along the (projected) dir so they sit "in between" the
		// two cells they separate. Two edge features on the same cell (different dirs) then land on their own
		// edges instead of coinciding. Uses OrientDir so it shares ProjectPoint's handedness. Rooms (tiles)
		// pass no dir and keep their cell-center position.
		return OrientDir(NativeDir).GetSafeNormal() * 0.5;
	}

	virtual bool ConsolidatesById() const override { return true; }

	virtual FName ConsolidationBucket(const FName FeatureId) const override
	{
		// Collapse to TWO clouds instead of one per id: "tiles" (the room cells -- the only true tiles) and
		// "features" (doors / windows / stairs / exit -- edge data). Each point keeps its own WatabouKey, so
		// the exact id / type is still recovered downstream via the Feature Map (load properties).
		return FeatureId == FName(TEXT("rooms")) ? FName(TEXT("tiles")) : FName(TEXT("features"));
	}
};
