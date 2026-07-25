// Copyright 2026 Timothé Lapetite

#include "WatabouGeneratorSettings.h"

void FWatabouGeneratorSettings::ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const
{
	OutRef.GeneratorId = GeneratorId;
	OutRef.Seed        = Seed;
	OutRef.Tags        = SelectedTags;
	OutRef.Params.Reset();   // subclasses add their params after calling the base
	if (!Name.IsEmpty()) { OutRef.Params.Add(TEXT("name"), Name); }
}

void FWatabouGeneratorSettings::LoadFrom(const FWatabouSeedRef& InRef)
{
	Seed         = InRef.Seed;
	SelectedTags = InRef.Tags;
	// Reset-on-absent: if the browser cleared the name (URL drops "name="), clear it here too --
	// otherwise the stale value is re-emitted and the panel's read-back never reconciles.
	const FString* N = InRef.Params.Find(TEXT("name"));
	Name = N ? *N : FString();
}

void FWatabouGeneratorSettings::RandomizeSeed()
{
	// Match Watabou's URL seeds: non-negative integer. RandRange spans the full int32 range
	// (FMath::Rand alone tops out at ~32767, far below the ~10-digit seeds Watabou emits).
	Seed = FString::FromInt(FMath::RandRange(0, TNumericLimits<int32>::Max() - 1));
}
