// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UWatabouAssetBase;

/**
 * Per-generator JSON-to-asset parser.
 *
 * One concrete subclass per Watabou generator (e.g. FPerilousShoresParser).
 * Most generators follow the GeoJSON-shaped output of FWatabouGeometryParser
 * (which lives in this module) -- they subclass it and either change nothing
 * or override specific hooks for non-standard fields.
 *
 * The parser fills an existing UWatabouAssetBase (created by the caller with
 * the right SourceSeed / BundleVersion already populated). On failure, returns
 * false and sets OutError.
 */
class WATABOUCORE_API IWatabouParser : public TSharedFromThis<IWatabouParser>
{
public:
	virtual ~IWatabouParser() = default;

	/** Parses the JSON document into the asset. Returns false on failure (sets OutError). */
	virtual bool Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError) = 0;

	/** Bundle versions this parser claims to support. Used for version selection / mismatch warnings. */
	virtual TArray<FString> GetSupportedVersions() const { return {}; }
};
