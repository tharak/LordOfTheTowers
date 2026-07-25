// Copyright 2026 Timothé Lapetite

#include "PerilousShoresInfo.h"
#include "PerilousShoresParser.h"
#include "PerilousShoresProcessor.h"
#include "PerilousShoresSettings.h"

TSharedPtr<IWatabouParser> FPerilousShoresInfo::CreateParser() const
{
	return MakeShared<FPerilousShoresParser>();
}

TSharedPtr<IWatabouProcessor> FPerilousShoresInfo::CreateProcessor() const
{
	return MakeShared<FPerilousShoresProcessor>();
}

FInstancedStruct FPerilousShoresInfo::CreateDefaultSettings() const
{
	return MakeDefaultSettingsFromQuery<FPerilousShoresSettings>(Id, DefaultQuery);
}

TArray<FWatabouTagGroup> FPerilousShoresInfo::GetTagGroups() const
{
	// Sourced from rc.all + rc.resolve (Perilous.js v1.8.0). The bundle injects defaults when a
	// category is absent: Template -> island, Alignment -> neutral (both reflected in DefaultQuery).
	FWatabouTagGroup Template;
	Template.Label = TEXT("Template");
	Template.Tags  = { TEXT("island"), TEXT("coast"), TEXT("lake"), TEXT("land"),
	                   TEXT("peninsula"), TEXT("fjord"), TEXT("bay"), TEXT("archipelago") };
	Template.bMutuallyExclusive = true;

	FWatabouTagGroup Elevation;
	Elevation.Label = TEXT("Elevation");
	Elevation.Tags  = { TEXT("highland"), TEXT("lowland") };
	Elevation.bMutuallyExclusive = true;

	FWatabouTagGroup Moisture;
	Moisture.Label = TEXT("Moisture");
	Moisture.Tags  = { TEXT("barren"), TEXT("wetland") };
	Moisture.bMutuallyExclusive = true;

	FWatabouTagGroup Danger;
	Danger.Label = TEXT("Danger");
	Danger.Tags  = { TEXT("safe"), TEXT("perilous") };
	Danger.bMutuallyExclusive = true;

	FWatabouTagGroup Alignment;
	Alignment.Label = TEXT("Alignment");
	Alignment.Tags  = { TEXT("good"), TEXT("lawful"), TEXT("neutral"), TEXT("chaotic"), TEXT("evil") };
	Alignment.bMutuallyExclusive = true;

	FWatabouTagGroup Features;
	Features.Label = TEXT("Features");
	Features.Tags  = { TEXT("woodland"), TEXT("civilized"), TEXT("difficult") };
	Features.bMutuallyExclusive = false;

	return { Template, Elevation, Moisture, Danger, Alignment, Features };
}

FWatabouResourceManifest FPerilousShoresInfo::GetResourceManifest() const
{
	FWatabouResourceManifest M;
	M.UpstreamBaseUrl = TEXT("https://watabou.github.io/perilous-shores");
	M.BundleScript    = BundleScript;
	M.TopLevelFiles   = { TEXT("favicon.png") };
	M.AssetsDir       = TEXT("Assets");
	M.FontStems = {
		TEXT("ShareTech-Regular"),
		TEXT("ShareTechMono-Regular"),
		TEXT("LovedbytheKing-Regular"),   // partial / sometimes 404
		TEXT("CaveatBrush-Regular"),       // partial / sometimes 404
	};
	M.RuntimeAssets = {
		TEXT("default.json"),    TEXT("bw.json"),      TEXT("antique.json"),
		TEXT("soft.json"),       TEXT("cartoon.json"), TEXT("october.json"),
		TEXT("full_colour.json"),TEXT("grammar.json"), TEXT("centrepiece.json"),
		TEXT("english.txt"),     TEXT("elven.txt"),    TEXT("demonic.txt"),
		TEXT("given_male.txt"),  TEXT("given_female.txt"),
	};
	return M;
}

FPerilousShoresInfo::FPerilousShoresInfo()
{
	Id              = TEXT("perilous-shores");
	ShortId         = TEXT("ps");
	DisplayName     = TEXT("Perilous Shores");
	BundleScript    = TEXT("Perilous.js");
	BundleVersion   = TEXT("1.8.0");
	// island + neutral are the bundle's injected category defaults; w/h/hexes omitted at their
	// defaults (1200/1200/Off), matching what the bundle's updateURL would emit.
	DefaultQuery    = TEXT("?seed=1208662359&tags=island,neutral");
	JsReadinessExpr = TEXT("window.__hx['com.watabou.perilous.model.Region'].inst");
	JsTriggerExpr   = TEXT("window.__hx['com.watabou.perilous.model.Serializer'].saveAsJSON(window.__hx['com.watabou.perilous.model.Region'].inst)");

	BundlePatch.BundleVersion        = BundleVersion;
	BundlePatch.ExpectedRegistryVar  = TEXT("h");
	// PatchSitePattern / PatchReplacement inherit the universal Haxe defaults

	// Emit each hex's RENDERED centre (a.data.center -> x/y) alongside q/r. The default / "warped" modes relax
	// and rotate the mesh per seed, so q/r can't reproduce the drawn positions, but the bundle already holds
	// them; emitting them lets the importer match the render for every hex mode. Patch site is getHex's
	// `b={q:b.x,r:b.y}` literal (occurs once, so unambiguous).
	//
	// bRequired = FALSE + idempotent: the optional (?:...)? group swallows an already-present ",x:...,y:..." so
	// re-vendoring is a no-op, and a moved getHex shape just omits the fields -> the parser falls back to the
	// canonical q/r projection (no broken download). Mirrors the Dwellings va/vb endpoint patch.
	BundlePatch.ExtraReplacements.Add(FWatabouBundleReplacement{
		TEXT("Perilous Shores getHex -> emit rendered hex centre (x/y) for warp-accurate positions"),
		TEXT(R"RX(b=\{q:b\.x,r:b\.y(?:,x:a\.data\.center\.x,y:a\.data\.center\.y)?\})RX"),
		TEXT("b={q:b.x,r:b.y,x:a.data.center.x,y:a.data.center.y}"),
		/*bRequired=*/false
	});
}
