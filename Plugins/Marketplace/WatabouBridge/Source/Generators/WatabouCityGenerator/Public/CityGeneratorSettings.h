// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorSettings.h"
#include "CityGeneratorSettings.generated.h"

/**
 * Color palette (URL "style"). Default is the implicit palette -- the bundle applies its built-in
 * palette when no "style" is given -- so it is emitted as no "style" param at all.
 */
UENUM(BlueprintType)
enum class EWatabouCityStyle : uint8
{
	Default    UMETA(DisplayName = "Default"),
	Ink        UMETA(DisplayName = "Ink"),
	BlackWhite UMETA(DisplayName = "Black & White"),
	Vivid      UMETA(DisplayName = "Vivid"),
	Natural    UMETA(DisplayName = "Natural"),
	Modern     UMETA(DisplayName = "Modern"),
};

/**
 * City gate mode (URL "hub"/"gates"). The bundle writes either hub=1 (every wall edge becomes a
 * gate) or gates=N (an explicit count; -1 = auto-compute), never both.
 */
UENUM(BlueprintType)
enum class EWatabouCityGates : uint8
{
	Auto   UMETA(DisplayName = "Auto"),     // gates=-1 (engine picks the count)
	Hub    UMETA(DisplayName = "Hub"),      // hub=1
	Custom UMETA(DisplayName = "Custom"),   // gates=GateCount
};

/**
 * Import settings for the Medieval Fantasy City Generator. City has no tag system; its map features
 * are individual boolean URL flags instead (rendered as checkboxes in the details view). size and
 * seed are required by the bundle (both must be > 0). Flags and size are always emitted (the bundle
 * writes them unconditionally); style is omitted at Default, sea only when Coast is on, and
 * population only when non-zero.
 *
 * Excluded: "export" (an auto-download trigger -- we have our own import flow) and "preview" (an
 * internal simplified-render mode).
 */
USTRUCT()
struct FCityGeneratorSettings : public FWatabouGeneratorSettings
{
	GENERATED_BODY()

	/** Number of wards/blocks (URL "size"); higher = more districts and far more features. */
	UPROPERTY(EditAnywhere, Category = "City", meta = (ClampMin = "6", ClampMax = "80"))
	int32 Size = 25;

	/** Displayed population (URL "population"). 0 => auto-derived from size; omitted from the URL when 0. */
	UPROPERTY(EditAnywhere, Category = "City", meta = (ClampMin = "0"))
	int32 Population = 0;

	/** Color palette (URL "style"). Default emits no param. */
	UPROPERTY(EditAnywhere, Category = "City")
	EWatabouCityStyle Style = EWatabouCityStyle::Default;

	/** City has a citadel (URL "citadel"). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bCitadel = true;

	/** Castle is placed inside the city walls (URL "urban_castle", the "inner" field). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bUrbanCastle = false;

	/** City has a market plaza (URL "plaza"). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bPlaza = true;

	/** City has a temple (URL "temple"). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bTemple = true;

	/** City has defensive walls (URL "walls"). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bWalls = true;

	/** City has a shanty town (URL "shantytown", the "shanty" field). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bShantytown = false;

	/** City is on a coast (URL "coast"); enables the Sea Direction below. */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bCoast = true;

	/** City has a river (URL "river"). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bRiver = false;

	/** Parks / green areas (URL "greens"). */
	UPROPERTY(EditAnywhere, Category = "City|Features")
	bool bGreens = false;

	/** Gate mode (URL "hub"/"gates"). */
	UPROPERTY(EditAnywhere, Category = "City|Gates")
	EWatabouCityGates GateMode = EWatabouCityGates::Auto;

	/** Explicit gate count (URL "gates"), used only when Gate Mode is Custom. */
	UPROPERTY(EditAnywhere, Category = "City|Gates", meta = (ClampMin = "0", EditCondition = "GateMode == EWatabouCityGates::Custom", EditConditionHides))
	int32 GateCount = 0;

	/** Coast direction angle in radians (URL "sea"). Only emitted when Coast is enabled. */
	UPROPERTY(EditAnywhere, Category = "City", meta = (EditCondition = "bCoast", EditConditionHides))
	float SeaDirection = 0.f;

	virtual void ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const override;
	virtual void LoadFrom(const FWatabouSeedRef& InRef) override;
};
