// Copyright 2026 Timothé Lapetite

#include "WatabouMinAreaBox2D.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

// File-local helpers in a named namespace (Unity-build safe: a bare `static`/anonymous namespace would
// collide with same-named helpers in other files concatenated into one translation unit).
namespace WatabouMinAreaBox2D_Internal
{
	/** Andrew's monotone-chain convex hull (CCW). */
	TArray<FVector2D> ConvexHull(TConstArrayView<FVector2D> Points)
	{
		TArray<FVector2D> Pts(Points.GetData(), Points.Num());
		Pts.Sort([](const FVector2D& A, const FVector2D& B)
		{
			return A.X < B.X || (A.X == B.X && A.Y < B.Y);
		});

		const int32 N = Pts.Num();
		if (N < 3) { return Pts; }

		auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B)
		{
			return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
		};

		TArray<FVector2D> Hull;
		Hull.Reserve(2 * N);

		for (int32 I = 0; I < N; ++I)
		{
			while (Hull.Num() >= 2 && Cross(Hull[Hull.Num() - 2], Hull[Hull.Num() - 1], Pts[I]) <= 0.0)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(Pts[I]);
		}

		const int32 LowerSize = Hull.Num() + 1;
		for (int32 I = N - 2; I >= 0; --I)
		{
			while (Hull.Num() >= LowerSize && Cross(Hull[Hull.Num() - 2], Hull[Hull.Num() - 1], Pts[I]) <= 0.0)
			{
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(Pts[I]);
		}

		Hull.Pop(EAllowShrinking::No);
		return Hull;
	}
}

namespace WatabouMath
{
	FVector2D SignedAreaCentroid(TConstArrayView<FVector2D> Polygon)
	{
		const int32 N = Polygon.Num();
		if (N == 0) { return FVector2D::ZeroVector; }
		if (N == 1) { return Polygon[0]; }
		if (N == 2) { return (Polygon[0] + Polygon[1]) * 0.5; }

		double SumD = 0.0, SumX = 0.0, SumY = 0.0;
		FVector2D Prev = Polygon[N - 1];
		for (int32 I = 0; I < N; ++I)
		{
			const FVector2D Curr = Polygon[I];
			const double Cross = Prev.X * Curr.Y - Prev.Y * Curr.X;
			SumD += Cross;
			SumX += (Prev.X + Curr.X) * Cross;
			SumY += (Prev.Y + Curr.Y) * Cross;
			Prev = Curr;
		}
		if (FMath::IsNearlyZero(SumD)) { return Polygon[0]; }
		const double K = 1.0 / (3.0 * SumD);
		return FVector2D(K * SumX, K * SumY);
	}

	bool PolygonContainsPoint(TConstArrayView<FVector2D> Polygon, const FVector2D& P)
	{
		const int32 N = Polygon.Num();
		if (N < 3) { return false; }

		bool bInside = false;
		FVector2D Prev = Polygon[N - 1];
		for (int32 I = 0; I < N; ++I)
		{
			const FVector2D Curr = Polygon[I];
			const double EdgeX = Curr.X - Prev.X;
			const double EdgeY = Curr.Y - Prev.Y;
			if (EdgeY != 0.0)
			{
				const double T = (EdgeY * (P.X - Prev.X) - EdgeX * (P.Y - Prev.Y)) / EdgeY;
				if (T <= 0.0)
				{
					const double S = FMath::Abs(EdgeX) > FMath::Abs(EdgeY)
						? (P.X - Prev.X - T) / EdgeX
						: (P.Y - Prev.Y) / EdgeY;
					if (S >= 0.0 && S <= 1.0)
					{
						bInside = !bInside;
					}
				}
			}
			Prev = Curr;
		}
		return bInside;
	}

	bool ComputeMinAreaBox2D(TConstArrayView<FVector2D> Polygon, FMinAreaBox2D& Out)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WatabouMath::ComputeMinAreaBox2D);

		using namespace WatabouMinAreaBox2D_Internal;

		const TArray<FVector2D> Hull = ConvexHull(Polygon);
		const int32 NH = Hull.Num();
		if (NH < 3) { return false; }

		double  BestArea = TNumericLimits<double>::Max();
		FVector2D BestDir = FVector2D(1.0, 0.0);
		double  BestMinD = 0.0, BestMaxD = 0.0, BestMinP = 0.0, BestMaxP = 0.0;
		bool    bFound = false;

		for (int32 I = 0; I < NH; ++I)
		{
			const FVector2D A = Hull[I];
			const FVector2D B = Hull[(I + 1) % NH];
			FVector2D Edge = B - A;
			const double EdgeLen = Edge.Size();
			if (EdgeLen <= 0.0) { continue; }
			Edge /= EdgeLen;

			const double L = Edge.X;
			const double H = -Edge.Y;

			double MinD = TNumericLimits<double>::Max(), MaxD = -TNumericLimits<double>::Max();
			double MinP = TNumericLimits<double>::Max(), MaxP = -TNumericLimits<double>::Max();
			for (const FVector2D& M : Hull)
			{
				const double D = M.X * L - M.Y * H;
				const double P = M.Y * L + M.X * H;
				if (D < MinD) MinD = D;
				if (D > MaxD) MaxD = D;
				if (P < MinP) MinP = P;
				if (P > MaxP) MaxP = P;
			}

			const double Area = (MaxD - MinD) * (MaxP - MinP);
			if (Area < BestArea)
			{
				BestArea = Area;
				BestDir  = Edge;
				BestMinD = MinD; BestMaxD = MaxD;
				BestMinP = MinP; BestMaxP = MaxP;
				bFound = true;
			}
		}

		if (!bFound || BestArea <= 0.0) { return false; }

		const double B = BestDir.Y;
		const double C = BestDir.X;
		auto Rotate = [B, C](double X, double Y) -> FVector2D
		{
			return FVector2D(X * C - Y * B, Y * C + X * B);
		};

		const FVector2D BL = Rotate(BestMinD, BestMinP);
		const FVector2D BR = Rotate(BestMaxD, BestMinP);
		const FVector2D TL = Rotate(BestMinD, BestMaxP);

		Out.O    = BL;
		Out.V0   = BR - BL;
		Out.V1   = TL - BL;
		Out.Len0 = Out.V0.Size();
		Out.Len1 = Out.V1.Size();
		Out.bValid = true;
		return true;
	}
}
