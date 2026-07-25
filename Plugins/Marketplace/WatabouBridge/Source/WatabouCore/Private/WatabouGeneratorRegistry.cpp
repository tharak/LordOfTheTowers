// Copyright 2026 Timothé Lapetite

#include "WatabouGeneratorRegistry.h"
#include "WatabouGeneratorInfo.h"
#include "WatabouBridge.h"

FWatabouGeneratorRegistry& FWatabouGeneratorRegistry::Get()
{
	static FWatabouGeneratorRegistry Instance;
	return Instance;
}

void FWatabouGeneratorRegistry::Register(TSharedRef<FWatabouGeneratorInfo> Info)
{
	check(IsInGameThread());

	const FString Id = Info->Id;
	if (FindById(Id).IsValid())
	{
		UE_LOG(LogWatabou, Warning, TEXT("Generator '%s' already registered -- ignoring duplicate"), *Id);
		return;
	}

	Generators.Add(Info);
	UE_LOG(LogWatabou, Log, TEXT("Registered generator: %s (%s)"), *Info->DisplayName, *Id);
}

void FWatabouGeneratorRegistry::Unregister(const FString& Id)
{
	check(IsInGameThread());

	const int32 NumRemoved = Generators.RemoveAll(
		[&Id](const TSharedRef<FWatabouGeneratorInfo>& Info) { return Info->Id == Id; });

	if (NumRemoved > 0)
	{
		UE_LOG(LogWatabou, Log, TEXT("Unregistered generator: %s"), *Id);
	}
}

TSharedPtr<FWatabouGeneratorInfo> FWatabouGeneratorRegistry::FindById(const FString& Id) const
{
	for (const TSharedRef<FWatabouGeneratorInfo>& Info : Generators)
	{
		if (Info->Id == Id) { return Info; }
	}
	return nullptr;
}
