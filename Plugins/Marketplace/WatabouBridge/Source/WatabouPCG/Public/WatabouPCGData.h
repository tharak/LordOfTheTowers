// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

class UPCGData;
class UPCGBasePointData;

/**
 * Local "PCGEx-friendly" data-writing helpers.
 *
 * WatabouPCG deliberately does not depend on PCGExtendedToolkit, but its output
 * is meant to flow straight into PCGEx path / cluster nodes. The only conventions
 * those nodes rely on for path data are two @Data-domain bool attributes --
 * "IsClosed" and "IsHole" -- plus the one-data-per-path layout. We reproduce the
 * attribute names here (kept in lockstep with PCGExPaths::Labels) so emitted paths
 * are recognized downstream without linking PCGEx.
 *
 * All writes target the @Data metadata domain (one value per data set), matching
 * how PCGEx stores per-path flags and how the legacy Watabou node stored details.
 */
namespace WatabouPCG
{
	namespace PathAttributes
	{
		/** Closed-loop flag. Mirror of PCGExPaths::Labels::ClosedLoopIdentifier ("IsClosed", @Data). */
		const FName IsClosed = FName(TEXT("IsClosed"));

		/** Hole flag for polygon rings. Mirror of PCGExPaths::Labels::HoleIdentifier ("IsHole", @Data). */
		const FName IsHole = FName(TEXT("IsHole"));
	}

	/** Create (or find) a @Data-domain attribute and set its single data value + default. */
	WATABOUPCG_API void SetDataValue(UPCGData* InData, FName Name, bool Value);
	WATABOUPCG_API void SetDataValue(UPCGData* InData, FName Name, int32 Value);
	WATABOUPCG_API void SetDataValue(UPCGData* InData, FName Name, int64 Value);
	WATABOUPCG_API void SetDataValue(UPCGData* InData, FName Name, double Value);
	WATABOUPCG_API void SetDataValue(UPCGData* InData, FName Name, FName Value);
	WATABOUPCG_API void SetDataValue(UPCGData* InData, FName Name, const FString& Value);
	WATABOUPCG_API void SetDataValue(UPCGData* InData, FName Name, const FSoftObjectPath& Value);

	/** Tag this data as a closed (true) or open (false) path. Recognized by PCGEx path nodes. */
	WATABOUPCG_API void SetClosedLoop(UPCGData* InData, bool bIsClosed);

	/** Tag this data as a polygon hole ring. Recognized by PCGEx path nodes. */
	WATABOUPCG_API void SetIsHole(UPCGData* InData, bool bIsHole);

	/**
	 * Ensure every point in InData has a valid, writable metadata entry key, so per-point
	 * attribute writes don't hit uninitialized entries. Mirrors FPointIO::InitializeMetadataEntries.
	 *
	 * bConservative=true  : preserve points that already carry a valid key (duplicated data),
	 *                       (re)initializing only invalid keys / stale parent references.
	 * bConservative=false : fresh data -- give every point a brand-new entry regardless of its
	 *                       current (uninitialized) value. Use for newly-created point data, whose
	 *                       entries are garbage (not PCGInvalidEntryKey) and so are NOT caught by
	 *                       the conservative pass.
	 */
	WATABOUPCG_API void EnsureMetadataEntries(UPCGBasePointData* InData, bool bConservative = true);
}
