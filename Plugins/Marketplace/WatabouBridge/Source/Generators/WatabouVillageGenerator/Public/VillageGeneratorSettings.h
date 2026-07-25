// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouGeneratorSettings.h"
#include "VillageGeneratorSettings.generated.h"

/**
 * Import settings for the Village Generator. URL params beyond the shared seed/name/tags: width,
 * height, pop. Each is emitted only when it differs from the bundle default (width/height 500,
 * pop 0), matching the bundle's own updateURL.
 *
 * Not modeled (out of this pass): "trees" (a secondary tree-placement RNG seed that defaults to the
 * main seed), "marked" (post-hoc building-numbering indices), and "style" (opaque serialized palette
 * data, not a named preset).
 */
USTRUCT()
struct FVillageGeneratorSettings : public FWatabouGeneratorSettings
{
	GENERATED_BODY()

	/** Map width in px (URL "width"). Omitted from the URL at the 500 default. */
	UPROPERTY(EditAnywhere, Category = "Village", meta = (ClampMin = "200", ClampMax = "4000"))
	int32 Width = 500;

	/** Map height in px (URL "height"). Omitted from the URL at the 500 default. */
	UPROPERTY(EditAnywhere, Category = "Village", meta = (ClampMin = "200", ClampMax = "4000"))
	int32 Height = 500;

	/** Displayed population (URL "pop"). 0 => auto-derived from size; omitted from the URL when 0. */
	UPROPERTY(EditAnywhere, Category = "Village", meta = (ClampMin = "0"))
	int32 Population = 0;

	virtual void ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const override;
	virtual void LoadFrom(const FWatabouSeedRef& InRef) override;
};
