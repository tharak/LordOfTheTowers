// Copyright 2026 Timothé Lapetite

#include "PerilousShoresSettings.h"

void FPerilousShoresSettings::ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const
{
	FWatabouGeneratorSettings::ToSeedRef(GeneratorId, OutRef);
	// Match the bundle's updateURL: emit only when different from the bundle default (w/h 1200,
	// hexes 0), so the panel's browser-sync equality check (HandleUrlChanged) stays clean.
	if (Width  != 1200) { OutRef.Params.Add(TEXT("w"), FString::FromInt(Width)); }
	if (Height != 1200) { OutRef.Params.Add(TEXT("h"), FString::FromInt(Height)); }
	if (Hexes != EWatabouPerilousHexes::Off)
	{
		OutRef.Params.Add(TEXT("hexes"), FString::FromInt(static_cast<int32>(Hexes)));
	}
}

void FPerilousShoresSettings::LoadFrom(const FWatabouSeedRef& InRef)
{
	FWatabouGeneratorSettings::LoadFrom(InRef);
	// Reset-on-absent: a param the browser omits (because it's at the bundle default) returns the
	// field to its default, so read-back reconciles instead of keeping a stale non-default value.
	// Defaults come from a default-constructed instance (single source of truth = the .h initializers).
	const FPerilousShoresSettings D;
	const FString* W  = InRef.Params.Find(TEXT("w"));
	const FString* H  = InRef.Params.Find(TEXT("h"));
	const FString* Hx = InRef.Params.Find(TEXT("hexes"));
	Width  = W ? FCString::Atoi(**W) : D.Width;
	Height = H ? FCString::Atoi(**H) : D.Height;
	Hexes  = Hx ? static_cast<EWatabouPerilousHexes>(FMath::Clamp(FCString::Atoi(**Hx), 0, 3)) : D.Hexes;
}
