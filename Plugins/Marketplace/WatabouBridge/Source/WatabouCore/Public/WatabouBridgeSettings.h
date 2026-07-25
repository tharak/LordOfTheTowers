// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "WatabouDwellingLimits.h"
#include "WatabouBridgeSettings.generated.h"

/**
 * Per-generator override of the shared dwelling cell metrics. The divisor and the cap toggle
 * independently: when a toggle is off, the matching shared UWatabouBridgeSettings value is used.
 * They're all dwellings, so the shared value is the common case; an override is for a generator
 * whose source coordinate scale (or desired fill) genuinely differs.
 */
USTRUCT()
struct WATABOUCORE_API FWatabouDwellingTuning
{
	GENERATED_BODY()

	/** Use this generator's own CellSizeDivisor instead of the shared one. */
	UPROPERTY(EditAnywhere, Category = "Dwellings", meta = (InlineEditConditionToggle))
	bool bOverrideCellSizeDivisor = false;

	/** Per-generator cell size divisor. Active only when the toggle is on. */
	UPROPERTY(EditAnywhere, Category = "Dwellings", meta = (EditCondition = "bOverrideCellSizeDivisor", ClampMin = "0.1"))
	double CellSizeDivisor = 4.0;

	/** Use this generator's own MaxCells instead of the shared one. */
	UPROPERTY(EditAnywhere, Category = "Dwellings", meta = (InlineEditConditionToggle))
	bool bOverrideMaxCells = false;

	/** Per-generator per-axis cell cap. Active only when the toggle is on; clamped to the bundle ceiling. */
	UPROPERTY(EditAnywhere, Category = "Dwellings", meta = (EditCondition = "bOverrideMaxCells", ClampMin = "1", ClampMax = "64"))
	int32 MaxCells = 64;
};

/**
 * Project Settings -> Plugins -> Watabou Bridge: import-time knobs for the Watabou -> PCG bridge.
 *
 * These are read while an asset is PARSED (imported), so a change takes effect on the next (re)import --
 * it does NOT retroactively alter assets already on disk. Re-import to apply.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Watabou Bridge"))
class WATABOUCORE_API UWatabouBridgeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWatabouBridgeSettings();

	/** Surfaces this under Project Settings -> Plugins (rather than the default Game category). */
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	/**
	 * Shared cell size divisor, in the source generator's coordinate units, used when inferring a Dwellings
	 * plan from each building footprint: it divides the footprint's oriented-box length to choose the cell
	 * counts, so bigger -> bigger / fewer cells. Every dwelling-emitting generator (City, Village, Urban
	 * Places, Neighbourhood) uses this unless its per-generator override below is enabled. Import-time only.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Dwellings", meta = (ClampMin = "0.1", DisplayName = "Cell Size Divisor"))
	double CellSizeDivisor = 2.0;

	/**
	 * Shared per-axis cell cap for inferred Dwellings plans. Bounds how many cells a large building fills;
	 * too small a divisor against a big footprint overruns this and the building under-fills. The patched
	 * bundle accepts up to WatabouDwellingLimits::BundleMaxCells and resolved values are clamped to it.
	 * Used by every dwelling-emitting generator unless its per-generator override below is enabled.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Dwellings", meta = (ClampMin = "1", ClampMax = "64", DisplayName = "Max Cells"))
	int32 MaxCells = 11;

	/** Per-generator overrides of the shared metrics above. Defaults preserve historical behavior. */
	UPROPERTY(config, EditAnywhere, Category = "Dwellings", meta = (DisplayName = "City"))
	FWatabouDwellingTuning City;

	UPROPERTY(config, EditAnywhere, Category = "Dwellings", meta = (DisplayName = "Village"))
	FWatabouDwellingTuning Village;

	UPROPERTY(config, EditAnywhere, Category = "Dwellings", meta = (DisplayName = "Urban Places"))
	FWatabouDwellingTuning UrbanPlaces;

	UPROPERTY(config, EditAnywhere, Category = "Dwellings", meta = (DisplayName = "Neighbourhood"))
	FWatabouDwellingTuning Neighbourhood;

	/** Effective cell size divisor for a generator: its override when enabled, else the shared value. */
	double ResolveCellSizeDivisor(const FWatabouDwellingTuning& Tuning) const
	{
		return Tuning.bOverrideCellSizeDivisor ? Tuning.CellSizeDivisor : CellSizeDivisor;
	}

	/**
	 * Effective per-axis cell cap for a generator, clamped to the bundle ceiling so we never emit a plan
	 * the bundle would reject (and substitute a random shape for). Override when enabled, else the shared value.
	 */
	int32 ResolveMaxCells(const FWatabouDwellingTuning& Tuning) const
	{
		const int32 Value = Tuning.bOverrideMaxCells ? Tuning.MaxCells : MaxCells;
		return FMath::Clamp(Value, 1, WatabouDwellingLimits::BundleMaxCells);
	}
};
