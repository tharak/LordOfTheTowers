// Copyright 2026 Timothé Lapetite

#include "WatabouForward.h"

#include "WatabouFeatureMap.h"   // FeatureMapAttributes::Key (bookkeeping exclusion)
#include "WatabouPCGData.h"      // PathAttributes::IsClosed / IsHole (bookkeeping exclusion)
#include "WatabouPCGMetaHelpers.h" // WatabouPCGMeta::ReadDataValue (@Data value read, parent-chain walk)

#include "PCGData.h"
#include "Data/PCGBasePointData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"   // PCGMetadataAttribute::CallbackWithRightType
#include "Metadata/PCGMetadataCommon.h"

// File-local helpers in a NAMED namespace (anonymous namespaces / file-static functions are unsafe
// under UBT Unity builds -- two files could collide within one translation unit).
namespace WatabouForwardInternal
{
	/** Split a comma-separated wildcard list into trimmed, non-empty patterns. */
	void ParsePatterns(const FString& In, TArray<FString>& Out)
	{
		if (In.IsEmpty()) { return; }
		In.ParseIntoArray(Out, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Pattern : Out) { Pattern.TrimStartAndEndInline(); }
		Out.RemoveAll([](const FString& Pattern) { return Pattern.IsEmpty(); });
	}

	/** Include/exclude wildcard test: exclude wins; empty include = match all. */
	bool PassesFilter(const FName Name, const TArray<FString>& Includes, const TArray<FString>& Excludes)
	{
		const FString NameStr = Name.ToString();
		for (const FString& Pattern : Excludes) { if (NameStr.MatchesWildcard(Pattern)) { return false; } }
		if (Includes.IsEmpty()) { return true; }
		for (const FString& Pattern : Includes) { if (NameStr.MatchesWildcard(Pattern)) { return true; } }
		return false;
	}

	/** Watabou bookkeeping attributes that must never be forwarded (would clobber the target's own key / path flags). */
	bool IsBookkeeping(const FName Name)
	{
		return Name == WatabouPCG::FeatureMapAttributes::Key
			|| Name == WatabouPCG::PathAttributes::IsClosed
			|| Name == WatabouPCG::PathAttributes::IsHole;
	}

}

FWatabouForwardHandler::FWatabouForwardHandler(const FWatabouForwardDetails& InDetails, const UPCGBasePointData* InSource)
{
	using namespace WatabouForwardInternal;

	if (!InDetails.bEnabled || !InSource) { return; }

	const UPCGMetadata* Metadata = InSource->ConstMetadata();
	if (!Metadata) { return; }

	TArray<FString> Includes;
	TArray<FString> Excludes;
	ParsePatterns(InDetails.Include, Includes);
	ParsePatterns(InDetails.Exclude, Excludes);

	TArray<FPCGAttributeIdentifier> Identifiers;
	TArray<EPCGMetadataTypes> Types;
	Metadata->GetAllAttributes(Identifiers, Types);

	const int32 NumAttributes = Identifiers.Num();
	Identities.Reserve(NumAttributes);

	for (int32 i = 0; i < NumAttributes; i++)
	{
		const FPCGAttributeIdentifier& Identifier = Identifiers[i];
		if (IsBookkeeping(Identifier.Name)) { continue; }
		if (!PassesFilter(Identifier.Name, Includes, Excludes)) { continue; }

		const FPCGMetadataAttributeBase* Attribute = Metadata->GetConstAttribute(Identifier);
		if (!Attribute) { continue; }

		FForwarded& Fwd = Identities.Emplace_GetRef();
		Fwd.Identifier = Identifier;
		Fwd.Type = Types[i];
		Fwd.bAllowsInterpolation = Attribute->AllowsInterpolation();
		Fwd.bDataDomain = Identifier.MetadataDomain.Flag == EPCGMetadataDomainFlag::Data;
	}

	// Only retain the source if there is anything to forward.
	if (!Identities.IsEmpty()) { Source = InSource; }
}

void FWatabouForwardHandler::ForwardToData(const int32 SourceIndex, UPCGMetadata* TargetMetadata, const bool bDataDomainOnly) const
{
	if (!Source || !TargetMetadata || Identities.IsEmpty()) { return; }

	const UPCGMetadata* SourceMetadata = Source->ConstMetadata();
	if (!SourceMetadata) { return; }

	// Per-point read needs the source point's metadata entry; @Data-only reads ignore it.
	const int64 SourceEntry = (!bDataDomainOnly && SourceIndex >= 0) ? Source->GetMetadataEntry(SourceIndex) : PCGInvalidEntryKey;

	for (const FForwarded& Fwd : Identities)
	{
		if (bDataDomainOnly && !Fwd.bDataDomain) { continue; } // @Data stamp: per-point seed attrs are dropped

		PCGMetadataAttribute::CallbackWithRightType(static_cast<uint16>(Fwd.Type), [&](auto DummyValue) -> void
		{
			using T = decltype(DummyValue);

			const FPCGMetadataAttribute<T>* Src = static_cast<const FPCGMetadataAttribute<T>*>(SourceMetadata->GetConstAttribute(Fwd.Identifier));
			if (!Src) { return; }

			// @Data source attr -> its single data value (broadcast); per-point attr -> the source point's value.
			const T Value = Fwd.bDataDomain ? WatabouPCGMeta::ReadDataValue<T>(Src) : Src->GetValueFromItemKey(SourceEntry);

			// Write onto the target's @Data domain (mirrors WatabouPCG::SetDataValue).
			const FPCGAttributeIdentifier TargetId(Fwd.Identifier.Name, EPCGMetadataDomainFlag::Data);
			FPCGMetadataAttribute<T>* Target = TargetMetadata->FindOrCreateAttribute<T>(TargetId, Value, Fwd.bAllowsInterpolation, /*bOverrideParent=*/true);
			if (Target)
			{
				Target->SetValue(PCGFirstEntryKey, Value);
				Target->SetDefaultValue(Value);
			}
		});
	}
}
