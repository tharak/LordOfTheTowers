// Copyright 2026 Timothé Lapetite

#include "WatabouFeaturesCollection.h"

void UWatabouFeaturesCollection::Reset()
{
	Details = FWatabouFeatureDetails{};
	Id = NAME_None;
	Elements.Reset();
	SubCollections.Reset();
	ElementsDetails.Reset();
}

bool UWatabouFeaturesCollection::IsValidCollection() const
{
	if (Elements.Num() > 0) { return true; }
	for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : SubCollections)
	{
		if (Sub && Sub->IsValidCollection()) { return true; }
	}
	return false;
}

UWatabouFeaturesCollection* UWatabouFeaturesCollection::FindOrAddSubCollection(FName InId, UObject* InOuter)
{
	for (const TObjectPtr<UWatabouFeaturesCollection>& Sub : SubCollections)
	{
		if (Sub && Sub->Id == InId) { return Sub; }
	}
	UWatabouFeaturesCollection* New = NewObject<UWatabouFeaturesCollection>(InOuter);
	New->Id = InId;
	New->SetFlags(RF_Transactional);
	SubCollections.Add(New);
	return New;
}
