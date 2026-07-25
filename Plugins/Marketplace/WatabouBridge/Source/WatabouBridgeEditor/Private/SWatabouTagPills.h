// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"         // FCheckBoxStyle
#include "WatabouGeneratorSettings.h"   // FWatabouTagGroup

/** Query whether a tag is currently selected. */
DECLARE_DELEGATE_RetVal_OneParam(bool, FWatabouIsTagSelected, const FString& /*Tag*/);

/** Toggle a tag within its group; the owner enforces mutual exclusion using the group. */
DECLARE_DELEGATE_TwoParams(FWatabouOnToggleTag, const FWatabouTagGroup& /*Group*/, const FString& /*Tag*/);

/**
 * Renders a generator's tag vocabulary as rows of toggle-button "pills", one row per
 * FWatabouTagGroup. Presentation only: selection state is read via IsTagSelected and clicks
 * forwarded via OnToggleTag -- the owning panel holds the authoritative settings and enforces
 * mutual exclusion. Call SetGroups() when the active generator changes.
 */
class SWatabouTagPills : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWatabouTagPills) {}
		SLATE_EVENT(FWatabouIsTagSelected, IsTagSelected)
		SLATE_EVENT(FWatabouOnToggleTag, OnToggleTag)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Swap in a new generator's tag groups and rebuild the pill rows. */
	void SetGroups(const TArray<FWatabouTagGroup>& InGroups);

private:
	void Rebuild();

	/** Builds PillStyle once: a tighter-padded toggle button with a faint fill when unchecked. */
	void InitPillStyle();

	TArray<FWatabouTagGroup> Groups;
	FWatabouIsTagSelected IsTagSelectedDelegate;
	FWatabouOnToggleTag OnToggleTagDelegate;
	TSharedPtr<SVerticalBox> Root;

	/** Owned so SCheckBox (which keeps a raw pointer to its style) stays valid across rebuilds. */
	FCheckBoxStyle PillStyle;
};
