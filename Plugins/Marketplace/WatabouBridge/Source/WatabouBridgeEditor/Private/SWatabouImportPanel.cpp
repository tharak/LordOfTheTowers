// Copyright 2026 Timothé Lapetite

#include "SWatabouImportPanel.h"
#include "SWatabouTagPills.h"
#include "WatabouImportConfig.h"
#include "WatabouAssetBase.h"
#include "WatabouAssetBuilder.h"
#include "WatabouBridge.h"
#include "WatabouBridgeEditorModule.h"
#include "WatabouBridgeJSObject.h"
#include "WatabouBrowserPool.h"
#include "WatabouGeneratorInfo.h"
#include "WatabouGeneratorRegistry.h"
#include "WatabouGeneratorSettings.h"
#include "WatabouRecursiveImporter.h"
#include "WatabouResourceUpdater.h"
#include "WatabouSeedRef.h"
#include "WatabouShim.h"
#include "WatabouUrlUtils.h"

#include "StructUtils/InstancedStruct.h"
#include "UObject/StructOnScope.h"
#include "PropertyEditorModule.h"
#include "IStructureDetailsView.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"

#include "WatabouBridgeEditorStyle.h"

#include "SWebBrowser.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"

#include "HAL/PlatformApplicationMisc.h"
#include "Dialogs/DlgPickPath.h"   // SDlgPickPath -- content-folder picker for the import target dir

#define LOCTEXT_NAMESPACE "SWatabouImportPanel"

void SWatabouImportPanel::Construct(const FArguments& InArgs)
{
	Bridge = NewObject<UWatabouBridgeJSObject>(GetTransientPackage(), UWatabouBridgeJSObject::StaticClass());
	Bridge->OnExportReceivedNative.AddSP(this, &SWatabouImportPanel::HandleExportReceived);
	Bridge->OnReadySignalNative.AddSP(this, &SWatabouImportPanel::HandleBridgeReady);
	Bridge->OnErrorSignalNative.AddSP(this, &SWatabouImportPanel::HandleBridgeError);

	// Seed the module recursive importer's Follow filter with every generator on first open,
	// matching the previous panel-owned default. Skip if already configured (returning user).
	if (TSharedPtr<FWatabouRecursiveImporter> Importer = FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
	{
		FWatabouRecursiveImporter::FSettings& S = Importer->MutableSettings();
		if (S.AllowedTargetGenerators.Num() == 0)
		{
			for (const TSharedRef<FWatabouGeneratorInfo>& Info : FWatabouGeneratorRegistry::Get().GetAll())
			{
				S.AllowedTargetGenerators.Add(FName(*Info->Id));
			}
		}
		ImporterProgressHandle = Importer->OnProgressChanged.AddSP(this, &SWatabouImportPanel::HandleRecursiveProgress);
	}
	if (TSharedPtr<FWatabouBrowserPool> Pool = FWatabouBridgeEditorModule::Get().GetBrowserPool())
	{
		PoolChangedHandle = Pool->OnChanged.AddSP(this, &SWatabouImportPanel::HandlePoolChanged);
	}

	// Build per-generator settings: defaults from each generator's DefaultQuery, then overridden
	// by the last-used query persisted in config. Populated once -- the map isn't grown after,
	// so raw pointers into the held FInstancedStructs stay stable for the panel's lifetime.
	const UWatabouImportConfig* Config = UWatabouImportConfig::Get();
	const TArray<TSharedRef<FWatabouGeneratorInfo>>& AllGens = FWatabouGeneratorRegistry::Get().GetAll();
	for (const TSharedRef<FWatabouGeneratorInfo>& Info : AllGens)
	{
		FInstancedStruct Settings = Info->CreateDefaultSettings();
		const FString Saved = Config->GetSavedQuery(Info->Id);
		if (!Saved.IsEmpty() && Settings.IsValid())
		{
			Settings.GetMutable<FWatabouGeneratorSettings>().LoadFrom(
				WatabouUrlUtils::ParseQueryString(Info->Id, Saved));
		}
		SettingsByGenerator.Add(FName(*Info->Id), MoveTemp(Settings));
	}
	if (AllGens.Num() > 0) { ActiveGenerator = AllGens[0]; }

	// Structure details view for the active generator's typed params. Seed + SelectedTags are
	// hidden (rendered as the custom seed row + tag pills instead).
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.bShowScrollBar = false;   // the settings column's own SScrollBox handles scrolling
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	FStructureDetailsViewArgs StructArgs;
	StructDetailsView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, nullptr);
	// Seed + SelectedTags are non-Edit UPROPERTYs, so they don't appear here -- the panel renders
	// them as the custom seed row + tag pills. Only each generator's typed params show.
	StructDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SWatabouImportPanel::OnDetailsChanged);

	ChildSlot
	[
		SNew(SOverlay)

		// The four-region panel sits above a full-width attribution footer. Both live in the
		// first overlay slot so the blocking scrim (next slot) still covers the whole window --
		// footer included -- keeping the modal "block everything" guarantee intact.
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SHorizontalBox)

				// Region 1: generator rail.
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)
				[
					SNew(SBox).WidthOverride(56.f)
					[ BuildGeneratorRail() ]
				]

				// Region 2: settings column.
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 6.f)
				[
					SNew(SBox).WidthOverride(320.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[ BuildSettingsColumn() ]
					]
				]

				// Region 3: render view.
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f)
				[
					SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder")).Padding(2.f)
					[
						SAssignNew(WebBrowser, SWebBrowser)
						.InitialURL(TEXT("about:blank"))
						.ShowControls(false).ShowAddressBar(false).ShowInitialThrobber(true).SupportsTransparency(false)
						.OnLoadCompleted(FSimpleDelegate::CreateSP(this, &SWatabouImportPanel::HandleLoadCompleted))
						.OnConsoleMessage(FOnConsoleMessageDelegate::CreateSP(this, &SWatabouImportPanel::HandleConsoleMessage))
						.OnUrlChanged(FOnTextChanged::CreateSP(this, &SWatabouImportPanel::HandleUrlChanged))
					]
				]

				// Region 4: fixed import controls.
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)
				[
					SNew(SBox).WidthOverride(220.f)
					[ BuildImportControls() ]
				]
			]

			// Attribution footer -- full-width, pinned under the panel (and under the scrim).
			+ SVerticalBox::Slot().AutoHeight()
			[ BuildFooter() ]
		]

		// Modal scrim shown while a headless recursive batch OR a generator update runs (progress,
		// plus Abort for batches). Blocks the whole window so the work -- which proceeds away from
		// this view -- can't be disturbed by stray clicks.
		+ SOverlay::Slot()
		[ BuildBlockingOverlay() ]
	];

	// Route the JS->C++ bridge through window.ue.watabou in addition to the console-log sentinel.
	// The console path only reaches C++ on the CEF (Windows) backend; macOS uses the WKWebView
	// backend which never fires OnConsoleMessage, so this bound object is the only channel that
	// works there. Bind now -- before the deferred first load -- and permanently, so it survives
	// the bundle's in-place regenerations. See UWatabouBridgeJSObject / WatabouShim.cpp `deliver`.
	if (WebBrowser.IsValid())
	{
		WebBrowser->BindUObject(TEXT("watabou"), Bridge.Get(), /*bIsPermanent=*/true);
	}

	// Bind the initial generator's settings UI immediately so the panel shows them on open.
	if (ActiveGenerator.IsValid())
	{
		BindActiveGeneratorUI();
	}

	// Defer the first browser load one tick -- the SWebBrowser isn't realized until after Construct
	// returns, and a LoadURL before that can be dropped. PerformInitialLoad also consumes a queued
	// OpenSeedRef request (asset "Open in Import Window" on a freshly-spawned tab) so the asset's seed
	// -- not the restored last-used settings -- drives the first navigation.
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double, float) { PerformInitialLoad(); return EActiveTimerReturnType::Stop; }));
}

SWatabouImportPanel::~SWatabouImportPanel()
{
	if (TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
	{
		Imp->OnProgressChanged.Remove(ImporterProgressHandle);
	}
	if (TSharedPtr<FWatabouBrowserPool> Pool = FWatabouBridgeEditorModule::Get().GetBrowserPool())
	{
		Pool->OnChanged.Remove(PoolChangedHandle);
	}
}

void SWatabouImportPanel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Bridge);
}

#pragma region Layout builders

TSharedRef<SWidget> SWatabouImportPanel::BuildGeneratorRail()
{
	const TAttribute<bool> Interactive = TAttribute<bool>::CreateRaw(this, &SWatabouImportPanel::GetInteractiveEnabled);

	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);

	// Generator tabs (scroll if the list outgrows the rail).
	const TSharedRef<SScrollBox> Tabs = SNew(SScrollBox);
	for (const TSharedRef<FWatabouGeneratorInfo>& Info : FWatabouGeneratorRegistry::Get().GetAll())
	{
		const FString Id      = Info->Id;
		const FString Display = Info->DisplayName;
		const FString Initial = Display.Left(1).ToUpper();

		// White icon when an SVG is installed (white reads on the selected tab's blue box; dimmed
		// when idle); otherwise fall back to the generator's initial letter.
		const FSlateBrush* GenIcon = FWatabouBridgeEditorStyle::Get().GetOptionalBrush(
			FWatabouBridgeEditorStyle::GeneratorIconName(Id), nullptr, nullptr);

		TSharedRef<SWidget> TabInner = GenIcon
			? StaticCastSharedRef<SWidget>(
				SNew(SImage)
				.Image(GenIcon)
				.ColorAndOpacity_Lambda([this, Id]() -> FSlateColor
				{
					const bool bActive = ActiveGenerator.IsValid() && ActiveGenerator->Id == Id;
					return bActive ? FStyleColors::White : FStyleColors::Foreground;
				}))
			: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(Initial)));

		Tabs->AddSlot().Padding(2.f)
		[
			SNew(SCheckBox)
			.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
			// Override the style's wide 16px h-padding; otherwise the 40px icon box (72px with
			// padding) is crushed into the 56px rail, squeezing the glyph.
			.Padding(FMargin(4.f))
			.ToolTipText(FText::FromString(Display))
			.IsEnabled(Interactive)
			.IsChecked_Lambda([this, Id]()
			{
				return (ActiveGenerator.IsValid() && ActiveGenerator->Id == Id)
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Id](ECheckBoxState NewState)
			{
				if (NewState == ECheckBoxState::Checked)
				{
					SetActiveGeneratorById(Id);
				}
				else if (ActiveGenerator.IsValid() && ActiveGenerator->Id == Id)
				{
					// Clicking the already-active tab is a reload request (regenerate current settings).
					ApplyAndLoad();
				}
			})
			[
				SNew(SBox).WidthOverride(40.f).HeightOverride(40.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[ TabInner ]
			]
		];
	}

	Rail->AddSlot().FillHeight(1.f) [ Tabs ];

	// Load-from-link: a dropdown with a URL field (pre-filled from the clipboard if it holds one).
	const FSlateBrush* LinkIcon = FWatabouBridgeEditorStyle::Get().GetOptionalBrush(
		FWatabouBridgeEditorStyle::LinkIconName(), nullptr, nullptr);
	TSharedRef<SWidget> LinkContent = LinkIcon
		? StaticCastSharedRef<SWidget>(
			SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[ SNew(SImage).Image(LinkIcon).ColorAndOpacity(FStyleColors::Foreground) ])
		: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("LoadLink", "Link")).Justification(ETextJustify::Center));

	Rail->AddSlot().AutoHeight().Padding(2.f)
	[
		SAssignNew(LinkComboButton, SComboButton)
		.HasDownArrow(false)
		.ToolTipText(LOCTEXT("LoadLinkTip", "Load settings from a Watabou URL (paste, or copy one first)"))
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SWatabouImportPanel::BuildLoadFromLinkMenu))
		.ButtonContent()
		[ LinkContent ]
	];

	// Update Generators (pinned bottom).
	const FSlateBrush* UpdateIcon = FWatabouBridgeEditorStyle::Get().GetOptionalBrush(
		FWatabouBridgeEditorStyle::UpdateIconName(), nullptr, nullptr);
	TSharedRef<SWidget> UpdateContent = UpdateIcon
		? StaticCastSharedRef<SWidget>(SNew(SImage).Image(UpdateIcon).ColorAndOpacity(FStyleColors::Foreground))
		: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("UpdateShort", "Update")));

	Rail->AddSlot().AutoHeight().Padding(2.f)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		// The Button style adds 12px ButtonMargins on top of ContentPadding; on a full-width rail
		// button that leaves too little room for the 24px icon and crushes it. Override the style
		// padding to zero (and keep it stable on press) so only the small ContentPadding remains.
		.ContentPadding(FMargin(6.f, 4.f))
		.NormalPaddingOverride(FMargin(0.f))
		.PressedPaddingOverride(FMargin(0.f))
		.ToolTipText(LOCTEXT("UpdateTip", "Download every registered generator's bundle JS + assets from upstream, patch them, write a versioned cache snapshot, then restart the local server."))
		.IsEnabled(Interactive)
		.OnClicked(this, &SWatabouImportPanel::OnUpdateGeneratorsClicked)
		[ UpdateContent ]
	];

	return Rail;
}

TSharedRef<SWidget> SWatabouImportPanel::BuildSettingsColumn()
{
	return SNew(SVerticalBox)

	// Generator name header.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		.Text_Lambda([this]()
		{
			return FText::FromString(ActiveGenerator.IsValid() ? ActiveGenerator->DisplayName : FString());
		})
	]

	// Seed row (textbox + re-roll).
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[ SNew(STextBlock).Text(LOCTEXT("SeedLabel", "Seed")) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SAssignNew(SeedTextBox, SEditableTextBox)
			.HintText(LOCTEXT("SeedHint", "(random)"))
			.OnTextCommitted(this, &SWatabouImportPanel::OnSeedTextCommitted)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("RerollTip", "Generate a new random seed and reload"))
			.OnClicked(this, &SWatabouImportPanel::OnRerollSeed)
			[ SNew(STextBlock).Text(LOCTEXT("Reroll", "Reroll")) ]
		]
	]

	// Typed params (width/height/size/...) via the structure details view.
	+ SVerticalBox::Slot().AutoHeight()
	[
		StructDetailsView->GetWidget().ToSharedRef()
	]

	// Tag pills.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		SAssignNew(TagPills, SWatabouTagPills)
		.IsTagSelected(FWatabouIsTagSelected::CreateSP(this, &SWatabouImportPanel::IsTagSelected))
		.OnToggleTag(FWatabouOnToggleTag::CreateSP(this, &SWatabouImportPanel::ToggleTag))
	];
}

TSharedRef<SWidget> SWatabouImportPanel::BuildImportControls()
{
	const TAttribute<bool> Interactive = TAttribute<bool>::CreateRaw(this, &SWatabouImportPanel::GetInteractiveEnabled);

	return SNew(SVerticalBox)

	// Import -- primary call to action: taller + the editor's blue PrimaryButton style.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(SBox).HeightOverride(34.f)
		[
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ToolTipText(LOCTEXT("ImportTip", "Fire the generator's JSON export against the current render and save it as a uasset."))
			.IsEnabled(Interactive)
			.OnClicked(this, &SWatabouImportPanel::OnImportClicked)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
				.Text(LOCTEXT("ImportBtn", "Import"))
			]
		]
	]

	// Import target folder: where new top-level imports are saved (recursive children still nest
	// under their parent). Editable path + a content-folder Browse picker, persisted in config.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[ SNew(STextBlock).Text(LOCTEXT("SaveToLabel", "Save to")) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SAssignNew(TargetDirTextBox, SEditableTextBox)
			.Text(FText::FromString(UWatabouImportConfig::Get()->GetImportTargetDir()))
			.ToolTipText(LOCTEXT("SaveToTip", "Content folder new top-level imports are saved into. Recursive children still nest under their parent asset."))
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
			{
				UWatabouImportConfig::Get()->SetImportTargetDir(Text.ToString());
				if (TargetDirTextBox.IsValid())
				{
					TargetDirTextBox->SetText(FText::FromString(UWatabouImportConfig::Get()->GetImportTargetDir()));
				}
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("SaveToBrowseTip", "Pick the import target folder from the Content Browser"))
			.OnClicked(this, &SWatabouImportPanel::OnBrowseTargetDir)
			[ SNew(STextBlock).Text(LOCTEXT("SaveToBrowse", "...")) ]
		]
	]

	// Import recursively + Follow filter.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bRecursiveOnImport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bRecursiveOnImport = (NewState == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("RecursiveTip", "After each successful manual import, walk the asset for nested seeds and import each one through the headless pool. Continues until the queue drains."))
			[ SNew(STextBlock).Text(LOCTEXT("RecursiveLabel", "Import recursively")) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("FollowTip", "Which generators recursive imports may follow into."))
			.OnGetMenuContent_Lambda([this]() { return BuildGeneratorFilterRow(); })
			.ButtonContent()
			[ SNew(STextBlock).Text(LOCTEXT("FollowLabel", "Follow")) ]
		]
	]

	// Force.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([]()
		{
			TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
			return Imp.IsValid() && Imp->GetSettings().bForceReimport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([](ECheckBoxState NewState)
		{
			if (TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
			{
				Imp->MutableSettings().bForceReimport = (NewState == ECheckBoxState::Checked);
			}
		})
		.ToolTipText(LOCTEXT("ForceTip", "Re-import refs even when an asset with matching CanonicalKey is already in the project."))
		[ SNew(STextBlock).Text(LOCTEXT("ForceLabel", "Force")) ]
	]

	// Abort on failure.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([]()
		{
			TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
			return Imp.IsValid() && Imp->GetSettings().OnFailure == FWatabouRecursiveImporter::EOnFailure::Abort
				? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([](ECheckBoxState NewState)
		{
			if (TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
			{
				Imp->MutableSettings().OnFailure = (NewState == ECheckBoxState::Checked)
					? FWatabouRecursiveImporter::EOnFailure::Abort
					: FWatabouRecursiveImporter::EOnFailure::Continue;
			}
		})
		.ToolTipText(LOCTEXT("AbortOnFailTip", "Stop the batch on the first failed import (default: continue past failures)."))
		[ SNew(STextBlock).Text(LOCTEXT("AbortOnFailLabel", "Abort on Failure")) ]
	]

	// Slots.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[ SNew(STextBlock).Text(LOCTEXT("SlotsLabel", "Slots")) ]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SSpinBox<int32>)
			.MinValue(FWatabouBrowserPool::MinSlots)
			.MaxValue(FWatabouBrowserPool::MaxSlots)
			.MinSliderValue(FWatabouBrowserPool::MinSlots)
			.MaxSliderValue(FWatabouBrowserPool::MaxSlots)
			.AlwaysUsesDeltaSnap(true)
			.Delta(1)
			.ToolTipText(LOCTEXT("SlotsTip", "Number of hidden browsers used in parallel for recursive batches. 1 = sequential. Higher speeds up large batches but uses more memory."))
			.Value_Lambda([]()
			{
				TSharedPtr<FWatabouBrowserPool> Pool = FWatabouBridgeEditorModule::Get().GetBrowserPool();
				return Pool.IsValid() ? Pool->GetSlotCount() : 1;
			})
			.OnValueCommitted_Lambda([](int32 NewValue, ETextCommit::Type)
			{
				if (TSharedPtr<FWatabouBrowserPool> Pool = FWatabouBridgeEditorModule::Get().GetBrowserPool())
				{
					Pool->SetSlotCount(NewValue);
				}
			})
		]
	]

	// Status surface (fills the remaining space).
	+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 4.f)
	[
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder")).Padding(2.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(OutputField, SMultiLineEditableText)
				.IsReadOnly(true)
				.AllowMultiLine(true)
				.AutoWrapText(true)
				.Text(LOCTEXT("OutputReady", "(ready)"))
			]
		]
	]

	// Copy -- its own full-width line directly under the status field (was cramped beside the toggles).
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.Text(LOCTEXT("CopyBtn", "Copy"))
		.ToolTipText(LOCTEXT("CopyTip", "Copy the last imported raw JSON to the clipboard."))
		.OnClicked(this, &SWatabouImportPanel::OnCopyOutputClicked)
	]

	// Mute / Auto-save toggles.
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]() { return WatabouShim::IsCefLogMuted() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([](ECheckBoxState NewState) { WatabouShim::SetCefLogMuted(NewState == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("MuteCefTip", "Drop CEF/bundle console.log chatter from the editor log. Bridge sentinel messages always pass through."))
			[ SNew(STextBlock).Text(LOCTEXT("MuteCefLabel", "Mute logs")) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]() { return WatabouShim::IsAutoSaveOnImport() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([](ECheckBoxState NewState) { WatabouShim::SetAutoSaveOnImport(NewState == ECheckBoxState::Checked); })
			.ToolTipText(LOCTEXT("AutoSaveTip", "When on, every successful import writes the uasset to disk immediately. When off, the asset is created dirty -- save with Ctrl+S."))
			[ SNew(STextBlock).Text(LOCTEXT("AutoSaveLabel", "Auto-save")) ]
		]
	]

	// Open in browser (bottom).
	+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.Text(LOCTEXT("OpenInBrowserBtn", "Open in browser"))
		.ToolTipText(LOCTEXT("OpenInBrowserTip", "Open the current generator URL in your OS web browser."))
		.OnClicked(this, &SWatabouImportPanel::OnOpenInBrowserClicked)
	];
}

TSharedRef<SWidget> SWatabouImportPanel::BuildGeneratorFilterRow()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	// All / None convenience buttons across the top.
	List->AddSlot().AutoHeight().Padding(2.f, 2.f, 2.f, 6.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("FollowAll", "All"))
			.ToolTipText(LOCTEXT("FollowAllTip", "Allow recursion to follow into every generator."))
			.OnClicked_Lambda([]() -> FReply
			{
				if (TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
				{
					TSet<FName>& Allowed = Imp->MutableSettings().AllowedTargetGenerators;
					Allowed.Reset();
					for (const TSharedRef<FWatabouGeneratorInfo>& Info : FWatabouGeneratorRegistry::Get().GetAll())
					{
						Allowed.Add(FName(*Info->Id));
					}
				}
				return FReply::Handled();
			})
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("FollowNone", "None"))
			.ToolTipText(LOCTEXT("FollowNoneTip", "Stop recursion from following into any generator."))
			.OnClicked_Lambda([]() -> FReply
			{
				if (TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
				{
					Imp->MutableSettings().AllowedTargetGenerators.Reset();
				}
				return FReply::Handled();
			})
		]
	];

	// One generator per line.
	for (const TSharedRef<FWatabouGeneratorInfo>& Info : FWatabouGeneratorRegistry::Get().GetAll())
	{
		const FName GenId(*Info->Id);
		const FString DisplayName = Info->DisplayName;

		List->AddSlot().AutoHeight().Padding(2.f, 1.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([GenId]()
			{
				TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
				if (!Imp.IsValid()) { return ECheckBoxState::Unchecked; }
				return Imp->GetSettings().AllowedTargetGenerators.Contains(GenId)
					? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([GenId](ECheckBoxState NewState)
			{
				TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
				if (!Imp.IsValid()) { return; }
				if (NewState == ECheckBoxState::Checked)
				{
					Imp->MutableSettings().AllowedTargetGenerators.Add(GenId);
				}
				else
				{
					Imp->MutableSettings().AllowedTargetGenerators.Remove(GenId);
				}
			})
			[ SNew(STextBlock).Text(FText::FromString(DisplayName)) ]
		];
	}

	return SNew(SBox).MinDesiredWidth(190.f).Padding(4.f) [ List ];
}

TSharedRef<SWidget> SWatabouImportPanel::BuildBlockingOverlay()
{
	return SNew(SBorder)
		.Visibility(TAttribute<EVisibility>::CreateRaw(this, &SWatabouImportPanel::GetBlockingOverlayVisibility))
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.75f))   // dark translucent scrim
		.Padding(0.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(340.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
				.Padding(20.f)
				[
					SNew(SVerticalBox)

					// Title -- names the active operation.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f).HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						.Text_Lambda([this]() -> FText
						{
							TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
							if (Imp.IsValid() && Imp->IsRunning()) { return LOCTEXT("BatchOverlayTitle", "Recursive import in progress"); }
							if (bUpdating)                        { return LOCTEXT("UpdateOverlayTitle", "Updating generators"); }
							return FText::GetEmpty();
						})
					]

					// Marquee for both modes. The batch Total grows as nested seeds surface (a
					// determinate ratio would regress); updates reuse the same bar with a file count
					// below. Its active timer keeps the card repainting so the count text stays live.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
					[
						SNew(SProgressBar)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f).HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Justification(ETextJustify::Center)
						.Text_Lambda([this]() -> FText
						{
							TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
							if (Imp.IsValid() && Imp->IsRunning())
							{
								const FWatabouRecursiveImporter::FProgress& P = Imp->GetProgress();
								const FString Counts = FString::Printf(TEXT("%d / %d done    %d in flight    %d failed"),
									P.Done, P.Total, P.InFlight, P.Failed);
								return (P.State == FWatabouRecursiveImporter::EBatchState::Cancelling)
									? FText::FromString(FString::Printf(TEXT("Cancelling -- %s"), *Counts))
									: FText::FromString(Counts);
							}
							if (bUpdating && ActiveUpdater.IsValid())
							{
								return FText::FromString(FString::Printf(TEXT("%d / %d files"),
									ActiveUpdater->GetCompleted(), ActiveUpdater->GetTotal()));
							}
							return FText::GetEmpty();
						})
					]

					// Abort -- batch only; the resource updater has no cancel API, so the row collapses
					// during a generator update.
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SBox).WidthOverride(120.f)
						.Visibility(TAttribute<EVisibility>::CreateLambda([]() -> EVisibility
						{
							TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
							return (Imp.IsValid() && Imp->IsRunning()) ? EVisibility::Visible : EVisibility::Collapsed;
						}))
						[
							SNew(SButton)
							.HAlign(HAlign_Center)
							.ToolTipText(LOCTEXT("BatchAbortTip", "Drop the queue and stop after the in-flight imports finish."))
							.IsEnabled_Lambda([]()
							{
								TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
								return Imp.IsValid() && Imp->GetProgress().State == FWatabouRecursiveImporter::EBatchState::Running;
							})
							.OnClicked(this, &SWatabouImportPanel::OnAbortBatch)
							[
								SNew(STextBlock)
								.Text_Lambda([]() -> FText
								{
									TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
									const bool bCancelling = Imp.IsValid()
										&& Imp->GetProgress().State == FWatabouRecursiveImporter::EBatchState::Cancelling;
									return bCancelling ? LOCTEXT("BatchAborting", "Aborting...") : LOCTEXT("BatchAbort", "Abort");
								})
							]
						]
					]
				]
			]
		];
}

EVisibility SWatabouImportPanel::GetBlockingOverlayVisibility() const
{
	TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
	const bool bBatchRunning = Imp.IsValid() && Imp->IsRunning();
	return (bBatchRunning || bUpdating) ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SWatabouImportPanel::OnAbortBatch()
{
	if (TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
	{
		Imp->Abort();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SWatabouImportPanel::BuildFooter()
{
	// Deliberately below the default label size -- the always-on footer carries it. The link reuses
	// NormalText (themed foreground + the hyperlink underline as the click affordance), shrunk to
	// match the attribution text. Stored on the panel so the pointer below outlives Construct.
	FooterLinkTextStyle = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	FooterLinkTextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 8));

	const FSlateFontInfo FooterFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	return SNew(SVerticalBox)

	// Hairline rule so the strip reads as a footer, not text floating under the panel.
	+ SVerticalBox::Slot().AutoHeight()
	[ SNew(SSeparator).Orientation(Orient_Horizontal).Thickness(1.f) ]

	// One centered line: static attribution + a clickable watabou.github.io link.
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(8.f, 3.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FooterFont)
			.ColorAndOpacity(FStyleColors::Foreground)
			.Text(LOCTEXT("FooterAttribution", "Generators (c) Watabou -- Watabou Bridge only brings them into Unreal."))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.f, 0.f, 0.f, 0.f)
		[
			SNew(SHyperlink)
			.Text(LOCTEXT("FooterUrl", "watabou.github.io"))
			.TextStyle(&FooterLinkTextStyle)
			.ToolTipText(LOCTEXT("FooterUrlTip", "Open watabou.github.io in your web browser"))
			.OnNavigate_Lambda([]() { FPlatformProcess::LaunchURL(TEXT("https://watabou.github.io/"), nullptr, nullptr); })
		]
	];
}

#pragma endregion

#pragma region Generator / settings

void SWatabouImportPanel::SetActiveGeneratorById(const FString& GeneratorId, bool bPersist)
{
	TSharedPtr<FWatabouGeneratorInfo> Gen = FWatabouGeneratorRegistry::Get().FindById(GeneratorId);
	if (!Gen.IsValid()) { return; }

	ActiveGenerator = Gen;
	BindActiveGeneratorUI();
	ApplyAndLoad(bPersist);
}

void SWatabouImportPanel::BindActiveGeneratorUI()
{
	if (!ActiveGenerator.IsValid()) { return; }

	// Point the details view at the active generator's settings (a non-owning view over the
	// FInstancedStruct's memory, so edits land in place -- no copy/sync-back).
	if (StructDetailsView.IsValid())
	{
		if (FInstancedStruct* Inst = SettingsByGenerator.Find(FName(*ActiveGenerator->Id)); Inst && Inst->IsValid())
		{
			StructDetailsView->SetStructureData(
				MakeShared<FStructOnScope>(Inst->GetScriptStruct(), Inst->GetMutableMemory()));
		}
		else
		{
			StructDetailsView->SetStructureData(nullptr);
		}
	}

	if (TagPills.IsValid())
	{
		TagPills->SetGroups(ActiveGenerator->GetTagGroups());
	}

	RefreshSeedTextBox();
}

FWatabouGeneratorSettings* SWatabouImportPanel::GetActiveSettings()
{
	if (!ActiveGenerator.IsValid()) { return nullptr; }
	FInstancedStruct* Inst = SettingsByGenerator.Find(FName(*ActiveGenerator->Id));
	return (Inst && Inst->IsValid()) ? &Inst->GetMutable<FWatabouGeneratorSettings>() : nullptr;
}

const FWatabouGeneratorSettings* SWatabouImportPanel::GetActiveSettings() const
{
	if (!ActiveGenerator.IsValid()) { return nullptr; }
	const FInstancedStruct* Inst = SettingsByGenerator.Find(FName(*ActiveGenerator->Id));
	return (Inst && Inst->IsValid()) ? &Inst->Get<FWatabouGeneratorSettings>() : nullptr;
}

FString SWatabouImportPanel::BuildQueryForActiveGenerator() const
{
	const FWatabouGeneratorSettings* S = GetActiveSettings();
	if (!S || !ActiveGenerator.IsValid()) { return FString(); }

	FWatabouSeedRef Ref;
	S->ToSeedRef(ActiveGenerator->Id, Ref);
	return Ref.BuildQueryString();
}

void SWatabouImportPanel::PersistActiveSettings()
{
	if (!ActiveGenerator.IsValid()) { return; }
	UWatabouImportConfig::Get()->SetSavedQuery(ActiveGenerator->Id, BuildQueryForActiveGenerator());
}

void SWatabouImportPanel::ApplyAndLoad(bool bPersist)
{
	if (!WebBrowser.IsValid() || !ActiveGenerator.IsValid()) { return; }

	// Defense-in-depth: make sure the server is up AND has routes for this generator before
	// navigating (recovers from a startup race or a server stopped between sessions).
	FWatabouBridgeEditorModule& Module = FWatabouBridgeEditorModule::Get();
	if (!Module.IsServerRunning())
	{
		UE_LOG(LogWatabou, Warning, TEXT("Load: server not running -- starting it now"));
		Module.Restart();
	}

	// No auto-download here (or anywhere on the load path): the shipped Resources/Bundles
	// snapshots guarantee every generator a pre-patched source, so the resolver always has a
	// floor to serve. Network access happens only through the explicit Update button.
	if (!Module.HasRoutesForGenerator(ActiveGenerator->Id))
	{
		UE_LOG(LogWatabou, Warning, TEXT("Load: no routes for '%s' -- restarting server to re-enumerate"), *ActiveGenerator->Id);
		Module.Restart();
		if (!Module.HasRoutesForGenerator(ActiveGenerator->Id))
		{
			UE_LOG(LogWatabou, Error,
				TEXT("Load: still no routes for '%s' after restart -- click Update to download / repair the bundle."),
				*ActiveGenerator->Id);
			if (OutputField.IsValid())
			{
				OutputField->SetText(FText::Format(
					LOCTEXT("LoadNoRoutesFmt", "(no cached bundle for {0} -- click Update)"),
					FText::FromString(ActiveGenerator->DisplayName)));
			}
			return;
		}
	}

	if (IsImporting()) { SetImportState(EWatabouImportState::Idle); }

	CurrentURL = BuildFullURL(*ActiveGenerator, BuildQueryForActiveGenerator());
	bPendingAutoExport = false;   // visible panel: Load navigates, Import triggers
	bReadbackArmed = false;       // ignore the URL churn from this load until the bundle is ready

	if (bPersist) { PersistActiveSettings(); }

	UE_LOG(LogWatabou, Log, TEXT("Auto-load [%s]: %s"), *ActiveGenerator->DisplayName, *CurrentURL);
	if (OutputField.IsValid())
	{
		OutputField->SetText(FText::Format(LOCTEXT("LoadingFmt", "(loading {0}...)"),
			FText::FromString(ActiveGenerator->DisplayName)));
	}
	WebBrowser->LoadURL(CurrentURL);
}

void SWatabouImportPanel::LoadFromSeedRef(const FWatabouSeedRef& Ref, bool bPersist)
{
	TSharedPtr<FWatabouGeneratorInfo> Gen = FWatabouGeneratorRegistry::Get().FindById(Ref.GeneratorId);
	if (!Gen.IsValid())
	{
		UE_LOG(LogWatabou, Warning, TEXT("LoadFromSeedRef: unknown generator '%s'"), *Ref.GeneratorId);
		if (OutputField.IsValid())
		{
			OutputField->SetText(FText::Format(
				LOCTEXT("SeedRefUnknownGen", "(unknown generator '{0}')"),
				FText::FromString(Ref.GeneratorId)));
		}
		return;
	}

	// Populate the generator's typed settings from the ref. Params the typed struct doesn't model
	// (e.g. name/hexes) are dropped -- the panel is faithful for each generator's modeled settings,
	// and the seed/tags that drive seed-chains are preserved.
	if (FInstancedStruct* Inst = SettingsByGenerator.Find(FName(*Gen->Id)); Inst && Inst->IsValid())
	{
		Inst->GetMutable<FWatabouGeneratorSettings>().LoadFrom(Ref);
	}

	// Switch to that generator's tab: rebinds the settings UI to the just-loaded values and loads.
	SetActiveGeneratorById(Gen->Id, bPersist);
}

void SWatabouImportPanel::PerformInitialLoad()
{
	bInitialLoadDone = true;

	// A seed-ref open request that arrived before realization wins over the default load, so the
	// asset's seed (not the restored last-used settings) drives the first navigation.
	if (PendingOpenSeed.IsSet())
	{
		const FWatabouSeedRef Seed = PendingOpenSeed.GetValue();
		const bool bPersist = bPendingOpenPersist;
		PendingOpenSeed.Reset();
		LoadFromSeedRef(Seed, bPersist);
		return;
	}

	ApplyAndLoad();
}

void SWatabouImportPanel::OpenSeedRef(const FWatabouSeedRef& Ref, bool bPersist)
{
	if (bInitialLoadDone)
	{
		// Tab was already open -- the browser is realized, so apply + navigate now.
		LoadFromSeedRef(Ref, bPersist);
	}
	else
	{
		// Freshly-spawned tab: queue for PerformInitialLoad so we neither race the browser
		// realization (a pre-realization LoadURL is dropped) nor let the default load run first.
		PendingOpenSeed = Ref;
		bPendingOpenPersist = bPersist;
	}
}

void SWatabouImportPanel::RefreshSeedTextBox()
{
	if (!SeedTextBox.IsValid()) { return; }
	const FWatabouGeneratorSettings* S = GetActiveSettings();
	SeedTextBox->SetText(FText::FromString(S ? S->Seed : FString()));
}

void SWatabouImportPanel::RefreshSettingsUI()
{
	RefreshSeedTextBox();
	// Re-bind the struct data so the details view re-reads values changed externally (read-back).
	if (StructDetailsView.IsValid() && ActiveGenerator.IsValid())
	{
		if (FInstancedStruct* Inst = SettingsByGenerator.Find(FName(*ActiveGenerator->Id)); Inst && Inst->IsValid())
		{
			StructDetailsView->SetStructureData(
				MakeShared<FStructOnScope>(Inst->GetScriptStruct(), Inst->GetMutableMemory()));
		}
	}
	// Tag pills reflect selection via their IsChecked attribute -- no explicit refresh needed.
}

void SWatabouImportPanel::OnSeedTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	FWatabouGeneratorSettings* S = GetActiveSettings();
	if (!S) { return; }

	S->Seed = Text.ToString();
	PersistActiveSettings();
	// Reload only on an explicit Enter; focus-loss just persists (avoids surprise reloads).
	if (CommitType == ETextCommit::OnEnter) { ApplyAndLoad(); }
}

FReply SWatabouImportPanel::OnRerollSeed()
{
	FWatabouGeneratorSettings* S = GetActiveSettings();
	if (!S) { return FReply::Handled(); }

	S->RandomizeSeed();
	RefreshSeedTextBox();
	ApplyAndLoad();   // persists internally
	return FReply::Handled();
}

void SWatabouImportPanel::OnDetailsChanged(const FPropertyChangedEvent& /*Event*/)
{
	// A typed param was edited; the details view wrote it into the active settings in place.
	ApplyAndLoad();   // persists internally
}

bool SWatabouImportPanel::IsTagSelected(const FString& InTag) const
{
	const FWatabouGeneratorSettings* S = GetActiveSettings();
	return S && S->SelectedTags.Contains(InTag);
}

void SWatabouImportPanel::ToggleTag(const FWatabouTagGroup& Group, const FString& InTag)
{
	FWatabouGeneratorSettings* S = GetActiveSettings();
	if (!S) { return; }

	const bool bWasSelected = S->SelectedTags.Contains(InTag);
	if (Group.bMutuallyExclusive)
	{
		// Radio-style: clear the whole group first, then (re)add below if it wasn't the one on.
		for (const FString& T : Group.Tags) { S->SelectedTags.Remove(T); }
	}
	if (bWasSelected)
	{
		S->SelectedTags.Remove(InTag);
	}
	else
	{
		S->SelectedTags.AddUnique(InTag);
	}

	ApplyAndLoad();   // persists internally
}

#pragma endregion

#pragma region Browser read-back

void SWatabouImportPanel::HandleUrlChanged(const FText& NewUrlText)
{
	// Ignore the URL churn produced by our own LoadURL: read-back is armed only once the bundle
	// reports ready, after which a URL change means the user tweaked something in the browser.
	if (!bReadbackArmed || !ActiveGenerator.IsValid()) { return; }

	const FString NewUrl = NewUrlText.ToString();
	int32 QIdx;
	if (!NewUrl.FindChar(TEXT('?'), QIdx)) { return; }   // no query -> nothing to read back

	const FString Query = NewUrl.RightChop(QIdx);   // includes leading '?'
	const FWatabouSeedRef Incoming = WatabouUrlUtils::ParseQueryString(ActiveGenerator->Id, Query);

	FWatabouGeneratorSettings* S = GetActiveSettings();
	if (!S) { return; }

	// Skip if the browser state already matches our settings (avoids churn on canonicalization).
	FWatabouSeedRef Current;
	S->ToSeedRef(ActiveGenerator->Id, Current);
	if (Current == Incoming) { return; }

	S->LoadFrom(Incoming);
	RefreshSettingsUI();
	PersistActiveSettings();
	// No reload: the browser already shows this state.
}

#pragma endregion

#pragma region Buttons

FReply SWatabouImportPanel::OnImportClicked()
{
	if (!WebBrowser.IsValid())
	{
		UE_LOG(LogWatabou, Warning, TEXT("Import clicked before browser realized"));
		return FReply::Handled();
	}
	if (CurrentURL.IsEmpty())
	{
		UE_LOG(LogWatabou, Warning, TEXT("Import clicked but nothing is loaded yet"));
		if (OutputField.IsValid())
		{
			OutputField->SetText(LOCTEXT("OutputNothingLoaded", "(nothing loaded yet)"));
		}
		return FReply::Handled();
	}
	// Recovery: if the previous click's export never came back (e.g. bundle regenerated and broke
	// our saveAs hook before we re-injected), past the threshold a fresh click retries.
	if (ShouldIgnoreManualClickAsStuck(TEXT("Import"))) { return FReply::Handled(); }

	SetImportState(EWatabouImportState::Loading);

	WebBrowser->ExecuteJavascript(TEXT(R"JS(
		if (typeof window.__watabou_trigger_export === 'function') {
			window.__watabou_trigger_export();
		} else {
			console.log('__UE_BRIDGE__|error|' + btoa('Import: shim not installed (Load first)'));
		}
	)JS"));
	return FReply::Handled();
}

TSharedRef<SWidget> SWatabouImportPanel::BuildLoadFromLinkMenu()
{
	// Pre-fill from the clipboard when it already holds a Watabou URL -- the common case is the
	// user copied a share link / seed-chain URL before clicking.
	FString Clip;
	FPlatformApplicationMisc::ClipboardPaste(Clip);
	FWatabouSeedRef Probe;
	const FString Prefill = WatabouUrlUtils::ParseWatabouUrl(Clip, Probe) ? Clip : FString();

	return SNew(SBox).WidthOverride(380.f).Padding(8.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[ SNew(STextBlock).Text(LOCTEXT("LinkPrompt", "Paste a Watabou URL")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SAssignNew(LinkTextBox, SEditableTextBox)
			.Text(FText::FromString(Prefill))
			.HintText(LOCTEXT("LinkHint", "https://watabou.github.io/<generator>/?seed=..."))
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnEnter)
				{
					HandleLoadFromLink(Text.ToString());
					if (LinkComboButton.IsValid()) { LinkComboButton->SetIsOpen(false); }
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(SButton)
			.Text(LOCTEXT("LinkLoadBtn", "Load"))
			.OnClicked_Lambda([this]() -> FReply
			{
				if (LinkTextBox.IsValid()) { HandleLoadFromLink(LinkTextBox->GetText().ToString()); }
				if (LinkComboButton.IsValid()) { LinkComboButton->SetIsOpen(false); }
				return FReply::Handled();
			})
		]
	];
}

void SWatabouImportPanel::HandleLoadFromLink(const FString& Url)
{
	FWatabouSeedRef Ref;
	if (!WatabouUrlUtils::ParseWatabouUrl(Url, Ref))
	{
		UE_LOG(LogWatabou, Warning, TEXT("Load from link: not a recognizable Watabou URL: %s"), *Url);
		if (OutputField.IsValid())
		{
			OutputField->SetText(LOCTEXT("LinkBadUrl", "(not a recognizable watabou.github.io URL)"));
		}
		return;
	}

	UE_LOG(LogWatabou, Log, TEXT("Load from link: '%s' seed='%s' -> switching tab + loading"),
		*Ref.GeneratorId, *Ref.Seed);

	// The user is acting on an already-loaded panel (browser realized), so apply synchronously and
	// persist the pasted link as this generator's last-used settings -- same as before.
	LoadFromSeedRef(Ref, /*bPersist=*/true);
}

FReply SWatabouImportPanel::OnCopyOutputClicked()
{
	if (!LastImportedJson.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*LastImportedJson);
	}
	else if (OutputField.IsValid())
	{
		FPlatformApplicationMisc::ClipboardCopy(*OutputField->GetText().ToString());
	}
	return FReply::Handled();
}

FReply SWatabouImportPanel::OnOpenInBrowserClicked()
{
	if (!CurrentURL.IsEmpty())
	{
		FPlatformProcess::LaunchURL(*CurrentURL, nullptr, nullptr);
	}
	return FReply::Handled();
}

FReply SWatabouImportPanel::OnBrowseTargetDir()
{
	UWatabouImportConfig* Config = UWatabouImportConfig::Get();

	const TSharedRef<SDlgPickPath> Dlg = SNew(SDlgPickPath)
		.Title(LOCTEXT("PickImportDirTitle", "Choose Watabou import folder"))
		.DefaultPath(FText::FromString(Config->GetImportTargetDir()));

	if (Dlg->ShowModal() == EAppReturnType::Ok)
	{
		Config->SetImportTargetDir(Dlg->GetPath().ToString());
		if (TargetDirTextBox.IsValid())
		{
			TargetDirTextBox->SetText(FText::FromString(Config->GetImportTargetDir()));
		}
	}
	return FReply::Handled();
}

FReply SWatabouImportPanel::OnUpdateGeneratorsClicked()
{
	if (ActiveUpdater.IsValid() && ActiveUpdater->IsRunning())
	{
		UE_LOG(LogWatabou, Warning, TEXT("Update already running"));
		return FReply::Handled();
	}
	StartGeneratorsUpdate();
	return FReply::Handled();
}

bool SWatabouImportPanel::StartGeneratorsUpdate()
{
	if (ActiveUpdater.IsValid() && ActiveUpdater->IsRunning())
	{
		return false;   // an update is already in flight
	}

	FWatabouResourceUpdater::FUpdateBatch Batch = FWatabouResourceUpdater::BuildBatchForAllGenerators();

	if (Batch.Jobs.Num() == 0)
	{
		UE_LOG(LogWatabou, Warning, TEXT("No resource manifests found; nothing to update."));
		if (OutputField.IsValid())
		{
			OutputField->SetText(LOCTEXT("UpdateNoJobs", "(no generator manifests found)"));
		}
		return false;
	}

	bUpdating = true;

	if (OutputField.IsValid())
	{
		OutputField->SetText(FText::Format(LOCTEXT("UpdateStarting", "(updating: 0 / {0} files...)"),
			FText::AsNumber(Batch.Jobs.Num())));
	}
	UE_LOG(LogWatabou, Log, TEXT("Updater: starting %d fetch jobs across %d generators"),
		Batch.Jobs.Num(), Batch.Generators.Num());

	ActiveUpdater = MakeShared<FWatabouResourceUpdater>();
	ActiveUpdater->Run(MoveTemp(Batch),
		FOnWatabouUpdaterProgress::CreateSP(this, &SWatabouImportPanel::OnUpdaterProgress),
		FOnWatabouUpdaterFinished::CreateSP(this, &SWatabouImportPanel::OnUpdaterFinished));
	return true;
}

void SWatabouImportPanel::OnUpdaterProgress(int32 Completed, int32 Total)
{
	if (!OutputField.IsValid()) { return; }
	if (Completed == Total || (Completed % 10) == 0)
	{
		OutputField->SetText(FText::Format(LOCTEXT("UpdateProgressFmt", "(updating: {0} / {1} files...)"),
			FText::AsNumber(Completed), FText::AsNumber(Total)));
	}
}

void SWatabouImportPanel::OnUpdaterFinished(int32 Succeeded, int32 Failed, int32 Total)
{
	UE_LOG(LogWatabou, Log, TEXT("Updater finished: %d / %d succeeded (%d failed). Auto-restarting server..."),
		Succeeded, Total, Failed);

	FWatabouBridgeEditorModule::Get().Restart();

	if (OutputField.IsValid())
	{
		OutputField->SetText(FText::Format(
			LOCTEXT("UpdateDoneFmt", "(updated {0} / {1} files, {2} failed -- server restarted)"),
			FText::AsNumber(Succeeded), FText::AsNumber(Total), FText::AsNumber(Failed)));
	}

	bUpdating = false;
	ActiveUpdater.Reset();
}

#pragma endregion

#pragma region Bridge / browser handlers

void SWatabouImportPanel::HandleLoadCompleted()
{
	if (!WebBrowser.IsValid() || !Bridge) { return; }
	if (!ActiveGenerator.IsValid()) { return; }

	// Loading just means injecting the shim. It reports back over whichever bridge transport is
	// live: window.ue.watabou (bound in Construct) wherever that backend works -- the only channel
	// on macOS's WKWebView -- or the console-log sentinel (HandleConsoleMessage) as the CEF
	// fallback. See WatabouShim.cpp `deliver`.
	WebBrowser->ExecuteJavascript(WatabouShim::BuildInjectionScript(*ActiveGenerator, bPendingAutoExport));
}

void SWatabouImportPanel::HandleConsoleMessage(const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity)
{
	FString Event;
	FWatabouExportPayload Payload;
	FString ErrorMessage;
	if (WatabouShim::TryParseBridgeMessage(Message, Event, Payload, ErrorMessage))
	{
		// Transport diagnostic (Verbose): the window.ue path logs its own line in OnBridgeMessage.
		UE_LOG(LogWatabou, Verbose, TEXT("Bridge transport: console.log sentinel (event='%s')"), *Event);
		if (Event == TEXT("export"))      { Bridge->OnExport(Payload); return; }
		if (Event == TEXT("ready"))       { Bridge->OnReady(); return; }
		if (Event == TEXT("error"))       { Bridge->OnError(ErrorMessage); return; }
		UE_LOG(LogWatabou, Warning, TEXT("Unhandled bridge event: %s"), *Event);
		return;
	}

	if (!WatabouShim::IsCefLogMuted())
	{
		UE_LOG(LogWatabou, Log, TEXT("[CEF %s:%d] %s"), *Source, Line, *Message);
	}
}

void SWatabouImportPanel::HandleExportReceived(const FWatabouExportPayload& Payload)
{
	LastImportedJson = Payload.Content;
	UE_LOG(LogWatabou, Log, TEXT("Visible-browser export %s: %d chars (state='%s', thumb=%d bytes)"),
		*Payload.FileName, Payload.Content.Len(), *Payload.StateUrl, Payload.ThumbnailDataUrl.Len());

	SetImportState(EWatabouImportState::Generating);

	if (!ActiveGenerator.IsValid())
	{
		SetImportState(EWatabouImportState::Failed, TEXT("no generator selected"));
		return;
	}
	TSharedPtr<FWatabouGeneratorInfo> Gen = ActiveGenerator;

	const bool bAutoSave = WatabouShim::IsAutoSaveOnImport();
	if (bAutoSave)
	{
		SetImportState(EWatabouImportState::Saving, FString::Printf(TEXT("WTB_%s"), *Gen->Id));
	}

	WatabouAssetBuilder::FBuildArgs Args;
	Args.Gen             = Gen;
	Args.Payload         = Payload;
	Args.ParentAssetPath = FString();   // visible-browser manual imports are always top-level
	Args.TopLevelBaseDir = UWatabouImportConfig::Get()->GetImportTargetDir();
	Args.bAutoSave       = bAutoSave;

	TWeakPtr<SWatabouImportPanel> WeakSelf = SharedThis(this);
	WatabouAssetBuilder::BuildAssetFromExport(Args,
		FOnWatabouAssetBuilt::CreateLambda(
			[WeakSelf](UWatabouAssetBase* Asset, const FString& Error)
			{
				TSharedPtr<SWatabouImportPanel> Self = WeakSelf.Pin();
				if (!Self.IsValid()) { return; }

				if (!Error.IsEmpty() || !Asset)
				{
					Self->SetImportState(EWatabouImportState::Failed,
						Error.IsEmpty() ? TEXT("asset build failed") : Error);
					return;
				}

				const FString PackagePath = Asset->GetPackage()->GetName();
				Self->SetImportState(EWatabouImportState::Idle,
					FString::Printf(TEXT("(imported %s; click Copy for raw JSON)"), *PackagePath));

				if (Self->bRecursiveOnImport)
				{
					if (TSharedPtr<FWatabouRecursiveImporter> Imp =
						FWatabouBridgeEditorModule::Get().GetRecursiveImporter())
					{
						Imp->EnqueueFromAsset(Asset);
					}
				}
			}));
}

void SWatabouImportPanel::HandleBridgeReady(const FString& /*Message*/)
{
	// Bundle finished generating: arm read-back so subsequent URL changes (user tweaks in the
	// embedded browser) flow back into the active settings.
	bReadbackArmed = true;
}

void SWatabouImportPanel::HandleBridgeError(const FString& Message)
{
	if (IsImporting())
	{
		SetImportState(EWatabouImportState::Failed, Message);
	}
	else if (OutputField.IsValid())
	{
		OutputField->SetText(FText::Format(LOCTEXT("ErrorFmt", "[error] {0}"), FText::FromString(Message)));
	}
}

FString SWatabouImportPanel::BuildFullURL(const FWatabouGeneratorInfo& Gen, const FString& Query) const
{
	if (Query.StartsWith(TEXT("http://")) || Query.StartsWith(TEXT("https://"))) { return Query; }

	const FWatabouBridgeEditorModule& Module = FWatabouBridgeEditorModule::Get();
	if (!Module.IsServerRunning())
	{
		UE_LOG(LogWatabou, Warning, TEXT("Local server is not running -- falling back to upstream."));
		const FString QuerySafe = Query.StartsWith(TEXT("?")) || Query.IsEmpty() ? Query : (TEXT("?") + Query);
		return FString::Printf(TEXT("https://watabou.github.io/%s/%s"), *Gen.Id, *QuerySafe);
	}

	FString QueryNormalized = Query;
	if (!QueryNormalized.IsEmpty() && !QueryNormalized.StartsWith(TEXT("?")) && !QueryNormalized.StartsWith(TEXT("/")))
	{
		QueryNormalized = TEXT("?") + QueryNormalized;
	}

	return FString::Printf(TEXT("http://127.0.0.1:%u/%s/%s"),
		Module.GetServerPort(), *Gen.Id, *QueryNormalized);
}

#pragma endregion

#pragma region Status

void SWatabouImportPanel::HandlePoolChanged()
{
	// Re-fire current state so status text reflects the pool transition (queue depth, busy/idle).
	SetImportState(ImportState);
}

void SWatabouImportPanel::HandleRecursiveProgress(const FWatabouRecursiveImporter::FProgress& Progress)
{
	if (!OutputField.IsValid()) { return; }

	if (Progress.State == FWatabouRecursiveImporter::EBatchState::Done)
	{
		OutputField->SetText(FText::FromString(FString::Printf(
			TEXT("(batch finished -- %d imported, %d failed; check Content Browser)"),
			Progress.Done, Progress.Failed)));

		// Defensive: if the visible-browser ImportState got stuck (e.g. user clicked Import during
		// the batch and the trigger silently failed), the batch finishing is a natural "settled"
		// signal -- restore a clickable state without the 5s manual-retry wait.
		if (IsImporting())
		{
			UE_LOG(LogWatabou, Warning,
				TEXT("Panel was stuck in non-Idle state at batch finish -- resetting to Idle"));
			SetImportState(EWatabouImportState::Idle, TEXT("(batch done; ready)"));
		}
	}
	else
	{
		SetImportState(ImportState);
	}
}

namespace SWatabouImportPanel_Internal
{
	static const TCHAR* ImportStateName(EWatabouImportState S)
	{
		switch (S)
		{
		case EWatabouImportState::Idle:       return TEXT("Idle");
		case EWatabouImportState::Loading:    return TEXT("Loading");
		case EWatabouImportState::Generating: return TEXT("Generating");
		case EWatabouImportState::Saving:     return TEXT("Saving");
		case EWatabouImportState::Failed:     return TEXT("Failed");
		}
		return TEXT("?");
	}
}

void SWatabouImportPanel::SetImportState(EWatabouImportState NewState, const FString& Detail)
{
	if (ImportState != NewState)
	{
		UE_LOG(LogWatabou, Verbose, TEXT("Panel ImportState %s -> %s  detail='%s'"),
			SWatabouImportPanel_Internal::ImportStateName(ImportState),
			SWatabouImportPanel_Internal::ImportStateName(NewState),
			*Detail);
		LastImportStateChangeSeconds = FPlatformTime::Seconds();
	}
	ImportState = NewState;

	if (!OutputField.IsValid()) { return; }

	FString BaseText;
	switch (NewState)
	{
	case EWatabouImportState::Idle:       BaseText = Detail; break;
	case EWatabouImportState::Loading:    BaseText = TEXT("(loading -- generating JSON in browser...)"); break;
	case EWatabouImportState::Generating: BaseText = TEXT("(generating -- parsing and encoding...)"); break;
	case EWatabouImportState::Saving:     BaseText = TEXT("(saving uasset -- editor will be briefly unresponsive)"); break;
	case EWatabouImportState::Failed:     BaseText = FString::Printf(TEXT("(failed: %s)"), Detail.IsEmpty() ? TEXT("unknown reason") : *Detail); break;
	}

	TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
	if (Imp.IsValid() && Imp->IsRunning())
	{
		const FWatabouRecursiveImporter::FProgress& P = Imp->GetProgress();
		OutputField->SetText(FText::FromString(
			FString::Printf(TEXT("[Pool %d/%d done, %d failed, %d in flight] %s"),
				P.Done, P.Total, P.Failed, P.InFlight, *BaseText)));
	}
	else
	{
		OutputField->SetText(FText::FromString(BaseText));
	}
}

bool SWatabouImportPanel::ShouldIgnoreManualClickAsStuck(const TCHAR* ButtonName) const
{
	if (!IsImporting()) { return false; }
	constexpr double StuckThresholdSeconds = 5.0;
	const double Elapsed = FPlatformTime::Seconds() - LastImportStateChangeSeconds;
	if (Elapsed < StuckThresholdSeconds)
	{
		UE_LOG(LogWatabou, Warning,
			TEXT("%s clicked while a manual import is in progress (%.1fs elapsed) -- ignoring; click again past %.0fs to force"),
			ButtonName, Elapsed, StuckThresholdSeconds);
		return true;
	}
	UE_LOG(LogWatabou, Warning,
		TEXT("%s: panel stuck in non-Idle state for %.1fs -- forcing through"),
		ButtonName, Elapsed);
	return false;
}

bool SWatabouImportPanel::GetInteractiveEnabled() const
{
	if (bUpdating) { return false; }   // a generator update is in flight
	// A recursive batch runs headless behind the blocking overlay. The scrim occludes the pointer
	// but not keyboard focus, so the action controls must be hard-disabled too, otherwise Tab+Enter
	// could fire a foreground Import / tab-switch into the batch through the shared browser pool.
	TSharedPtr<FWatabouRecursiveImporter> Imp = FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
	return !(Imp.IsValid() && Imp->IsRunning());
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
