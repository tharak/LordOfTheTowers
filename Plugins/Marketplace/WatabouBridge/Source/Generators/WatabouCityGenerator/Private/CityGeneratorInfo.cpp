// Copyright 2026 Timothé Lapetite

#include "CityGeneratorInfo.h"
#include "CityGeneratorParser.h"
#include "CityGeneratorSettings.h"

TSharedPtr<IWatabouParser> FCityGeneratorInfo::CreateParser() const
{
	return MakeShared<FCityGeneratorParser>();
}

FInstancedStruct FCityGeneratorInfo::CreateDefaultSettings() const
{
	return MakeDefaultSettingsFromQuery<FCityGeneratorSettings>(Id, DefaultQuery);
}

FWatabouResourceManifest FCityGeneratorInfo::GetResourceManifest() const
{
	FWatabouResourceManifest M;
	M.UpstreamBaseUrl = TEXT("https://watabou.github.io/city-generator");
	M.BundleScript    = BundleScript;
	M.TopLevelFiles   = { TEXT("favicon.png") };
	M.AssetsDir       = TEXT("Assets");
	M.FontStems = {
		TEXT("IMFellGreatPrimer-Regular"),
		TEXT("ShareTechMono-Regular"),
		TEXT("ShareTech-Regular"),
	};
	M.RuntimeAssets = {
		TEXT("bw.json"),            TEXT("default.json"),    TEXT("grammar.json"),
		TEXT("ink.json"),           TEXT("modern.json"),     TEXT("natural.json"),
		TEXT("vivid.json"),
		TEXT("elven.txt"),          TEXT("english.txt"),
		TEXT("given_female.txt"),   TEXT("given_male.txt"),
	};
	return M;
}

FCityGeneratorInfo::FCityGeneratorInfo()
{
	Id              = TEXT("city-generator");
	ShortId         = TEXT("cg");
	DisplayName     = TEXT("Medieval Fantasy City Generator");
	BundleScript    = TEXT("mfcg.js");
	BundleVersion   = TEXT("");  // pinned at vendor time; bundle's b.meta.h.version not yet parsed
	// size=25 is the bundle's documented fresh-create default (still light -- size=64 would be
	// 5000+ features). The map-feature flags come from the FCityGeneratorSettings struct defaults
	// (citadel/plaza/temple/walls/coast on, the rest off).
	DefaultQuery    = TEXT("?size=25&seed=831");
	JsReadinessExpr = TEXT("window.__hx['com.watabou.mfcg.model.City'].instance");
	JsTriggerExpr   = TEXT("window.__hx['com.watabou.mfcg.export.Export'].asJSON()");

	BundlePatch.BundleVersion        = BundleVersion;
	BundlePatch.ExpectedRegistryVar  = TEXT("g");
}
