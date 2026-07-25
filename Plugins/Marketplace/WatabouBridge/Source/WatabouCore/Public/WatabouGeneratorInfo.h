// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouBundlePatchSpec.h"
#include "WatabouResourceManifest.h"
#include "WatabouGeneratorSettings.h"   // FWatabouGeneratorSettings (base) + FWatabouTagGroup
#include "StructUtils/InstancedStruct.h"

class IWatabouParser;
class IWatabouProcessor;

/**
 * Per-Watabou-generator metadata. One concrete subclass per generator (in its
 * own module), populated with that generator's specifics.
 *
 * Lifecycle: instantiated once per generator module's StartupModule and
 * registered with FWatabouGeneratorRegistry. Lives as a TSharedRef in the
 * registry for the lifetime of the editor / game.
 *
 * The JS expressions are substituted into the editor's injection shim template
 * (see SWatabouImportPanel). See memory/reference_watabou_generators_anatomy.md
 * for how to derive each field from a bundle's source.
 */
struct WATABOUCORE_API FWatabouGeneratorInfo
{
	virtual ~FWatabouGeneratorInfo() = default;

	/** URL-safe id. Used as directory name under Resources/Bundles/ and as URL prefix. */
	FString Id;

	/** Compact code used in imported asset filenames (e.g. "vg" for village-generator) to keep
	 *  paths short. Optional -- GetShortId() falls back to Id when this is empty. */
	FString ShortId;

	/** Friendly name shown in the import picker. */
	FString DisplayName;

	/** Effective filename code: ShortId when set, else the full Id. */
	FString GetShortId() const { return ShortId.IsEmpty() ? Id : ShortId; }

	/** Bundle script filename, e.g. "Perilous.js". Informational; the HTTP server discovers it from disk. */
	FString BundleScript;

	/** Bundle version the rest of this struct was authored against. */
	FString BundleVersion;

	/** Sample query string used as the default for first-time UI use. */
	FString DefaultQuery;

	/** JS expression evaluating truthy when the bundle is ready to export. */
	FString JsReadinessExpr;

	/** JS statement that triggers a JSON export (its output flows through window.saveAs). */
	FString JsTriggerExpr;

	/**
	 * JS expression evaluating to a Promise<string> resolving to a PNG data URL
	 * (or empty string on failure). Substituted into the shim as __THUMBNAIL_EXPR__
	 * and captured alongside the JSON export to seed the imported asset's thumbnail.
	 *
	 * Leave empty to use the shim's default `window.__watabou_default_thumbnail()`,
	 * which picks the largest <canvas> in the DOM and falls back to <svg> if none.
	 * Override per-generator only when the default picks the wrong element (e.g.
	 * a bundle with multiple canvases where the main map isn't the largest).
	 *
	 * Output PNG is clamped to 512x512 max with aspect ratio preserved by the shim;
	 * authoring expressions don't need to resize themselves.
	 */
	FString JsThumbnailExpr;

	/** Spec for the vendor-time bundle patch (expose Haxe class registry). */
	FWatabouBundlePatchSpec BundlePatch;

	/**
	 * Build a parser instance for this generator. Returns null if no parser is
	 * registered yet (the round-trip pipeline will skip asset creation in that case).
	 * Per-generator subclasses override.
	 */
	virtual TSharedPtr<IWatabouParser> CreateParser() const { return nullptr; }

	/**
	 * Build the PCG consumption processor for this generator (WatabouPCG module).
	 * Returns null when the generator has no specialized processor; the Load Watabou
	 * Data node then falls back to the generic GeoJSON processor. Per-generator
	 * subclasses override to return their tailored processor (hex / cell / rect
	 * reconstruction).
	 *
	 * Lazy, mirroring CreateParser: invoked at PCG graph execution time, never at
	 * module load. The forward-declared IWatabouProcessor (defined in WatabouPCG)
	 * therefore imposes no build dependency on WatabouCore -- only the overriding
	 * generator modules link WatabouPCG / PCG.
	 */
	virtual TSharedPtr<IWatabouProcessor> CreateProcessor() const { return nullptr; }

	/**
	 * Resource manifest describing what to download to vendor this generator.
	 * Default returns empty; per-generator subclasses fill in their specifics.
	 * Consumed by FWatabouResourceUpdater (editor-side).
	 */
	virtual FWatabouResourceManifest GetResourceManifest() const { return {}; }

	/**
	 * Create a default-initialised settings instance (typed FWatabouGeneratorSettings
	 * subclass) for this generator, parallel to CreateParser(). Held by the import panel,
	 * one per generator, and edited via IStructureDetailsView. Base returns a plain
	 * base-settings instance; generators override to return their concrete type, typically
	 * via MakeDefaultSettingsFromQuery<FXxxSettings>(Id, DefaultQuery).
	 */
	virtual FInstancedStruct CreateDefaultSettings() const
	{
		return FInstancedStruct::Make<FWatabouGeneratorSettings>();
	}

	/**
	 * Tag vocabulary for this generator, grouped for the import panel's pill UI.
	 * Mutually-exclusive groups behave like radio buttons. Default: no tags. Filled in
	 * per generator as the vocabulary is sourced from each bundle.
	 */
	virtual TArray<FWatabouTagGroup> GetTagGroups() const { return {}; }
};
