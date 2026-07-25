// Copyright 2026 Timothé Lapetite

#include "DwellingsPlanEncoder.h"
#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouFeatureTypes.h"
#include "WatabouMinAreaBox2D.h"

#include "StructUtils/InstancedStruct.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace DwellingsPlanEncoder_Internal
{
	// The min-area oriented box, signed-area centroid, and point-in-polygon test moved to WatabouCore
	// (WatabouMath) so the runtime stamp reconstructs the IDENTICAL footprint frame + fit test (see
	// WatabouMinAreaBox2D.h). That MOVE is byte-for-byte equivalent (relocating it changes no plan).
	// NOTE: ComputeCellCounts below was separately, intentionally changed (aspect-preserving clamp) -- it
	// DOES alter the emitted plan for footprints that hit MaxCells, so those re-import differently.

	/**
	 * Derive (Cols, Rows) cell counts from the OBB. Cols indexes along V0 in the
	 * sampling loop and becomes URL "w"; Rows indexes along V1 and becomes URL "h".
	 * bSwapURLAxes selects which OBB length feeds Cols vs Rows (len0 vs len1) -- that
	 * is how the per-generator "w<-len0" vs "w<-len1" conventions are expressed
	 * without changing the sampling-loop axes. Both counts are clamped to
	 * [1, MaxCells]; existing generators use 11, Neighbourhood uses 16.
	 */
	static void ComputeCellCounts(
		const WatabouMath::FMinAreaBox2D& OBB,
		const DwellingsPlanEncoder::FOptions& Options,
		int32& OutCols,
		int32& OutRows)
	{
		const double Div = FMath::Max(Options.CellSizeDivisor, KINDA_SMALL_NUMBER);
		const int32 MaxCells = FMath::Max(1, Options.MaxCells);

		// Float cell counts BEFORE any rounding. Watabou's naming: in Urban Places "cols" (URL "w") comes
		// from len1 and "rows" (URL "h") from len0; City swaps (bSwapURLAxes) so "cols" <- len0, "rows" <- len1.
		double FCols = (Options.bSwapURLAxes ? OBB.Len0 : OBB.Len1) / Div;
		double FRows = (Options.bSwapURLAxes ? OBB.Len1 : OBB.Len0) / Div;

		if (Options.bProportionalClamp)
		{
			// Preserve the footprint's aspect at the cap: if EITHER axis exceeds MaxCells, scale BOTH down by
			// the same factor. The old clamp rounded first and only rescaled when *Cols* overran -- so a tall
			// footprint had its Rows hard-clamped (aspect crushed) while a wide one of the same shape was
			// rescaled, making identical shapes fill differently by orientation alone. Operating on the float
			// ratio and rounding LAST keeps the proportion intact.
			const double MaxF = FMath::Max(FCols, FRows);
			if (MaxF > static_cast<double>(MaxCells))
			{
				const double Scale = static_cast<double>(MaxCells) / MaxF;
				FCols *= Scale;
				FRows *= Scale;
			}
		}

		OutCols = FMath::Clamp(FMath::RoundToInt32(FCols), 1, MaxCells);
		OutRows = FMath::Clamp(FMath::RoundToInt32(FRows), 1, MaxCells);
	}

	/**
	 * 4-connectivity flood-fill: keep only cells belonging to the largest connected
	 * region. Disconnected/isolated cells get cleared. Without this, irregular building
	 * polygons that sample into multiple bitmap components crash Watabou's connectRooms
	 * algorithm at runtime ("Cannot read properties of null (reading '__id__')").
	 *
	 * Single-component bitmaps are untouched.
	 */
	static void KeepLargestConnectedComponent(TArray<bool>& Cells, int32 Cols, int32 Rows)
	{
		const int32 N = Cells.Num();
		TArray<int32> CompId;
		CompId.Init(-1, N);
		TArray<int32> CompSize;
		TArray<int32> Stack;

		for (int32 Start = 0; Start < N; ++Start)
		{
			if (!Cells[Start] || CompId[Start] >= 0) { continue; }

			const int32 ThisId = CompSize.Num();
			int32 ThisSize = 0;
			Stack.Reset();
			Stack.Add(Start);
			while (Stack.Num() > 0)
			{
				const int32 Idx = Stack.Pop(EAllowShrinking::No);
				if (CompId[Idx] >= 0) { continue; }
				CompId[Idx] = ThisId;
				++ThisSize;

				const int32 R = Idx / Cols;
				const int32 C = Idx % Cols;
				if (C > 0          && Cells[Idx - 1]    && CompId[Idx - 1]    < 0) { Stack.Add(Idx - 1); }
				if (C < Cols - 1   && Cells[Idx + 1]    && CompId[Idx + 1]    < 0) { Stack.Add(Idx + 1); }
				if (R > 0          && Cells[Idx - Cols] && CompId[Idx - Cols] < 0) { Stack.Add(Idx - Cols); }
				if (R < Rows - 1   && Cells[Idx + Cols] && CompId[Idx + Cols] < 0) { Stack.Add(Idx + Cols); }
			}
			CompSize.Add(ThisSize);
		}

		if (CompSize.Num() <= 1) { return; }  // 0 or 1 components -- nothing to filter

		int32 LargestId = 0;
		for (int32 I = 1; I < CompSize.Num(); ++I)
		{
			if (CompSize[I] > CompSize[LargestId]) { LargestId = I; }
		}

		for (int32 I = 0; I < N; ++I)
		{
			if (Cells[I] && CompId[I] != LargestId) { Cells[I] = false; }
		}
	}

	/**
	 * Build the hex bitmap from the OBB + polygon. Per-cell sampling mirrors
	 * he.getPlan / Te.getPlan / Xd.getPlan (Neighbourhood walks V0 left-to-right
	 * rather than right-to-left; bReverseV0InSampling=false picks that variant).
	 *
	 * The sampled bitmap is then reduced to its largest connected component (unless
	 * bSkipPointInPolygon, where the bitmap is full and single-component by
	 * construction). That reduction is NOT part of upstream getPlan: it stops the
	 * generated dwelling from crashing the bundle's room connector on disconnected
	 * footprints, at the cost of byte-exactness for those (rare) multi-component cases.
	 */
	static FString EncodePlan(
		const WatabouMath::FMinAreaBox2D& OBB,
		TConstArrayView<FVector2D> Polygon,
		int32 Cols,
		int32 Rows,
		bool bSkipPointInPolygon,
		bool bReverseV0InSampling)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DwellingsPlanEncoder::EncodePlan);

		TArray<bool> Cells;
		Cells.Init(false, Cols * Rows);
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				if (bSkipPointInPolygon)
				{
					Cells[Row * Cols + Col] = true;
					continue;
				}
				const double NCol = bReverseV0InSampling
					? (static_cast<double>(Cols) - Col - 1 + 0.5) / Cols
					: (static_cast<double>(Col) + 0.5) / Cols;
				const double NRow = (static_cast<double>(Rows) - Row - 1 + 0.5) / Rows;
				const FVector2D Center(
					OBB.O.X + OBB.V0.X * NCol + OBB.V1.X * NRow,
					OBB.O.Y + OBB.V0.Y * NCol + OBB.V1.Y * NRow);
				Cells[Row * Cols + Col] = WatabouMath::PolygonContainsPoint(Polygon, Center);
			}
		}

		// Watabou's room connector chokes on disconnected cells; Village's full-bitmap
		// (bSkipPointInPolygon=true) is single-component by construction.
		if (!bSkipPointInPolygon)
		{
			KeepLargestConnectedComponent(Cells, Cols, Rows);
		}

		FString Out;
		Out.Reserve((Cols * Rows + 3) / 4);
		int32 G = 0;
		int32 F = 0;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				if (Cells[Row * Cols + Col])
				{
					G |= 1 << (F & 3);
				}

				const bool bLast = (Col == Cols - 1) && (Row == Rows - 1);
				if ((F & 3) == 3 || bLast)
				{
					static const TCHAR HexDigits[] = TEXT("0123456789ABCDEF");
					Out.AppendChar(HexDigits[G & 0xF]);
					G = 0;
				}
				++F;
			}
		}
		return Out;
	}

	/** Copy of Polygon with X negated. Used to un-flip Unreal-space input. */
	static TArray<FVector2D> NegateX(TConstArrayView<FVector2D> Polygon)
	{
		TArray<FVector2D> Out;
		Out.Reserve(Polygon.Num());
		for (const FVector2D& P : Polygon) { Out.Emplace(-P.X, P.Y); }
		return Out;
	}
}

bool DwellingsPlanEncoder::MakeSeedRefFromPolygon(
	TConstArrayView<FVector2D> Polygon,
	FWatabouSeedRef& OutSeedRef,
	const FOptions& Options)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DwellingsPlanEncoder::MakeSeedRefFromPolygon);

	using namespace DwellingsPlanEncoder_Internal;

	if (Polygon.Num() < 3) { return false; }

	TArray<FVector2D> Negated;
	TConstArrayView<FVector2D> Poly;
	if (Options.bUnrealSpaceInput)
	{
		Negated = NegateX(Polygon);
		Poly = Negated;
	}
	else
	{
		Poly = Polygon;
	}

	WatabouMath::FMinAreaBox2D OBB;
	if (!WatabouMath::ComputeMinAreaBox2D(Poly, OBB)) { return false; }

	// Neighbourhood's Building constructor enforces len0 >= len1; rotate-calipers
	// alone doesn't guarantee that ordering, so swap when requested.
	if (Options.bEnsureLongAxisV0 && OBB.Len0 < OBB.Len1)
	{
		Swap(OBB.V0, OBB.V1);
		Swap(OBB.Len0, OBB.Len1);
	}

	int32 Cols = 0, Rows = 0;
	ComputeCellCounts(OBB, Options, Cols, Rows);

	const FString Plan = EncodePlan(OBB, Poly, Cols, Rows, Options.bSkipPointInPolygon, Options.bReverseV0InSampling);

	int64 Seed64 = 0;
	if (Options.ParentSeed >= 0)
	{
		Seed64 = Options.ParentSeed + Options.ElementIndex;
	}
	else
	{
		const FVector2D Centre = WatabouMath::SignedAreaCentroid(Poly);
		Seed64 = static_cast<int64>(1000.0 * FMath::Abs(Centre.X + Centre.Y));
	}

	OutSeedRef.GeneratorId = TEXT("dwellings");
	OutSeedRef.Seed        = FString::Printf(TEXT("%lld"), Seed64);
	OutSeedRef.Tags.Reset();
	OutSeedRef.Params.Reset();
	OutSeedRef.Params.Add(TEXT("w"),    FString::FromInt(Cols));
	OutSeedRef.Params.Add(TEXT("h"),    FString::FromInt(Rows));
	OutSeedRef.Params.Add(TEXT("plan"), Plan);
	OutSeedRef.Params.Add(TEXT("tags"), TEXT(""));
	if (!Options.FromMarker.IsEmpty())
	{
		OutSeedRef.Params.Add(TEXT("from"), Options.FromMarker);
	}
	return true;
}

namespace DwellingsPlanEncoder_Internal
{
	/**
	 * Recursive walker. ElementIndex is the running global count of polygon
	 * elements (with matching id) seen so far, so nested sub-collections get
	 * consecutive seed offsets -- mirroring how Watabou would have iterated a
	 * flat buildings list at runtime.
	 */
	static int32 AttachToCollection(
		UWatabouFeaturesCollection* InCollection,
		FName BuildingId,
		const DwellingsPlanEncoder::FOptions& BaseOptions,
		int32& InOutGlobalIndex)
	{
		if (!InCollection) { return 0; }

		int32 Count = 0;
		const int32 N = InCollection->Elements.Num();
		for (int32 I = 0; I < N; ++I)
		{
			const FWatabouFeature& Element = InCollection->Elements[I];
			if (Element.Type != EWatabouFeatureType::Polygon) { continue; }
			if (Element.Id != BuildingId) { continue; }

			DwellingsPlanEncoder::FOptions Options = BaseOptions;
			Options.ElementIndex = InOutGlobalIndex;

			FWatabouSeedRef Ref;
			if (DwellingsPlanEncoder::MakeSeedRefFromPolygon(Element.Coordinates, Ref, Options))
			{
				FWatabouFeatureDetails& Details = InCollection->ElementsDetails.FindOrAdd(I);
				Details.StructValues.Add(TEXT("dwellings"), FInstancedStruct::Make(Ref));
				++Count;
			}
			++InOutGlobalIndex;
		}

		for (UWatabouFeaturesCollection* Sub : InCollection->SubCollections)
		{
			Count += AttachToCollection(Sub, BuildingId, BaseOptions, InOutGlobalIndex);
		}
		return Count;
	}
}

int32 DwellingsPlanEncoder::AttachBuildingSeeds(
	UWatabouAssetBase* InAsset,
	FName BuildingId,
	const FOptions& Options)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DwellingsPlanEncoder::AttachBuildingSeeds);

	using namespace DwellingsPlanEncoder_Internal;

	if (!InAsset || !InAsset->Features) { return 0; }

	int32 GlobalIndex = 0;
	const int32 Attached = AttachToCollection(InAsset->Features, BuildingId, Options, GlobalIndex);
	UE_LOG(LogWatabou, Verbose,
		TEXT("DwellingsPlanEncoder: attached %d dwellings seed refs (id=%s, from=%s, parent=%lld)"),
		Attached, *BuildingId.ToString(),
		Options.FromMarker.IsEmpty() ? TEXT("(none)") : *Options.FromMarker,
		Options.ParentSeed);
	return Attached;
}

void DwellingsPlanEncoder::FOptions::SetParentSeedFromAsset(const UWatabouAssetBase* Asset)
{
	if (!Asset) { return; }
	const FString& Seed = Asset->SourceSeed.Seed;
	if (!Seed.IsEmpty() && Seed.IsNumeric())
	{
		ParentSeed = FCString::Atoi64(*Seed);
	}
}

int32 DwellingsPlanEncoder::RunBuildingsPass(
	UWatabouAssetBase* InAsset,
	const FOptions& Options,
	const TCHAR* LogTag,
	FName BuildingId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DwellingsPlanEncoder::RunBuildingsPass);

	const int32 Linked = AttachBuildingSeeds(InAsset, BuildingId, Options);
	UWatabouAssetBase::AggregateMetadata(InAsset);

	UE_LOG(LogWatabou, Log,
		TEXT("%s: %d building->dwellings seed refs (parent=%lld%s)"),
		LogTag, Linked, Options.ParentSeed,
		Options.ParentSeed < 0 ? TEXT(", centroid-fallback") : TEXT(""));
	return Linked;
}
