// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorSettings.h"
#include "DwellingsSettings.generated.h"

/**
 * Room layout style (URL "rooms"). Standard is the implicit default -- the bundle treats any absent
 * or unrecognized value as the standard layout -- so it is emitted as no "rooms" param at all.
 */
UENUM(BlueprintType)
enum class EWatabouDwellingsRooms : uint8
{
	Standard UMETA(DisplayName = "Standard"),
	Gothic   UMETA(DisplayName = "Gothic"),
	Tavern   UMETA(DisplayName = "Tavern"),
};

/**
 * Import settings for standalone Dwellings generation. URL params beyond the shared seed/name/tags:
 * floors, rooms. The footprint params the recursive importer supplies via DwellingsPlanEncoder
 * (plan / w / h / entrance) are intentionally not modeled here -- they describe a fixed footprint
 * grid, not a hand-set option -- and "view" is a browser-only initial-camera toggle that does not
 * affect the exported dwelling.
 */
USTRUCT()
struct FDwellingsSettings : public FWatabouGeneratorSettings
{
	GENERATED_BODY()

	/** Number of floors (URL "floors"). 0 => auto (biased by the low/tall tags); omitted from the URL when 0. */
	UPROPERTY(EditAnywhere, Category = "Dwellings", meta = (ClampMin = "0"))
	int32 Floors = 0;

	/** Room layout style (URL "rooms"). Standard emits no param. */
	UPROPERTY(EditAnywhere, Category = "Dwellings")
	EWatabouDwellingsRooms Rooms = EWatabouDwellingsRooms::Standard;

	virtual void ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const override;
	virtual void LoadFrom(const FWatabouSeedRef& InRef) override;
};
