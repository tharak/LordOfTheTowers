// Copyright 2026 Timothé Lapetite

#include "CityGeneratorSettings.h"

namespace CityGeneratorSettings
{
	// File-local helpers in a named namespace (Unity-build safe -- never anonymous / static free funcs).
	FString StyleToToken(const EWatabouCityStyle Style)
	{
		switch (Style)
		{
		case EWatabouCityStyle::Ink:        return TEXT("ink");
		case EWatabouCityStyle::BlackWhite: return TEXT("bw");
		case EWatabouCityStyle::Vivid:      return TEXT("vivid");
		case EWatabouCityStyle::Natural:    return TEXT("natural");
		case EWatabouCityStyle::Modern:     return TEXT("modern");
		default:                            return TEXT("default");
		}
	}

	EWatabouCityStyle StyleFromToken(const FString& Token)
	{
		if (Token == TEXT("ink"))     { return EWatabouCityStyle::Ink; }
		if (Token == TEXT("bw"))      { return EWatabouCityStyle::BlackWhite; }
		if (Token == TEXT("vivid"))   { return EWatabouCityStyle::Vivid; }
		if (Token == TEXT("natural")) { return EWatabouCityStyle::Natural; }
		if (Token == TEXT("modern"))  { return EWatabouCityStyle::Modern; }
		return EWatabouCityStyle::Default;
	}
}

void FCityGeneratorSettings::ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const
{
	FWatabouGeneratorSettings::ToSeedRef(GeneratorId, OutRef);

	OutRef.Params.Add(TEXT("size"), FString::FromInt(Size));

	// Map features -- always written as "1"/"0" (the bundle reads them via getFlag with defaults).
	OutRef.Params.Add(TEXT("citadel"),      bCitadel     ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("urban_castle"), bUrbanCastle ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("plaza"),        bPlaza       ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("temple"),       bTemple      ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("walls"),        bWalls       ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("shantytown"),   bShantytown  ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("coast"),        bCoast       ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("river"),        bRiver       ? TEXT("1") : TEXT("0"));
	OutRef.Params.Add(TEXT("greens"),       bGreens      ? TEXT("1") : TEXT("0"));

	// hub XOR gates: Hub writes the flag; Auto/Custom write the gate count (-1 = auto-compute).
	switch (GateMode)
	{
	case EWatabouCityGates::Hub:    OutRef.Params.Add(TEXT("hub"),   TEXT("1")); break;
	case EWatabouCityGates::Custom: OutRef.Params.Add(TEXT("gates"), FString::FromInt(GateCount)); break;
	default:                        OutRef.Params.Add(TEXT("gates"), TEXT("-1")); break;   // Auto
	}

	// Coast direction is only meaningful (and only written by the bundle) when coast is on.
	if (bCoast) { OutRef.Params.Add(TEXT("sea"), FString::SanitizeFloat(SeaDirection)); }

	// Population omitted when auto (0); style omitted at the implicit Default palette.
	if (Population != 0) { OutRef.Params.Add(TEXT("population"), FString::FromInt(Population)); }
	if (Style != EWatabouCityStyle::Default)
	{
		OutRef.Params.Add(TEXT("style"), CityGeneratorSettings::StyleToToken(Style));
	}
}

void FCityGeneratorSettings::LoadFrom(const FWatabouSeedRef& InRef)
{
	FWatabouGeneratorSettings::LoadFrom(InRef);

	// Reset-on-absent (see PerilousShoresSettings.cpp): a param the browser omits returns the field
	// to its default, so the panel's read-back reconciles instead of keeping a stale value. Defaults
	// come from a default-constructed instance (single source of truth = the .h initializers).
	const FCityGeneratorSettings D;
	const FString* P = nullptr;

	P = InRef.Params.Find(TEXT("size"));         Size       = P ? FCString::Atoi(**P) : D.Size;
	P = InRef.Params.Find(TEXT("population"));   Population = P ? FCString::Atoi(**P) : D.Population;

	P = InRef.Params.Find(TEXT("citadel"));      bCitadel     = P ? (*P != TEXT("0")) : D.bCitadel;
	P = InRef.Params.Find(TEXT("urban_castle")); bUrbanCastle = P ? (*P != TEXT("0")) : D.bUrbanCastle;
	P = InRef.Params.Find(TEXT("plaza"));        bPlaza       = P ? (*P != TEXT("0")) : D.bPlaza;
	P = InRef.Params.Find(TEXT("temple"));       bTemple      = P ? (*P != TEXT("0")) : D.bTemple;
	P = InRef.Params.Find(TEXT("walls"));        bWalls       = P ? (*P != TEXT("0")) : D.bWalls;
	P = InRef.Params.Find(TEXT("shantytown"));   bShantytown  = P ? (*P != TEXT("0")) : D.bShantytown;
	P = InRef.Params.Find(TEXT("coast"));        bCoast       = P ? (*P != TEXT("0")) : D.bCoast;
	P = InRef.Params.Find(TEXT("river"));        bRiver       = P ? (*P != TEXT("0")) : D.bRiver;
	P = InRef.Params.Find(TEXT("greens"));       bGreens      = P ? (*P != TEXT("0")) : D.bGreens;

	// hub takes precedence; otherwise gates (>=0 = explicit count, -1 = auto); neither present -> default.
	const FString* Hub = InRef.Params.Find(TEXT("hub"));
	const FString* Ga  = InRef.Params.Find(TEXT("gates"));
	if (Hub && *Hub != TEXT("0"))
	{
		GateMode  = EWatabouCityGates::Hub;
		GateCount = D.GateCount;
	}
	else if (Ga)
	{
		const int32 G = FCString::Atoi(**Ga);
		if (G >= 0) { GateMode = EWatabouCityGates::Custom; GateCount = G; }
		else        { GateMode = EWatabouCityGates::Auto;   GateCount = D.GateCount; }
	}
	else
	{
		GateMode  = D.GateMode;
		GateCount = D.GateCount;
	}

	P = InRef.Params.Find(TEXT("sea"));   SeaDirection = P ? FCString::Atof(**P) : D.SeaDirection;
	P = InRef.Params.Find(TEXT("style")); Style        = P ? CityGeneratorSettings::StyleFromToken(*P) : D.Style;
}
