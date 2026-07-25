// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

/**
 * Static description of everything that needs to be downloaded to vendor a
 * Watabou generator: bundle JS, index.html, fonts, runtime palette/name files.
 *
 * Each per-generator FWatabouGeneratorInfo subclass overrides
 * GetResourceManifest() to return its concrete list. The FWatabouResourceUpdater
 * service (editor-side) consumes these manifests and drives the downloads.
 *
 * Path conventions:
 *   - All paths in the manifest are RELATIVE to UpstreamBaseUrl when fetching, and to the
 *     versioned cache dir (<Plugin>/Content/Bundles/<GeneratorId>/<version>/, via its
 *     staging dir) when writing to disk. The updater rejects any path containing ".."
 *     for safety.
 *   - FontStems are filename stems without extension. Each stem is fetched four
 *     times under <AssetsDir>/, once per .eot / .woff / .ttf / .svg. Missing
 *     variants 404 silently (some upstream pages reference fonts that aren't
 *     actually shipped -- harmless, browser falls back).
 */
struct WATABOUCORE_API FWatabouResourceManifest
{
	/** Upstream base URL, no trailing slash. E.g. "https://watabou.github.io/perilous-shores". */
	FString UpstreamBaseUrl;

	/** Bundle JS filename (relative). Receives the var <X>={} -> var <X>=window.__hx={} patch. */
	FString BundleScript;

	/** HTML shell filename. Google Analytics script tags are stripped after download. */
	FString IndexFile = TEXT("index.html");

	/** Top-level files copied verbatim (e.g. "favicon.png"). */
	TArray<FString> TopLevelFiles;

	/** Subdirectory holding the bundle's runtime assets. "Assets" for most generators, "assets" for One Page Dungeon. */
	FString AssetsDir = TEXT("Assets");

	/**
	 * Font filename stems (e.g. "ShareTech-Regular"). Each is fetched four times
	 * under <AssetsDir>/ for .eot / .woff / .ttf / .svg variants. Missing
	 * variants are skipped silently.
	 */
	TArray<FString> FontStems;

	/**
	 * Runtime asset filenames (with extension, relative to <AssetsDir>/). E.g.
	 * "default.json", "grammar.json", "english.txt". Required for the bundle to
	 * run without 404-induced parse errors.
	 */
	TArray<FString> RuntimeAssets;

	/**
	 * Files that upstream may 404 on. If the download fails, the updater writes
	 * the corresponding placeholder content to disk so the bundle still gets
	 * a parseable response.
	 *
	 * Key: filename relative to <AssetsDir>/.
	 * Value: UTF-8 content to write on 404 (typically "{}" for JSON, "" for TXT).
	 */
	TMap<FString, FString> Placeholders;
};
