// Copyright 2026 Timothé Lapetite

#include "VillageGeneratorSettings.h"

void FVillageGeneratorSettings::ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const
{
	FWatabouGeneratorSettings::ToSeedRef(GeneratorId, OutRef);
	// Bundle uses "width"/"height" (not "w"/"h") and omits each at its 500 default; "pop" omitted at 0.
	if (Width      != 500) { OutRef.Params.Add(TEXT("width"),  FString::FromInt(Width)); }
	if (Height     != 500) { OutRef.Params.Add(TEXT("height"), FString::FromInt(Height)); }
	if (Population != 0)   { OutRef.Params.Add(TEXT("pop"),    FString::FromInt(Population)); }
}

void FVillageGeneratorSettings::LoadFrom(const FWatabouSeedRef& InRef)
{
	FWatabouGeneratorSettings::LoadFrom(InRef);
	// Reset-on-absent (see PerilousShoresSettings.cpp): omitted params return to their defaults so
	// the panel's browser read-back reconciles instead of keeping a stale non-default value.
	const FVillageGeneratorSettings D;
	const FString* W = InRef.Params.Find(TEXT("width"));
	const FString* H = InRef.Params.Find(TEXT("height"));
	const FString* P = InRef.Params.Find(TEXT("pop"));
	Width      = W ? FCString::Atoi(**W) : D.Width;
	Height     = H ? FCString::Atoi(**H) : D.Height;
	Population = P ? FCString::Atoi(**P) : D.Population;
}
