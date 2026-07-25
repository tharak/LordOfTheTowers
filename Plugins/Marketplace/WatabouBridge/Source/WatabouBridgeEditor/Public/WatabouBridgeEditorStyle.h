// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;
class ISlateStyle;

/**
 * Slate style set for the Watabou import editor UI. Hosts the generator rail icons (one SVG
 * per generator, named by generator id) plus the Link / Update toolbar icons.
 *
 * SVGs live in <Plugin>/Resources/Icons and are authored white so the rail can tint them
 * (white when idle, blue on the selected tab's blue background). A brush is registered only
 * for an SVG that actually exists on disk, so a not-yet-authored icon resolves to nullptr via
 * GetOptionalBrush() and the caller falls back to the generator's initial letter / button text.
 *
 * Registered at the top of the editor module's StartupModule (before the tab spawner) so the
 * style exists even when a previous session's layout restores the import tab during startup.
 */
class FWatabouBridgeEditorStyle
{
public:
	static void Register();
	static void Unregister();

	static FName GetStyleSetName();
	static const ISlateStyle& Get();

	/** Brush name for a generator's rail icon (e.g. "Watabou.Generator.perilous-shores"). */
	static FName GeneratorIconName(const FString& GeneratorId);

	/** Brush names for the two rail toolbar buttons (Link dropdown, Update). */
	static FName LinkIconName();
	static FName UpdateIconName();

private:
	static TSharedRef<FSlateStyleSet> Create();

	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
