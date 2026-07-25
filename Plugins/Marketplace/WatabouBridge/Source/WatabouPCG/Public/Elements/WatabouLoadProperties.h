// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "PCGContext.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "WatabouFeatureMap.h"
#include "WatabouLoadProperties.generated.h"

/**
 * Load Watabou Properties: the generator-agnostic consumer of the Feature Map.
 *
 * Takes the keyed geometry from Load Watabou Data (Points) plus its Feature Map (param),
 * resolves each point's WatabouKey back to its source feature, and writes that feature's
 * details as point attributes (String -> FString, Name -> FName, Numeric -> double, named
 * by the detail key). Struct / seed-ref details are left for recursion (a chained Load
 * Watabou Data), not surfaced here. Output is the input points plus the new attributes.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class WATABOUPCG_API UWatabouLoadPropertiesSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("WatabouLoadProperties")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Write every detail key found on each point's feature. Disable to restrict to DetailKeys. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bLoadAllKeys = true;

	/** When bLoadAllKeys is off, only these detail keys are written (as same-named attributes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (EditCondition = "!bLoadAllKeys"))
	TArray<FName> DetailKeys;

	/** Also write the owning collection's shared details (e.g. a Dwellings floor's "level"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bLoadCollectionDetails = true;

	/**
	 * Write each point's feature kind -- its FWatabouFeature::Id (e.g. rooms / doors / windows / stairs /
	 * exit) -- to a "FeatureId" Name attribute. On by default: consolidating generators (Dwellings) fold
	 * several feature kinds into one cloud, so the kind is otherwise unrecoverable on the merged points.
	 * Distinct from any "type" detail a feature may also carry (e.g. a door's subtype).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	bool bLoadFeatureType = true;
};

/** Custom context: async-loads the Feature Map's assets, then resolves keys off the game thread. */
struct FWatabouLoadPropertiesContext : public FPCGContext, public IPCGAsyncLoadingContext
{
	WatabouPCG::FWatabouFeatureMapUnpacker Unpacker;
};

class FWatabouLoadPropertiesElement : public IPCGElementWithCustomContext<FWatabouLoadPropertiesContext>
{
public:
	// The async-load request goes through the game-thread-only streamable manager, so the
	// load-issuing phase (PrepareData) is pinned to the game thread; attribute writing (Execute)
	// runs off-thread. The load stays non-blocking -- the element pauses while assets stream in.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return Context && Context->CurrentPhase == EPCGExecutionPhase::PrepareData;
	}

protected:
	virtual bool PrepareDataInternal(FPCGContext* Context) const override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
