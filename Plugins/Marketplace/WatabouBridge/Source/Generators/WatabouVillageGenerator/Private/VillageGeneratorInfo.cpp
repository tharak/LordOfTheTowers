// Copyright 2026 Timothé Lapetite

#include "VillageGeneratorInfo.h"
#include "VillageGeneratorParser.h"
#include "VillageGeneratorSettings.h"

TSharedPtr<IWatabouParser> FVillageGeneratorInfo::CreateParser() const
{
	return MakeShared<FVillageGeneratorParser>();
}

FInstancedStruct FVillageGeneratorInfo::CreateDefaultSettings() const
{
	return MakeDefaultSettingsFromQuery<FVillageGeneratorSettings>(Id, DefaultQuery);
}

TArray<FWatabouTagGroup> FVillageGeneratorInfo::GetTagGroups() const
{
	// Sourced from ic.tags + ic.resolve (Village.js v1.6.6). The bundle injects Roads -> isolated when
	// no Roads tag is present (reflected in DefaultQuery).
	//
	// The pill UI models only per-group exclusivity, so three cross-group constraints the bundle's
	// resolve() also enforces are NOT represented here (the bundle reconciles them on load, last-wins):
	//   - island (Water) excludes highway / dead end / crossroads (Roads);
	//   - "no square" excludes "dead end" (Roads);
	//   - palisade excludes district.
	FWatabouTagGroup Density;
	Density.Label = TEXT("Density");
	Density.Tags  = { TEXT("sparse"), TEXT("dense") };
	Density.bMutuallyExclusive = true;

	FWatabouTagGroup Water;
	Water.Label = TEXT("Water");
	Water.Tags  = { TEXT("river"), TEXT("coast"), TEXT("island"), TEXT("pond"), TEXT("confluence"), TEXT("estuary") };
	Water.bMutuallyExclusive = true;

	FWatabouTagGroup Roads;
	Roads.Label = TEXT("Roads");
	Roads.Tags  = { TEXT("isolated"), TEXT("highway"), TEXT("dead end"), TEXT("crossroads") };
	Roads.bMutuallyExclusive = true;

	FWatabouTagGroup Fields;
	Fields.Label = TEXT("Fields");
	Fields.Tags  = { TEXT("farmland"), TEXT("uncultivated"), TEXT("no orchards") };
	Fields.bMutuallyExclusive = true;

	// organic/grove are truly independent; district/palisade/no square carry the cross-constraints
	// noted above, which the pill UI can't enforce (the bundle resolves them on load).
	FWatabouTagGroup Features;
	Features.Label = TEXT("Features");
	Features.Tags  = { TEXT("organic"), TEXT("grove"), TEXT("district"), TEXT("palisade"), TEXT("no square") };
	Features.bMutuallyExclusive = false;

	return { Density, Water, Roads, Fields, Features };
}

FWatabouResourceManifest FVillageGeneratorInfo::GetResourceManifest() const
{
	FWatabouResourceManifest M;
	M.UpstreamBaseUrl = TEXT("https://watabou.github.io/village-generator");
	M.BundleScript    = BundleScript;
	M.TopLevelFiles   = { TEXT("favicon.png") };
	M.AssetsDir       = TEXT("Assets");
	M.FontStems = {
		TEXT("Cinzel-Bold"),
		TEXT("ShareTechMono-Regular"),
		TEXT("ShareTech-Regular"),
	};
	M.RuntimeAssets = {
		TEXT("given_female.txt"),     TEXT("given_male.txt"),     TEXT("grammar.json"),
		TEXT("village_bw.json"),      TEXT("village_cold.json"),  TEXT("village_default.json"),
		TEXT("village_minimal.json"), TEXT("village_night.json"), TEXT("village_sand.json"),
	};
	return M;
}

FVillageGeneratorInfo::FVillageGeneratorInfo()
{
	Id              = TEXT("village-generator");
	ShortId         = TEXT("vg");
	DisplayName     = TEXT("Village Generator");
	BundleScript    = TEXT("Village.js");
	BundleVersion   = TEXT("");
	// "isolated" is the bundle's injected Roads default; width/height/pop omitted at their defaults
	// (500/500/0), matching what the bundle's updateURL would emit.
	DefaultQuery    = TEXT("?seed=1234567&tags=isolated");
	JsReadinessExpr = TEXT("window.__hx['com.watabou.village.scenes.VillageScene'].inst && window.__hx['com.watabou.village.scenes.VillageScene'].inst.village");
	JsTriggerExpr   = TEXT("window.__hx['com.watabou.village.JSONExporter'].export(window.__hx['com.watabou.village.scenes.VillageScene'].inst.village)");

	BundlePatch.BundleVersion        = BundleVersion;
	BundlePatch.ExpectedRegistryVar  = TEXT("g");
}
