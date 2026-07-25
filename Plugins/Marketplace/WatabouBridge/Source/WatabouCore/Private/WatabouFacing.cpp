// Copyright 2026 Timothé Lapetite

#include "WatabouFacing.h"
#include "WatabouFeatureTypes.h"

namespace WatabouFacing
{
	bool ReadDirection(const FWatabouFeatureDetails& Details, FVector2D& OutDir)
	{
		// Cardinal-string form (Dwellings: NameValues["dir"]). Native Watabou screen space: +X = east,
		// +Y = south. FName comparison is case-insensitive, so "N" matches "n". Diagonals are normalized
		// (yaw is scale-invariant, but a unit vector keeps the contract clean).
		if (const FName* Cardinal = Details.NameValues.Find(FName(TEXT("dir"))))
		{
			const FName Dir = *Cardinal;
			if (Dir == FName(TEXT("n")))  { OutDir = FVector2D(0.0, -1.0);  return true; }
			if (Dir == FName(TEXT("s")))  { OutDir = FVector2D(0.0, 1.0);   return true; }
			if (Dir == FName(TEXT("e")))  { OutDir = FVector2D(1.0, 0.0);   return true; }
			if (Dir == FName(TEXT("w")))  { OutDir = FVector2D(-1.0, 0.0);  return true; }
			if (Dir == FName(TEXT("ne"))) { OutDir = FVector2D(1.0, -1.0).GetSafeNormal();  return true; }
			if (Dir == FName(TEXT("nw"))) { OutDir = FVector2D(-1.0, -1.0).GetSafeNormal(); return true; }
			if (Dir == FName(TEXT("se"))) { OutDir = FVector2D(1.0, 1.0).GetSafeNormal();   return true; }
			if (Dir == FName(TEXT("sw"))) { OutDir = FVector2D(-1.0, 1.0).GetSafeNormal();  return true; }
			return false; // unrecognized token -> no facing
		}

		// Vector form (One Page Dungeon: NumericValues["dir_x"/"dir_y"]).
		const double* Dx = Details.NumericValues.Find(FName(TEXT("dir_x")));
		const double* Dy = Details.NumericValues.Find(FName(TEXT("dir_y")));
		if (Dx && Dy)
		{
			OutDir = FVector2D(*Dx, *Dy);
			return !OutDir.IsNearlyZero();
		}

		return false;
	}
}
