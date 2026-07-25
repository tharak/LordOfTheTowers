// Copyright 2026 Timothé Lapetite

#include "DwellingsInfo.h"
#include "DwellingsParser.h"
#include "DwellingsProcessor.h"
#include "DwellingsSettings.h"
#include "WatabouDwellingLimits.h"

TSharedPtr<IWatabouParser> FDwellingsInfo::CreateParser() const
{
	return MakeShared<FDwellingsParser>();
}

TSharedPtr<IWatabouProcessor> FDwellingsInfo::CreateProcessor() const
{
	return MakeShared<FDwellingsProcessor>();
}

FInstancedStruct FDwellingsInfo::CreateDefaultSettings() const
{
	return MakeDefaultSettingsFromQuery<FDwellingsSettings>(Id, DefaultQuery);
}

TArray<FWatabouTagGroup> FDwellingsInfo::GetTagGroups() const
{
	// Sourced from xc.ALL + xc.resolve (Dwellings.js v1.4.2). No defaults injected.
	FWatabouTagGroup Size;
	Size.Label = TEXT("Size");
	Size.Tags  = { TEXT("small"), TEXT("medium"), TEXT("large") };
	Size.bMutuallyExclusive = true;

	FWatabouTagGroup Height;
	Height.Label = TEXT("Height");
	Height.Tags  = { TEXT("low"), TEXT("tall") };
	Height.bMutuallyExclusive = true;

	FWatabouTagGroup Windows;
	Windows.Label = TEXT("Windows");
	Windows.Tags  = { TEXT("blank"), TEXT("transparent") };
	Windows.bMutuallyExclusive = true;

	FWatabouTagGroup InnerWalls;
	InnerWalls.Label = TEXT("Inner walls");
	InnerWalls.Tags  = { TEXT("mechanical"), TEXT("organic") };
	InnerWalls.bMutuallyExclusive = true;

	FWatabouTagGroup Stairs;
	Stairs.Label = TEXT("Stairs");
	Stairs.Tags  = { TEXT("spiral"), TEXT("stairwell") };
	Stairs.bMutuallyExclusive = true;

	FWatabouTagGroup Features;
	Features.Label = TEXT("Features");
	Features.Tags  = { TEXT("square"), TEXT("slab"), TEXT("hallways"), TEXT("basement"), TEXT("generic") };
	Features.bMutuallyExclusive = false;

	return { Size, Height, Windows, InnerWalls, Stairs, Features };
}

FWatabouResourceManifest FDwellingsInfo::GetResourceManifest() const
{
	FWatabouResourceManifest M;
	M.UpstreamBaseUrl = TEXT("https://watabou.github.io/dwellings");
	M.BundleScript    = BundleScript;
	M.TopLevelFiles   = { TEXT("favicon.png") };
	M.AssetsDir       = TEXT("Assets");
	// Dwellings uses lowercase 'regular' in its font stems -- preserved verbatim
	M.FontStems = {
		TEXT("ShareTechMono-regular"),
		TEXT("ShareTech-regular"),
	};
	M.RuntimeAssets = {
		TEXT("blueprint.json"), TEXT("bw.json"),     TEXT("gothic.json"),
		TEXT("grammar.json"),   TEXT("natural.json"),TEXT("plain.json"),
		TEXT("tavern.json"),    TEXT("wooden.json"),
	};
	// gothic.json and tavern.json may 404 upstream depending on version; placeholder empties.
	M.Placeholders.Add(TEXT("gothic.json"), TEXT("{}"));
	M.Placeholders.Add(TEXT("tavern.json"), TEXT("{}"));
	return M;
}

FDwellingsInfo::FDwellingsInfo()
{
	Id              = TEXT("dwellings");
	ShortId         = TEXT("dw");
	DisplayName     = TEXT("Dwellings");
	BundleScript    = TEXT("Dwellings.js");
	BundleVersion   = TEXT("");
	DefaultQuery    = TEXT("?seed=1204865330&tags=large");
	JsReadinessExpr = TEXT("window.__hx['com.watabou.dwellings.model.House'].inst");
	JsTriggerExpr   = TEXT("window.__hx['com.watabou.dwellings.model.JsonExporter'].export(window.__hx['com.watabou.dwellings.model.House'].inst)");

	BundlePatch.BundleVersion        = BundleVersion;
	BundlePatch.ExpectedRegistryVar  = TEXT("h");

	// Raise the dwelling plan grid cap: com.watabou.dwellings.model.Specs.MAX_SIZE gates qc.fromCode,
	// which REJECTS any plan whose w/h exceeds it (substituting a random shape). Stock = 11; bump it to
	// WatabouDwellingLimits::BundleMaxCells so large footprints (e.g. City buildings) can fill instead of
	// under-filling. The pattern anchors on the SIZE=["small","medium","large"] array literal that follows
	// the assignment in the minified static-init chain: that pins the Specs.MAX_SIZE site (NOT the unrelated
	// image-export ub.MAX_SIZE=16777216) without depending on the minified class letter, and \d+ tolerates
	// the stock default changing across bundle versions. Applied at download time by the resource updater.
	BundlePatch.ExtraReplacements.Add(FWatabouBundleReplacement{
		FString::Printf(TEXT("Dwellings Specs.MAX_SIZE -> %d (per-axis dwelling cell cap)"), WatabouDwellingLimits::BundleMaxCells),
		TEXT(R"RX(MAX_SIZE=\d+(;[A-Za-z_$][\w$]{0,3}\.SIZE=\["small","medium","large"\]))RX"),
		FString::Printf(TEXT("MAX_SIZE=%d$1"), WatabouDwellingLimits::BundleMaxCells),
		/*bRequired=*/true
	});

	// Emit each edge feature's two endpoint VERTICES (va / vb) alongside the legacy cell + dir. The importer
	// places doors / windows at the wall MIDPOINT derived from these, instead of guessing the wall from
	// (cell + half-cell dir): the exported dir is the edge's winding-relative CCW normal, which is reliable on
	// the simple outer ring but flips on upper-floor courtyard / setback edges, landing openings on the wrong
	// (interior) wall. The endpoints are the exact segment the bundle drew the opening on -- unambiguous.
	// edge2data is the shared encoder for both windows (bare edge) and doors (door2data -> edge2data); stairs /
	// exit use a plain cell and are unaffected (the importer falls back to the dir offset for them). bRequired
	// is FALSE: a future bundle whose edge2data shape moved simply omits the endpoints, and the importer
	// transparently reverts to the legacy dir-offset placement -- no broken download, just the old behaviour.
	//
	// IDEMPOTENT (like the MAX_SIZE patch above): the pattern's trailing (?:...)? OPTIONALLY consumes an
	// already-present ",va:{...},vb:{...}" and the replacement always reproduces exactly one. So applying this
	// to an already-patched bundle (e.g. the vendored copy) rewrites it to the identical text instead of
	// double-appending duplicate keys. (?:...) is a non-capturing group -- group $1 still refers to the
	// prefix -- and needs no lookahead, which UE's ICU regex does not reliably support.
	BundlePatch.ExtraReplacements.Add(FWatabouBundleReplacement{
		TEXT("Dwellings edge2data -> emit endpoint vertices (va/vb) for exact opening placement"),
		TEXT(R"RX((edge2data=function\(a\)\{return\{cell:[A-Za-z_$][\w$]{0,3}\.cell2data\([A-Za-z_$][\w$]{0,3}\.grid\.edge2cell\(a\)\),dir:[A-Za-z_$][\w$]{0,3}\.dir2data\(a\.dir\.ccw\))(?:,va:\{i:a\.a\.i,j:a\.a\.j\},vb:\{i:a\.b\.i,j:a\.b\.j\})?)RX"),
		TEXT("$1,va:{i:a.a.i,j:a.a.j},vb:{i:a.b.i,j:a.b.j}"),
		/*bRequired=*/false
	});
}
