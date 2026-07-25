// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadataCommon.h"          // FPCGAttributeIdentifier, entry keys, domain flag
#include "Metadata/PCGMetadataAttributeTraits.h"  // EPCGMetadataTypes
#include "WatabouForward.generated.h"

class UPCGBasePointData;
class UPCGMetadata;

/**
 * Seed -> target attribute forwarding config (the slim, PCGEx-free counterpart of FPCGExForwardDetails).
 *
 * Forwards attributes from the seed that produced a stamp onto the stamped geometry's @Data domain
 * (one value per emitted feature). Selection is a pair of comma-separated wildcard lists; the
 * WatabouKey / IsClosed / IsHole bookkeeping attributes are always excluded (forwarding the seed's key
 * would clobber the emitted feature's own key and break the Feature Map chain).
 */
USTRUCT(BlueprintType)
struct FWatabouForwardDetails
{
	GENERATED_BODY()

	/** Forward seed attributes onto the stamped geometry's @Data domain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bEnabled = false;

	/** Comma-separated attribute-name wildcards to forward; empty = all (e.g. "building_*, zone"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable, EditCondition = "bEnabled"))
	FString Include;

	/** Comma-separated attribute-name wildcards to exclude (takes precedence over Include). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable, EditCondition = "bEnabled"))
	FString Exclude;
};

/**
 * Captures a seed input's forwardable attributes (filtered by FWatabouForwardDetails, minus bookkeeping)
 * and writes them onto a target's @Data domain. Built once per seed input; reused across every stamp
 * from that input.
 *
 * Type-generic via PCG's CallbackWithRightType, so any PCG attribute type forwards (FName, double,
 * FVector, FTransform, ...). PCGEx-free.
 */
class WATABOUPCG_API FWatabouForwardHandler
{
public:
	FWatabouForwardHandler(const FWatabouForwardDetails& InDetails, const UPCGBasePointData* InSource);

	bool IsEmpty() const { return Identities.IsEmpty(); }

	/**
	 * Write the captured seed attributes onto TargetMetadata's @Data domain.
	 *
	 * bDataDomainOnly=false (per-point stamp): forward every captured attribute -- per-point attrs read
	 *   at SourceIndex's metadata entry, @Data attrs broadcast.
	 * bDataDomainOnly=true (@Data-keyed stamp): forward ONLY the seed's @Data-domain attributes;
	 *   SourceIndex is ignored.
	 */
	void ForwardToData(int32 SourceIndex, UPCGMetadata* TargetMetadata, bool bDataDomainOnly) const;

private:
	struct FForwarded
	{
		FPCGAttributeIdentifier Identifier;
		EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;
		bool bAllowsInterpolation = true;
		bool bDataDomain = false;
	};

	const UPCGBasePointData* Source = nullptr;
	TArray<FForwarded> Identities;
};
