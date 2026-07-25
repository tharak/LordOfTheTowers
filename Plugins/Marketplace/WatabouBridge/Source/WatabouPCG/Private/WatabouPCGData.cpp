// Copyright 2026 Timothé Lapetite

#include "WatabouPCGData.h"

#include "PCGCommon.h"
#include "PCGData.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataCommon.h"

// File-local helper in a NAMED namespace (anonymous namespaces / file-static functions are
// unsafe under UBT Unity builds -- two files could collide within one translation unit).
namespace WatabouPCGDataInternal
{
	template <typename T>
	void SetDataValueImpl(UPCGData* InData, const FName Name, const T& Value)
	{
		if (!InData || Name.IsNone()) { return; }

		UPCGMetadata* Metadata = InData->MutableMetadata();
		if (!Metadata) { return; }

		// @Data domain: one value per data set, stored on the default + first entry key.
		const FPCGAttributeIdentifier Identifier(Name, EPCGMetadataDomainFlag::Data);

		FPCGMetadataAttribute<T>* Attribute = Metadata->FindOrCreateAttribute<T>(
			Identifier, Value, /*bAllowsInterpolation=*/true, /*bOverrideParent=*/true);

		if (!Attribute) { return; }

		Attribute->SetValue(PCGFirstEntryKey, Value);
		Attribute->SetDefaultValue(Value);
	}
}

namespace WatabouPCG
{
	void SetDataValue(UPCGData* InData, const FName Name, const bool Value) { WatabouPCGDataInternal::SetDataValueImpl<bool>(InData, Name, Value); }
	void SetDataValue(UPCGData* InData, const FName Name, const int32 Value) { WatabouPCGDataInternal::SetDataValueImpl<int32>(InData, Name, Value); }
	void SetDataValue(UPCGData* InData, const FName Name, const int64 Value) { WatabouPCGDataInternal::SetDataValueImpl<int64>(InData, Name, Value); }
	void SetDataValue(UPCGData* InData, const FName Name, const double Value) { WatabouPCGDataInternal::SetDataValueImpl<double>(InData, Name, Value); }
	void SetDataValue(UPCGData* InData, const FName Name, const FName Value) { WatabouPCGDataInternal::SetDataValueImpl<FName>(InData, Name, Value); }
	void SetDataValue(UPCGData* InData, const FName Name, const FString& Value) { WatabouPCGDataInternal::SetDataValueImpl<FString>(InData, Name, Value); }
	void SetDataValue(UPCGData* InData, const FName Name, const FSoftObjectPath& Value) { WatabouPCGDataInternal::SetDataValueImpl<FSoftObjectPath>(InData, Name, Value); }

	void SetClosedLoop(UPCGData* InData, const bool bIsClosed) { SetDataValue(InData, PathAttributes::IsClosed, bIsClosed); }
	void SetIsHole(UPCGData* InData, const bool bIsHole) { SetDataValue(InData, PathAttributes::IsHole, bIsHole); }

	void EnsureMetadataEntries(UPCGBasePointData* InData, const bool bConservative)
	{
		if (!InData) { return; }

		InData->AllocateProperties(EPCGPointNativeProperties::MetadataEntry);

		UPCGMetadata* Metadata = InData->MutableMetadata();
		if (!Metadata) { return; }

		// (true) allocates the writable entry range (mirrors FPointIO::InitializeMetadataEntries).
		TPCGValueRange<int64> Entries = InData->GetMetadataEntryValueRange(true);

		TArray<int64*> KeysNeedingInit;
		KeysNeedingInit.Reserve(Entries.Num());

		if (bConservative)
		{
			// Duplicated data: keep points that already carry a valid local key; (re)initialize
			// only invalid keys and stale parent references.
			const int64 ItemKeyOffset = Metadata->GetItemKeyCountForParent();
			for (int64& Key : Entries)
			{
				if (Key == PCGInvalidEntryKey || Key < ItemKeyOffset)
				{
					KeysNeedingInit.Add(&Key);
				}
			}
		}
		else
		{
			// Fresh data: every point needs a brand-new entry, whatever uninitialized value it
			// currently holds (NewPointData + SetNumPoints(false) leaves garbage, not -1).
			for (int64& Key : Entries)
			{
				KeysNeedingInit.Add(&Key);
			}
		}

		if (KeysNeedingInit.Num() > 0)
		{
			Metadata->AddEntriesInPlace(KeysNeedingInit);
		}
	}
}
