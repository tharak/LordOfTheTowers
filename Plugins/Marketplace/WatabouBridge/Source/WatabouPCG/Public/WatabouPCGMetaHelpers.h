// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataCommon.h"

class UPCGData;

/**
 * Small home for metadata / attribute helpers adapted from PCGExMetaHelpers (MIT, same
 * author). WatabouPCG can't depend on PCGExtendedToolkit, so the handful of helpers we
 * reuse are copied here verbatim. Grow this as more are needed rather than re-deriving
 * them ad-hoc at call sites.
 */
namespace WatabouPCGMeta
{
	/** True if Name round-trips to a valid, writable PCG attribute selection. */
	WATABOUPCG_API bool IsWritableAttributeName(FName Name);

	/**
	 * Sanitize an arbitrary name into a valid PCG attribute name: characters outside
	 * [A-Za-z0-9 _-/] become '_', runs of '_' collapse, leading/trailing '_' are trimmed.
	 * Returns NAME_None if the result is empty. Use before turning open-vocabulary detail
	 * keys into attribute names.
	 */
	WATABOUPCG_API FName SanitizeAttributeName(FName InName);

	/** Strip a leading @{Domain}. prefix from an attribute name, if present. */
	WATABOUPCG_API FName StripDomainFromName(const FName& InName);

	/** Data-domain attribute identifier from a base name (domain prefix stripped). */
	WATABOUPCG_API FPCGAttributeIdentifier MakeDataIdentifier(const FName& BaseName);

	/** Elements-domain (per-point) attribute identifier from a base name (domain prefix stripped). */
	WATABOUPCG_API FPCGAttributeIdentifier MakeElementIdentifier(const FName& BaseName);

	WATABOUPCG_API bool HasAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier);
	WATABOUPCG_API bool HasAttribute(const UPCGData* InData, const FPCGAttributeIdentifier& Identifier);

	/** Typed const attribute lookup that tolerates a missing metadata domain. Null if absent. */
	template <typename T>
	const FPCGMetadataAttribute<T>* TryGetConstAttribute(const UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata) { return nullptr; }
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain)) { return nullptr; }

		// 'template' keyword required for Clang.
		return InMetadata->template GetConstTypedAttribute<T>(Identifier);
	}

	/** Typed mutable attribute lookup that tolerates a missing metadata domain. Null if absent. */
	template <typename T>
	FPCGMetadataAttribute<T>* TryGetMutableAttribute(UPCGMetadata* InMetadata, const FPCGAttributeIdentifier& Identifier)
	{
		if (!InMetadata) { return nullptr; }
		if (!InMetadata->GetConstMetadataDomain(Identifier.MetadataDomain)) { return nullptr; }

		return InMetadata->template GetMutableTypedAttribute<T>(Identifier);
	}

	/**
	 * Read the single value of a @Data-domain attribute. PCG attributes form an inheritance chain;
	 * a duplicated attribute usually has NO local entries (its value lives in a parent), so reading
	 * a local entry key would trip "Invalid metadata key". This walks up to the nearest ancestor
	 * that has entries, falling back to the attribute's default value. Adapted from
	 * PCGExData::Helpers::ReadDataValue.
	 */
	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute)
	{
		// Read a single value from a @Data domain attribute (one value per dataset, not per-point).
		// PCG metadata attributes form an inheritance chain (parent pointers).
		// If the current attribute has no entries, walk up the parent chain to find
		// the nearest ancestor with actual data. If none have entries, fall back to
		// the attribute's default value.
		const FPCGMetadataAttributeBase* Attr = Attribute;
		if (!Attr->GetNumberOfEntries())
		{
			const FPCGMetadataAttributeBase* Parent = Attr->GetParent();
			while (Parent)
			{
				if (!Parent->GetNumberOfEntries())
				{
					Parent = Parent->GetParent();
				}
				else
				{
					Attr = Parent;
					Parent = nullptr;
				}
			}
		}
		return Attr->GetValueFromItemKey<T>(!Attr->GetNumberOfEntries() ? Attr->GetValueKey(PCGDefaultValueKey) : PCGFirstEntryKey);
	}
}
