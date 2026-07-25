// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorSettings.h"
#include "PerilousShoresSettings.generated.h"

/**
 * Hex-grid tilt mode (URL "hexes"). 0 (Off) is the implicit default -- an organic mesh with no hex
 * tilt; the in-app tilt menu only offers the three hex modes (1..3).
 */
UENUM(BlueprintType)
enum class EWatabouPerilousHexes : uint8
{
	Off          = 0 UMETA(DisplayName = "Off (organic mesh)"),
	PointyTopped = 1 UMETA(DisplayName = "Pointy topped"),
	FlatTopped   = 2 UMETA(DisplayName = "Flat topped"),
	Warped       = 3 UMETA(DisplayName = "Warped"),
};

/**
 * Import settings for Perilous Shores. URL params beyond the shared seed/name/tags: w, h, hexes.
 * w/h/hexes are emitted only when they differ from the bundle defaults (1200/1200/Off), matching
 * the bundle's own updateURL.
 */
USTRUCT()
struct FPerilousShoresSettings : public FWatabouGeneratorSettings
{
	GENERATED_BODY()

	/** Map width in px (URL "w"). Bundle clamps to 200..4000; omitted from the URL at the 1200 default. */
	UPROPERTY(EditAnywhere, Category = "Perilous Shores", meta = (ClampMin = "200", ClampMax = "4000"))
	int32 Width = 1200;

	/** Map height in px (URL "h"). Bundle clamps to 200..4000; omitted from the URL at the 1200 default. */
	UPROPERTY(EditAnywhere, Category = "Perilous Shores", meta = (ClampMin = "200", ClampMax = "4000"))
	int32 Height = 1200;

	/** Hex-grid tilt mode (URL "hexes"). Omitted from the URL when Off. */
	UPROPERTY(EditAnywhere, Category = "Perilous Shores")
	EWatabouPerilousHexes Hexes = EWatabouPerilousHexes::Off;

	virtual void ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const override;
	virtual void LoadFrom(const FWatabouSeedRef& InRef) override;
};
