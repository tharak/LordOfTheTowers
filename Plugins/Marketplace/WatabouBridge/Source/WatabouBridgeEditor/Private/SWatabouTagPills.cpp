// Copyright 2026 Timothé Lapetite

#include "SWatabouTagPills.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"
#include "Brushes/SlateRoundedBoxBrush.h"

void SWatabouTagPills::Construct(const FArguments& InArgs)
{
	IsTagSelectedDelegate = InArgs._IsTagSelected;
	OnToggleTagDelegate   = InArgs._OnToggleTag;

	InitPillStyle();

	ChildSlot
	[
		SAssignNew(Root, SVerticalBox)
	];

	Rebuild();
}

void SWatabouTagPills::InitPillStyle()
{
	// Derive from the editor's standard toggle button, then diverge in two ways:
	//  - Unchecked pills get a faint filled rounded box; the stock style draws nothing when off,
	//    which left every unselected tag as bare floating text.
	//  - Halve the style's wide 16px horizontal padding so noticeably more pills fit per row.
	PillStyle = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox");

	constexpr float CornerRadius = 4.0f;   // matches the checked (Primary) box
	PillStyle.SetUncheckedImage(FSlateRoundedBoxBrush(FStyleColors::Header, CornerRadius));
	PillStyle.SetPadding(FMargin(8.0f, 2.0f));
}

void SWatabouTagPills::SetGroups(const TArray<FWatabouTagGroup>& InGroups)
{
	Groups = InGroups;
	Rebuild();
}

void SWatabouTagPills::Rebuild()
{
	if (!Root.IsValid()) { return; }
	Root->ClearChildren();

	// Wrap width for the pill rows. SWrapBox defaults its PreferredSize (the wrap threshold) to
	// 100px and, with UseAllottedSize, only corrects it a frame later in Tick -- so a freshly-built
	// box (SetGroups recreates them on every generator switch) wraps the pills super-narrow, then
	// snaps wide once Tick runs: a visible multi-frame reflow. Seeding a fixed PreferredSize from
	// our already-laid-out width wraps correctly on the first frame instead. The settings column is
	// a fixed-width 320px box, so this never needs to adapt at runtime; the fallback only covers the
	// very first build (before this widget has been laid out, so its cached width is still zero).
	const float CachedWidth = GetCachedGeometry().GetLocalSize().X;
	const float WrapWidth = CachedWidth > 1.f ? CachedWidth : 300.f;

	for (const FWatabouTagGroup& Group : Groups)
	{
		if (Group.Tags.Num() == 0) { continue; }

		if (!Group.Label.IsEmpty())
		{
			// Uppercase, small, dimmed heading -- matches the group-chunk style used by the detail
			// customizations elsewhere (e.g. PCGExZGSettingsCustomization). The bottom gap sets the
			// title apart from its pills; the larger trailing gap on the pill row (below) is what
			// separates one group from the next.
			Root->AddSlot().AutoHeight().Padding(0.f, 7.f, 0.f, 6.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Group.Label.ToUpper()))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f)))
			];
		}

		const TSharedRef<SWrapBox> Wrap = SNew(SWrapBox)
			.PreferredSize(WrapWidth)   // fixed wrap width -> correct on the first frame (see above)
			.InnerSlotPadding(FVector2D(3.f, 3.f));

		// Capture the group by value so each pill's click closure carries its exclusivity info.
		const FWatabouTagGroup GroupCopy = Group;
		for (const FString& LocalTag : Group.Tags)
		{
			Wrap->AddSlot()
			[
				SNew(SCheckBox)
				.Style(&PillStyle)
				.IsChecked_Lambda([this, LocalTag]()
				{
					return IsTagSelectedDelegate.IsBound() && IsTagSelectedDelegate.Execute(LocalTag)
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, GroupCopy, LocalTag](ECheckBoxState)
				{
					OnToggleTagDelegate.ExecuteIfBound(GroupCopy, LocalTag);
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(LocalTag))
					.Margin(FMargin(2.f, 1.f))
				]
			];
		}

		// Trailing gap below each group's pills so adjacent groups read as distinct blocks.
		Root->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			Wrap
		];
	}
}
