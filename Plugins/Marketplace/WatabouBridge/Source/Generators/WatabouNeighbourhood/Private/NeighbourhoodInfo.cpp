// Copyright 2026 Timothé Lapetite

#include "NeighbourhoodInfo.h"
#include "NeighbourhoodParser.h"
#include "NeighbourhoodSettings.h"

TSharedPtr<IWatabouParser> FNeighbourhoodInfo::CreateParser() const
{
	return MakeShared<FNeighbourhoodParser>();
}

FInstancedStruct FNeighbourhoodInfo::CreateDefaultSettings() const
{
	return MakeDefaultSettingsFromQuery<FNeighbourhoodSettings>(Id, DefaultQuery);
}

TArray<FWatabouTagGroup> FNeighbourhoodInfo::GetTagGroups() const
{
	// Sourced from I.tags + I.resolve (Neighbourhood.js v1.2.2). The bundle injects defaults when a
	// category is absent: Size -> medium, Template -> area (both reflected in DefaultQuery).
	FWatabouTagGroup Size;
	Size.Label = TEXT("Size");
	Size.Tags  = { TEXT("small"), TEXT("medium"), TEXT("large") };
	Size.bMutuallyExclusive = true;

	FWatabouTagGroup Template;
	Template.Label = TEXT("Template");
	Template.Tags  = { TEXT("area"), TEXT("street"), TEXT("square"), TEXT("ring") };
	Template.bMutuallyExclusive = true;

	FWatabouTagGroup Features;
	Features.Label = TEXT("Features");
	Features.Tags  = { TEXT("leafy"), TEXT("secular") };
	Features.bMutuallyExclusive = false;

	return { Size, Template, Features };
}

FWatabouResourceManifest FNeighbourhoodInfo::GetResourceManifest() const
{
	FWatabouResourceManifest M;
	M.UpstreamBaseUrl = TEXT("https://watabou.github.io/neighbourhood");
	M.BundleScript    = BundleScript;
	M.TopLevelFiles   = { TEXT("favicon.png") };
	M.AssetsDir       = TEXT("Assets");
	M.FontStems = {
		TEXT("ShareTech-Regular"),
		TEXT("ShareTechMono-Regular"),
		TEXT("Voltaire-Regular"),
	};
	M.RuntimeAssets = {
		TEXT("grammar.json"),
		TEXT("words.txt"),
		TEXT("default.json"),
		TEXT("modern.json"),
		TEXT("warm.json"),
		TEXT("cool.json"),
		TEXT("night.json"),
		TEXT("bw.json"),
	};
	return M;
}

FNeighbourhoodInfo::FNeighbourhoodInfo()
{
	Id              = TEXT("neighbourhood");
	ShortId         = TEXT("nbh");
	DisplayName     = TEXT("Neighbourhood");
	BundleScript    = TEXT("Neighbourhood.js");
	BundleVersion   = TEXT("1.2.2");
	DefaultQuery    = TEXT("?seed=1168349019&tags=medium,area");

	// MapScene singleton mirrors Village's pattern: <Scene>.inst then .hood field.
	// Constructor's `Hf.inst=this` self-registers; the export trigger reads
	// MapScene.inst.hood and routes JSON through JsonExporter.export -> Pc.saveText
	// -> window.saveAs (captured by our bridge).
	JsReadinessExpr = TEXT("window.__hx['com.watabou.neighbourhood.scenes.MapScene'].inst && window.__hx['com.watabou.neighbourhood.scenes.MapScene'].inst.hood");
	JsTriggerExpr   = TEXT("window.__hx['com.watabou.neighbourhood.export.JsonExporter'].export(window.__hx['com.watabou.neighbourhood.scenes.MapScene'].inst.hood)");

	BundlePatch.BundleVersion       = BundleVersion;
	BundlePatch.ExpectedRegistryVar = TEXT("h");
}
