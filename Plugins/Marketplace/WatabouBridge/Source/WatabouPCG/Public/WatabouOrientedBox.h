// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

namespace WatabouPCG
{
	/**
	 * A 2D oriented bounding box: center, in-plane rotation (yaw, degrees, about +Z), and half-extents
	 * with X = the longest axis. Mirrors the frame convention of PCGEx FBestFitPlane (local +X = longest).
	 */
	struct FOrientedBox2D
	{
		FVector2D Center = FVector2D::ZeroVector;
		double YawDegrees = 0.0;
		FVector2D Extents = FVector2D(0.5, 0.5); // half-extents; X >= Y
		bool bValid = false;
	};

	/**
	 * Oriented box over a set of 2D points, via UE's minimum-volume box (UE::Geometry::TMinVolumeBox3 on
	 * the z=0 plane) plus an extent-descending axis sort -- the SAME algorithm PCGEx FBestFitPlane uses in
	 * precise mode, replicated here so WatabouPCG stays free of any PCGEx dependency (it links UE's
	 * GeometryCore / GeometryAlgorithms only). Falls back to an axis-aligned box for degenerate (collinear
	 * / coincident) input. Empty input returns bValid = false.
	 */
	WATABOUPCG_API FOrientedBox2D ComputeOrientedBox2D(TConstArrayView<FVector2D> Points);
}
