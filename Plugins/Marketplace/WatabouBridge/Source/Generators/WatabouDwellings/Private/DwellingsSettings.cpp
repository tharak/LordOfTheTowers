// Copyright 2026 Timothé Lapetite

#include "DwellingsSettings.h"

void FDwellingsSettings::ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const
{
	FWatabouGeneratorSettings::ToSeedRef(GeneratorId, OutRef);
	// floors omitted at 0 (auto); rooms omitted when Standard -- both match the bundle's updateURL.
	if (Floors > 0) { OutRef.Params.Add(TEXT("floors"), FString::FromInt(Floors)); }
	switch (Rooms)
	{
	case EWatabouDwellingsRooms::Gothic: OutRef.Params.Add(TEXT("rooms"), TEXT("gothic")); break;
	case EWatabouDwellingsRooms::Tavern: OutRef.Params.Add(TEXT("rooms"), TEXT("tavern")); break;
	default:                             break;   // Standard -> no "rooms" param
	}
}

void FDwellingsSettings::LoadFrom(const FWatabouSeedRef& InRef)
{
	FWatabouGeneratorSettings::LoadFrom(InRef);
	// Reset-on-absent (see PerilousShoresSettings.cpp): omitted params return to their defaults so
	// the panel's browser read-back reconciles instead of keeping a stale non-default value.
	const FDwellingsSettings D;
	const FString* F = InRef.Params.Find(TEXT("floors"));
	Floors = F ? FCString::Atoi(**F) : D.Floors;

	if (const FString* R = InRef.Params.Find(TEXT("rooms")))
	{
		if      (*R == TEXT("gothic")) { Rooms = EWatabouDwellingsRooms::Gothic; }
		else if (*R == TEXT("tavern")) { Rooms = EWatabouDwellingsRooms::Tavern; }
		else                           { Rooms = EWatabouDwellingsRooms::Standard; }
	}
	else { Rooms = D.Rooms; }
}
