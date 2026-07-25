// Copyright 2026 Timothé Lapetite

#include "WatabouOrientedBox.h"

#include "MinVolumeBox3.h"     // UE::Geometry::TMinVolumeBox3 (GeometryAlgorithms)
#include "OrientedBoxTypes.h"  // UE::Geometry::FOrientedBox3d (GeometryCore)
#include "Algo/Sort.h"
#include "Math/Box2D.h"        // FBox2D (degenerate fallback)

namespace WatabouPCG
{
	FOrientedBox2D ComputeOrientedBox2D(const TConstArrayView<FVector2D> Points)
	{
		FOrientedBox2D Out;

		const int32 Num = Points.Num();
		if (Num == 0) { return Out; } // bValid = false

		// Min-volume oriented box over the planar (z = 0) points. Mirrors PCGExMath::FBestFitPlane's
		// precise path so results match the seed-placement convention, without depending on PCGEx.
		UE::Geometry::TMinVolumeBox3<double> Box;
		Box.Solve(Num, [&Points](const int32 i) { return FVector(Points[i].X, Points[i].Y, 0.0); });

		if (Box.IsSolutionAvailable())
		{
			UE::Geometry::FOrientedBox3d OrientedBox;
			Box.GetResult(OrientedBox);

			// Sort axes by extent (descending) with index tie-break for determinism: X = longest in-plane
			// axis (the ~zero-thickness Z axis sorts last). Identical to FBestFitPlane::ProcessBox.
			int32 Swizzle[3] = {0, 1, 2};
			Algo::Sort(TArrayView<int32>(Swizzle, 3), [&OrientedBox](const int32 A, const int32 B)
			{
				if (OrientedBox.Extents[A] != OrientedBox.Extents[B]) { return OrientedBox.Extents[A] > OrientedBox.Extents[B]; }
				return A < B;
			});

			const FVector LongAxis = OrientedBox.Frame.GetAxis(Swizzle[0]);
			const FVector Center = OrientedBox.Center();

			Out.Center = FVector2D(Center.X, Center.Y);
			Out.YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(LongAxis.Y, LongAxis.X));
			Out.Extents = FVector2D(OrientedBox.Extents[Swizzle[0]], OrientedBox.Extents[Swizzle[1]]);
			Out.bValid = true;
			return Out;
		}

		// Degenerate input (collinear / coincident) -> axis-aligned bounds fallback (yaw 0).
		FBox2D AABB(ForceInit);
		for (const FVector2D& P : Points) { AABB += P; }

		Out.Center = AABB.GetCenter();
		Out.YawDegrees = 0.0;
		Out.Extents = AABB.GetExtent(); // half-extents
		Out.bValid = true;
		return Out;
	}
}
