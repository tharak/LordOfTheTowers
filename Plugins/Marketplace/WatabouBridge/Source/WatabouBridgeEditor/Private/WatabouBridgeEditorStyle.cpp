// Copyright 2026 Timothé Lapetite

#include "WatabouBridgeEditorStyle.h"

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"      // FSlateVectorImageBrush
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

TSharedPtr<FSlateStyleSet> FWatabouBridgeEditorStyle::StyleInstance;

FName FWatabouBridgeEditorStyle::GetStyleSetName()
{
	static const FName StyleName(TEXT("WatabouBridgeEditorStyle"));
	return StyleName;
}

FName FWatabouBridgeEditorStyle::GeneratorIconName(const FString& GeneratorId)
{
	return FName(*FString::Printf(TEXT("Watabou.Generator.%s"), *GeneratorId));
}

FName FWatabouBridgeEditorStyle::LinkIconName()   { return FName(TEXT("Watabou.Link")); }
FName FWatabouBridgeEditorStyle::UpdateIconName() { return FName(TEXT("Watabou.Update")); }

void FWatabouBridgeEditorStyle::Register()
{
	if (StyleInstance.IsValid()) { return; }
	StyleInstance = Create();
	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FWatabouBridgeEditorStyle::Unregister()
{
	if (!StyleInstance.IsValid()) { return; }
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	StyleInstance.Reset();
}

const ISlateStyle& FWatabouBridgeEditorStyle::Get()
{
	check(StyleInstance.IsValid());
	return *StyleInstance;
}

TSharedRef<FSlateStyleSet> FWatabouBridgeEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WatabouBridge"));
	if (!Plugin.IsValid())
	{
		// No content root resolvable -- leave the set empty; every lookup falls back gracefully.
		return Style;
	}
	Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Icons"));

	const FVector2D RailIconSize(32.0f, 32.0f);   // centered in the 40x40 tab box -> ~4px breathing room
	const FVector2D ToolIconSize(24.0f, 24.0f);

	// Register a brush only when its SVG exists, so a not-yet-authored icon resolves to nullptr
	// (GetOptionalBrush) and callers fall back to the generator's letter / button text.
	auto RegisterIfPresent = [&Style](const FName BrushName, const FString& RelativeName, const FVector2D& Size)
	{
		const FString Path = Style->RootToContentDir(RelativeName, TEXT(".svg"));
		if (FPaths::FileExists(Path))
		{
			Style->Set(BrushName, new FSlateVectorImageBrush(Path, Size));
		}
	};

	// Generator rail icons. Filenames mirror the generator ids (also the Resources/Bundles/<id>
	// folder names). Hard-coded rather than read from the registry because this set is built at
	// module startup, before the per-generator modules have populated FWatabouGeneratorRegistry.
	static const TCHAR* GeneratorIds[] =
	{
		TEXT("city-generator"),
		TEXT("dwellings"),
		TEXT("neighbourhood"),
		TEXT("one-page-dungeon"),
		TEXT("perilous-shores"),
		TEXT("urban-places"),
		TEXT("village-generator"),
	};
	for (const TCHAR* Id : GeneratorIds)
	{
		RegisterIfPresent(GeneratorIconName(Id), Id, RailIconSize);
	}

	RegisterIfPresent(LinkIconName(),   TEXT("link"),   ToolIconSize);
	RegisterIfPresent(UpdateIconName(), TEXT("update"), ToolIconSize);

	return Style;
}
