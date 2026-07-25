// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

/**
 * Minimum-AREA 2D oriented bounding box (rotating calipers over the convex hull) plus the signed-area
 * polygon centroid.
 *
 * This is the CANONICAL footprint frame for the Watabou pipeline. The Dwellings plan encoder samples
 * its cell bitmap in this frame at import time, so the runtime stamp must reconstruct the SAME frame to
 * drop a dwelling onto its footprint without a spurious rotation. Sharing one implementation -- rather
 * than letting the runtime fit a DIFFERENT oriented box (UE's DiTO TMinVolumeBox3, which on coplanar
 * footprints both picks a different axis on skewed shapes and gives an arbitrary axis sign) -- is what
 * keeps gen-time and runtime orientation consistent.
 *
 * Pure FVector2D math, no PCG / GeometryProcessing dependency, so both the encoder (WatabouDwellings,
 * which must stay PCG-free) and the runtime processor (WatabouPCG) can call it.
 *
 * SIGN CAVEAT: the box's axis DIRECTIONS (V0 / V1) carry an algorithm-determined sign with no intrinsic
 * geometric meaning -- only the axis LINES are stable. A caller that needs a definite +/- orientation
 * must disambiguate it from the footprint CONTENT (e.g. the signed-area centroid offset along the long
 * axis), never from V0's raw sign.
 */
namespace WatabouMath
{
	/**
	 * A min-area oriented box: origin corner O (the minimum projection on both axes) plus the two edge
	 * vectors V0 (axis 0, along the area-minimizing hull edge) and V1 (axis 1, the left normal of V0).
	 * Len0 / Len1 are their lengths.
	 */
	struct FMinAreaBox2D
	{
		FVector2D O    = FVector2D::ZeroVector;
		FVector2D V0   = FVector2D::ZeroVector;
		FVector2D V1   = FVector2D::ZeroVector;
		double    Len0 = 0.0;
		double    Len1 = 0.0;
		bool      bValid = false;

		/** Box center (O + half of each edge vector). */
		FVector2D Center() const { return O + (V0 + V1) * 0.5; }

		/** Unit direction of the longer axis. The +/- sign is algorithm-arbitrary -- see the SIGN CAVEAT. */
		FVector2D LongAxisDir() const
		{
			const FVector2D Axis = (Len0 >= Len1) ? V0 : V1;
			return Axis.GetSafeNormal();
		}

		/** Length of the longer / shorter axis. */
		double LongLen() const  { return FMath::Max(Len0, Len1); }
		double ShortLen() const { return FMath::Min(Len0, Len1); }
	};

	/**
	 * Compute the minimum-area oriented box of a 2D polygon via rotating calipers over its convex hull.
	 * Returns false (Out left default, bValid = false) when the polygon has fewer than 3 hull vertices
	 * or collapses to zero area.
	 */
	WATABOUCORE_API bool ComputeMinAreaBox2D(TConstArrayView<FVector2D> Polygon, FMinAreaBox2D& Out);

	/**
	 * Signed-area weighted centroid of a polygon (com.watabou.geom.polygons.PolyCore.centroid).
	 * Degenerate (near-zero area) input returns the first vertex.
	 */
	WATABOUCORE_API FVector2D SignedAreaCentroid(TConstArrayView<FVector2D> Polygon);

	/**
	 * Point-in-polygon (horizontal ray cast, matching com.watabou jb.containsPoint). Used both to sample
	 * the Dwellings plan bitmap at import and to score the runtime best-fit overlap. < 3 vertices -> false.
	 */
	WATABOUCORE_API bool PolygonContainsPoint(TConstArrayView<FVector2D> Polygon, const FVector2D& P);
}
