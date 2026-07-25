// Copyright 2026 Timothé Lapetite

#include "OnePageDungeonInfo.h"
#include "OnePageDungeonParser.h"
#include "OnePageDungeonProcessor.h"
#include "OnePageDungeonSettings.h"

TSharedPtr<IWatabouParser> FOnePageDungeonInfo::CreateParser() const
{
	return MakeShared<FOnePageDungeonParser>();
}

TSharedPtr<IWatabouProcessor> FOnePageDungeonInfo::CreateProcessor() const
{
	return MakeShared<FOnePageDungeonProcessor>();
}

FInstancedStruct FOnePageDungeonInfo::CreateDefaultSettings() const
{
	return MakeDefaultSettingsFromQuery<FOnePageDungeonSettings>(Id, DefaultQuery);
}

TArray<FWatabouTagGroup> FOnePageDungeonInfo::GetTagGroups() const
{
	// Sourced from cd.getPublic + cd.resolve (Dungeon.js v1.2.7). No defaults injected. The bundle's
	// 11th exclusivity group, Branching {string | secret}, overlaps the Secrets group on "secret",
	// so it can't be a clean pill group: "secret" lives in Secrets below, and "string" is offered as
	// an independent toggle. The bundle's resolve() still enforces string XOR secret on load.
	FWatabouTagGroup Symmetry;
	Symmetry.Label = TEXT("Symmetry");
	Symmetry.Tags  = { TEXT("chaotic"), TEXT("ordered") };
	Symmetry.bMutuallyExclusive = true;

	FWatabouTagGroup Corridors;
	Corridors.Label = TEXT("Corridors");
	Corridors.Tags  = { TEXT("winding"), TEXT("compact") };
	Corridors.bMutuallyExclusive = true;

	FWatabouTagGroup RoomSize;
	RoomSize.Label = TEXT("Room size");
	RoomSize.Tags  = { TEXT("cramped"), TEXT("spacious") };
	RoomSize.bMutuallyExclusive = true;

	FWatabouTagGroup DungeonSize;
	DungeonSize.Label = TEXT("Dungeon size");
	DungeonSize.Tags  = { TEXT("large"), TEXT("medium"), TEXT("small") };
	DungeonSize.bMutuallyExclusive = true;

	FWatabouTagGroup RoomShape;
	RoomShape.Label = TEXT("Room shape");
	RoomShape.Tags  = { TEXT("round"), TEXT("square") };
	RoomShape.bMutuallyExclusive = true;

	FWatabouTagGroup Water;
	Water.Label = TEXT("Water");
	Water.Tags  = { TEXT("dry"), TEXT("wet"), TEXT("flooded") };
	Water.bMutuallyExclusive = true;

	FWatabouTagGroup Levels;
	Levels.Label = TEXT("Levels");
	Levels.Tags  = { TEXT("single-level"), TEXT("multi-level") };
	Levels.bMutuallyExclusive = true;

	FWatabouTagGroup Secrets;
	Secrets.Label = TEXT("Secrets");
	Secrets.Tags  = { TEXT("secret"), TEXT("no secrets") };
	Secrets.bMutuallyExclusive = true;

	FWatabouTagGroup Backdoor;
	Backdoor.Label = TEXT("Backdoor");
	Backdoor.Tags  = { TEXT("backdoor"), TEXT("no backdoor") };
	Backdoor.bMutuallyExclusive = true;

	FWatabouTagGroup Steps;
	Steps.Label = TEXT("Steps");
	Steps.Tags  = { TEXT("flat"), TEXT("deep") };
	Steps.bMutuallyExclusive = true;

	// treasure..crumbling are truly independent; "string" carries the Branching cross-constraint
	// noted above (string XOR secret), which the pill UI can't enforce (the bundle resolves it on load).
	FWatabouTagGroup Features;
	Features.Label = TEXT("Features");
	Features.Tags  = { TEXT("treasure"), TEXT("dangerous"), TEXT("colonnades"), TEXT("temple"),
	                   TEXT("tomb"), TEXT("dwelling"), TEXT("crumbling"), TEXT("string") };
	Features.bMutuallyExclusive = false;

	return { Symmetry, Corridors, RoomSize, DungeonSize, RoomShape, Water, Levels, Secrets, Backdoor, Steps, Features };
}

FWatabouResourceManifest FOnePageDungeonInfo::GetResourceManifest() const
{
	FWatabouResourceManifest M;
	M.UpstreamBaseUrl = TEXT("https://watabou.github.io/one-page-dungeon");
	M.BundleScript    = BundleScript;
	M.TopLevelFiles   = { TEXT("favicon.png") };
	// One Page Dungeon uses lowercase "assets/" (the others use capital "Assets/").
	M.AssetsDir       = TEXT("assets");
	M.FontStems = {
		TEXT("Grenze-Bold"),
		TEXT("Neuton-Italic"),
		TEXT("Neuton-Regular"),
		TEXT("Neuton-ExtraBold"),
		TEXT("ShareTech-Regular"),
		TEXT("ShareTechMono-Regular"),
	};
	M.RuntimeAssets = {
		TEXT("ancient.json"), TEXT("default.json"), TEXT("demons.txt"),
		TEXT("grammar.json"), TEXT("light.json"),   TEXT("link.json"),
		TEXT("modern.json"),  TEXT("tags.txt"),
	};
	return M;
}

FOnePageDungeonInfo::FOnePageDungeonInfo()
{
	Id              = TEXT("one-page-dungeon");
	ShortId         = TEXT("opd");
	DisplayName     = TEXT("One Page Dungeon");
	BundleScript    = TEXT("Dungeon.js");
	BundleVersion   = TEXT("");
	// A default tag is required for determinism: an empty tags= makes the bundle re-roll tags on
	// every load (cd.random). "small" pins a compact dungeon (Dungeon-size group, so it reads back
	// cleanly as a single selected pill).
	DefaultQuery    = TEXT("?seed=4567890&tags=small");
	JsReadinessExpr = TEXT("window.__hx['com.watabou.dungeon.scenes.ViewScene'].inst && window.__hx['com.watabou.dungeon.scenes.ViewScene'].inst.dungeon");
	JsTriggerExpr   = TEXT("window.__hx['com.watabou.dungeon.Formats'].exportJSON(window.__hx['com.watabou.dungeon.scenes.ViewScene'].inst.dungeon)");

	BundlePatch.BundleVersion        = BundleVersion;
	BundlePatch.ExpectedRegistryVar  = TEXT("g");
}
