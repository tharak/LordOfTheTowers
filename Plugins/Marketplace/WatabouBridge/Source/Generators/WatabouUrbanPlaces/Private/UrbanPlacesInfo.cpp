// Copyright 2026 Timothé Lapetite

#include "UrbanPlacesInfo.h"
#include "UrbanPlacesParser.h"
#include "UrbanPlacesSettings.h"

TSharedPtr<IWatabouParser> FUrbanPlacesInfo::CreateParser() const
{
	return MakeShared<FUrbanPlacesParser>();
}

FInstancedStruct FUrbanPlacesInfo::CreateDefaultSettings() const
{
	return MakeDefaultSettingsFromQuery<FUrbanPlacesSettings>(Id, DefaultQuery);
}

TArray<FWatabouTagGroup> FUrbanPlacesInfo::GetTagGroups() const
{
	// Sourced from qc.all + qc.resolve (Urban.js v1.2.1). No defaults injected.
	FWatabouTagGroup Size;
	Size.Label = TEXT("Size");
	Size.Tags  = { TEXT("small"), TEXT("large") };
	Size.bMutuallyExclusive = true;

	FWatabouTagGroup Template;
	Template.Label = TEXT("Template");
	Template.Tags  = { TEXT("square"), TEXT("coast"), TEXT("river"), TEXT("park"), TEXT("street"), TEXT("wall") };
	Template.bMutuallyExclusive = true;

	FWatabouTagGroup Density;
	Density.Label = TEXT("Density");
	Density.Tags  = { TEXT("dense"), TEXT("sparse") };
	Density.bMutuallyExclusive = true;

	FWatabouTagGroup Angles;
	Angles.Label = TEXT("Angles");
	Angles.Tags  = { TEXT("rigid"), TEXT("chaotic") };
	Angles.bMutuallyExclusive = true;

	FWatabouTagGroup Features;
	Features.Label = TEXT("Features");
	Features.Tags  = { TEXT("green") };
	Features.bMutuallyExclusive = false;

	return { Size, Template, Density, Angles, Features };
}

FWatabouResourceManifest FUrbanPlacesInfo::GetResourceManifest() const
{
	FWatabouResourceManifest M;
	M.UpstreamBaseUrl = TEXT("https://watabou.github.io/urban-places");
	M.BundleScript    = BundleScript;
	M.TopLevelFiles   = { TEXT("favicon.png") };
	M.AssetsDir       = TEXT("Assets");
	M.FontStems = {
		TEXT("ShareTech-Regular"),
		TEXT("ShareTechMono-Regular"),
		TEXT("CormorantUnicase-Regular"),
	};
	M.RuntimeAssets = {
		TEXT("grammar.json"),
		TEXT("given_male.txt"),
		TEXT("given_female.txt"),
		TEXT("default.json"),
		TEXT("winter.json"),
		TEXT("night.json"),
		TEXT("bw.json"),
	};
	return M;
}

FUrbanPlacesInfo::FUrbanPlacesInfo()
{
	Id              = TEXT("urban-places");
	ShortId         = TEXT("up");
	DisplayName     = TEXT("Urban Places");
	BundleScript    = TEXT("Urban.js");
	BundleVersion   = TEXT("1.2.1");
	// A default tag is required for determinism: an empty tags= makes the bundle re-roll tags on
	// every load (qc.random). "square" pins a central-square place (Template group, so it reads back
	// cleanly as a single selected pill).
	DefaultQuery    = TEXT("?seed=1234567&tags=square");

	// Urban Places does not expose a *.inst singleton; the active scene lives at
	// com.watabou.coogee.Game.scene (the global lime application), and the model
	// to export is Scene.place. The exporter routes through Export.asJSON, which
	// internally calls JsonExporter.export() and then Rd.saveText() -> window.saveAs
	// (captured by our bridge).
	JsReadinessExpr = TEXT("window.__hx['com.watabou.coogee.Game'].scene && window.__hx['com.watabou.coogee.Game'].scene.place");
	JsTriggerExpr   = TEXT("window.__hx['com.watabou.urban.Export'].asJSON(window.__hx['com.watabou.coogee.Game'].scene.place)");

	BundlePatch.BundleVersion       = BundleVersion;
	BundlePatch.ExpectedRegistryVar = TEXT("h");
}
