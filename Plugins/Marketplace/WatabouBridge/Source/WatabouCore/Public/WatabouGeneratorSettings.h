// Copyright 2026 Timothé Lapetite

#pragma once

#include <type_traits>

#include "CoreMinimal.h"
#include "WatabouSeedRef.h"
#include "WatabouUrlUtils.h"
#include "StructUtils/InstancedStruct.h"
#include "WatabouGeneratorSettings.generated.h"

/**
 * A named group of generation tags for one generator. The import panel renders each
 * group as a row of toggle pills; when bMutuallyExclusive is set, at most one pill in
 * the group can be active at a time (radio-style).
 *
 * Plain (non-reflected) struct: it's static vocabulary supplied by
 * FWatabouGeneratorInfo::GetTagGroups and consumed by editor Slate -- never edited or
 * serialized as a property.
 */
struct FWatabouTagGroup
{
	/** Heading shown above the group (e.g. "Geography"). Empty = no heading. */
	FString Label;

	/** Tag tokens in this group, in display order. */
	TArray<FString> Tags;

	/** When true, selecting one tag in this group clears the others (radio-style). */
	bool bMutuallyExclusive = false;
};

/**
 * Base class for a generator's typed import settings. One concrete subclass per
 * generator (in that generator's module) adds its own parameters (width/height, size,
 * plaza, ...) and overrides ToSeedRef / LoadFrom.
 *
 * Held polymorphically in an FInstancedStruct so the editor's import panel can show the
 * right properties per generator (IStructureDetailsView) and persist them per session.
 *
 * Runtime-friendly: lives in WatabouCore and produces an FWatabouSeedRef, so the same
 * types drive both the editor panel and any future headless/PCG URL building.
 */
USTRUCT()
struct WATABOUCORE_API FWatabouGeneratorSettings
{
	GENERATED_BODY()

	virtual ~FWatabouGeneratorSettings() = default;

	/** Optional content name (URL "name"), supported by every generator. Emitted only when non-empty
	 *  (BuildQueryString URL-encodes it); blank means "let the generator pick a name". */
	UPROPERTY(EditAnywhere, Category = "General")
	FString Name;

	/** Generation seed (Watabou seeds are non-negative integers, kept as text to avoid range limits).
	 *  Non-Edit on purpose: the import panel renders it as a custom seed row (with a re-roll button)
	 *  rather than in the details view, and edits it directly in C++. */
	UPROPERTY()
	FString Seed;

	/** Selected generation tags -- a subset of this generator's vocabulary (GetTagGroups).
	 *  Non-Edit on purpose: rendered as toggle pills by the panel, not in the details view. */
	UPROPERTY()
	TArray<FString> SelectedTags;

	/**
	 * Populate OutRef (GeneratorId / Seed / Tags / Params) from these settings. The base
	 * fills GeneratorId, Seed and Tags and clears Params; subclasses call the base then add
	 * their own params. Drives the URL via FWatabouSeedRef::BuildQueryString.
	 */
	virtual void ToSeedRef(const FString& GeneratorId, FWatabouSeedRef& OutRef) const;

	/**
	 * Inverse of ToSeedRef: copy seed/tags/params back into these settings. Used by "load
	 * from link" and by the panel's read-back of in-browser changes. Subclasses call the
	 * base then read their own params; params absent from InRef keep their current value.
	 */
	virtual void LoadFrom(const FWatabouSeedRef& InRef);

	/** Assign a fresh random non-negative seed (UI re-roll button). */
	void RandomizeSeed();
};

/**
 * Make a default settings instance of the given concrete type, pre-populated from a
 * generator's DefaultQuery sample. Struct member defaults act as the fallback for any
 * param the DefaultQuery omits, so the sample query stays the single source of curated
 * defaults. Used by FWatabouGeneratorInfo::CreateDefaultSettings overrides.
 */
template <typename TSettings>
FInstancedStruct MakeDefaultSettingsFromQuery(const FString& GeneratorId, const FString& DefaultQuery)
{
	static_assert(std::is_base_of_v<FWatabouGeneratorSettings, TSettings>,
		"TSettings must derive from FWatabouGeneratorSettings");

	FInstancedStruct Inst;
	Inst.InitializeAs<TSettings>();
	Inst.GetMutable<FWatabouGeneratorSettings>().LoadFrom(
		WatabouUrlUtils::ParseQueryString(GeneratorId, DefaultQuery));
	return Inst;
}
