// Copyright 2026 Timothé Lapetite
//
// Helpers adapted from PCGExtendedToolkit's PCGExMetaHelpers (MIT, same author),
// copied here so WatabouPCG stays free of any PCGEx dependency.

#include "WatabouPCGMetaHelpers.h"

#include "PCGData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Metadata/PCGAttributePropertySelector.h"

namespace WatabouPCGMeta
{
	bool IsWritableAttributeName(const FName Name)
	{
		if (Name.IsNone()) { return false; }

		FPCGAttributePropertyInputSelector DummySelector;
		if (!DummySelector.Update(Name.ToString())) { return false; }

		return DummySelector.GetSelection() == EPCGAttributePropertySelection::Attribute && DummySelector.IsValid();
	}

	FName SanitizeAttributeName(const FName InName)
	{
		if (InName.IsNone()) { return NAME_None; }

		// Allowed character set mirrors FPCGMetadataAttributeBase::IsValidName:
		// alphanumerics plus space, underscore, hyphen, forward slash.
		const FString Src = InName.ToString();
		FString Out;
		Out.Reserve(Src.Len());

		bool bLastUnderscore = false;
		for (int32 i = 0; i < Src.Len(); ++i)
		{
			const TCHAR C = Src[i];
			const bool bValid =
				FChar::IsAlpha(C) || FChar::IsDigit(C) ||
				C == TEXT(' ') || C == TEXT('_') || C == TEXT('-') || C == TEXT('/');

			if (bValid)
			{
				if (C == TEXT('_'))
				{
					if (bLastUnderscore) { continue; }
					bLastUnderscore = true;
				}
				else
				{
					bLastUnderscore = false;
				}
				Out.AppendChar(C);
			}
			else
			{
				if (bLastUnderscore) { continue; }
				Out.AppendChar(TEXT('_'));
				bLastUnderscore = true;
			}
		}

		// Trim leading/trailing underscores.
		int32 Start = 0;
		while (Start < Out.Len() && Out[Start] == TEXT('_')) { ++Start; }
		int32 End = Out.Len();
		while (End > Start && Out[End - 1] == TEXT('_')) { --End; }
		if (Start > 0 || End < Out.Len()) { Out = Out.Mid(Start, End - Start); }

		return Out.IsEmpty() ? NAME_None : FName(Out);
	}

	FName StripDomainFromName(const FName& InName)
	{
		const FString NameStr = InName.ToString();

		// Check for @{Domain}. pattern.
		if (NameStr.StartsWith(TEXT("@")))
		{
			const int32 DotIndex = NameStr.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
			if (DotIndex != INDEX_NONE) { return FName(NameStr.Mid(DotIndex + 1)); }
		}

		return InName;
	}

	FPCGAttributeIdentifier MakeDataIdentifier(const FName& BaseName)
	{
		return FPCGAttributeIdentifier(StripDomainFromName(BaseName), PCGMetadataDomainID::Data);
	}

	FPCGAttributeIdentifier MakeElementIdentifier(const FName& BaseName)
	{
		return FPCGAttributeIdentifier(StripDomainFromName(BaseName), PCGMetadataDomainID::Elements);
	}

	bool HasAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata) { return false; }
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain)) { return false; }
		return InMetadata->HasAttribute(Identifier);
	}

	bool HasAttribute(const UPCGData* InData, const FPCGAttributeIdentifier& Identifier)
	{
		return InData ? HasAttribute(InData->ConstMetadata(), Identifier) : false;
	}
}
