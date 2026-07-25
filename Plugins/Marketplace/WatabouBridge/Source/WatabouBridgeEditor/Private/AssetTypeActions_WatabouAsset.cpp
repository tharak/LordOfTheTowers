// Copyright 2026 Timothé Lapetite

#include "AssetTypeActions_WatabouAsset.h"

#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouBridgeEditorModule.h"
#include "WatabouRecursiveImporter.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions_WatabouAsset"

FText FAssetTypeActions_WatabouAsset::GetName() const
{
	return LOCTEXT("Name", "Watabou Asset");
}

UClass* FAssetTypeActions_WatabouAsset::GetSupportedClass() const
{
	return UWatabouAssetBase::StaticClass();
}

void FAssetTypeActions_WatabouAsset::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	// TODO: multi-select queueing. v1 only starts from the first selected asset.
	TWeakObjectPtr<UWatabouAssetBase> WeakRoot;
	for (UObject* Obj : InObjects)
	{
		if (UWatabouAssetBase* Asset = Cast<UWatabouAssetBase>(Obj))
		{
			WeakRoot = Asset;
			break;
		}
	}
	if (!WeakRoot.IsValid()) { return; }

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ResolveLinks", "Resolve Linked Content (Recursive)"),
		LOCTEXT("ResolveLinksTip",
			"Walk this asset for FWatabouSeedRefs and import each one via the module's "
			"headless browser pool. Runs in the background regardless of the import panel "
			"being open. Uses the Follow / Force / On-failure settings from the panel."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
		FUIAction(FExecuteAction::CreateLambda([WeakRoot]()
		{
			UWatabouAssetBase* Root = WeakRoot.Get();
			if (!Root) { return; }

			TSharedPtr<FWatabouRecursiveImporter> Importer =
				FWatabouBridgeEditorModule::Get().GetRecursiveImporter();
			if (!Importer.IsValid())
			{
				UE_LOG(LogWatabou, Warning, TEXT("Resolve Linked Content: recursive importer not initialized"));
				return;
			}
			if (Importer->IsRunning())
			{
				UE_LOG(LogWatabou, Warning, TEXT("Resolve Linked Content: a batch is already running -- ignoring"));
				return;
			}

			Importer->StartFromAsset(Root);
		})));
}

#undef LOCTEXT_NAMESPACE
