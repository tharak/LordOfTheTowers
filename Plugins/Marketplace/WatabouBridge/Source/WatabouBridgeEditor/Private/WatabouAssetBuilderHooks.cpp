// Copyright 2026 Timothé Lapetite

#include "WatabouAssetBuilderHooks.h"
#include "WatabouAssetBase.h"
#include "WatabouAssetBuilder.h"
#include "WatabouBridge.h"

#include "Misc/Base64.h"
#include "Misc/PackageName.h"

#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ObjectTools.h"   // ThumbnailTools::CacheThumbnail

namespace WatabouAssetBuilderHooks
{
	static void ApplyThumbnail_Impl(UWatabouAssetBase* Asset, const FString& DataUrl)
	{
		if (!Asset || DataUrl.IsEmpty()) { return; }

		FString Base64 = DataUrl;
		int32 CommaIdx;
		if (DataUrl.StartsWith(TEXT("data:")) && DataUrl.FindChar(TEXT(','), CommaIdx))
		{
			Base64 = DataUrl.RightChop(CommaIdx + 1);
		}

		TArray<uint8> PngBytes;
		if (!FBase64::Decode(Base64, PngBytes) || PngBytes.Num() == 0)
		{
			UE_LOG(LogWatabou, Warning, TEXT("Thumbnail: base64 decode produced no bytes"));
			return;
		}

		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(PngBytes.GetData(), PngBytes.Num()))
		{
			UE_LOG(LogWatabou, Warning, TEXT("Thumbnail: PNG header parse failed"));
			return;
		}

		TArray<uint8> Bgra;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Bgra))
		{
			UE_LOG(LogWatabou, Warning, TEXT("Thumbnail: PNG decode to BGRA failed"));
			return;
		}

		const int32 W = Wrapper->GetWidth();
		const int32 H = Wrapper->GetHeight();
		if (W <= 0 || H <= 0 || Bgra.Num() != W * H * 4)
		{
			UE_LOG(LogWatabou, Warning, TEXT("Thumbnail: decoded image is malformed (%dx%d, %d bytes)"),
				W, H, Bgra.Num());
			return;
		}

		FObjectThumbnail Thumb;
		Thumb.SetImageSize(W, H);
		Thumb.AccessImageData() = MoveTemp(Bgra);
		ThumbnailTools::CacheThumbnail(Asset->GetFullName(), &Thumb, Asset->GetPackage());

		UE_LOG(LogWatabou, Log, TEXT("Thumbnail: applied %dx%d to %s"),
			W, H, *Asset->GetPathName());
	}

	static void RegisterCreatedAsset_Impl(UWatabouAssetBase* Asset)
	{
		if (!Asset) { return; }
		Asset->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);
	}

	bool SavePackageNow(UWatabouAssetBase* Asset)
	{
		if (!Asset) { return false; }
		UPackage* Package = Asset->GetPackage();
		if (!Package) { return false; }

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags     = SAVE_None;
		FSavePackageResultStruct Result;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WatabouAssetBuilderHooks::SavePackage);
			Result = UPackage::Save(Package, Asset, *FileName, SaveArgs);
		}

		if (!Result.IsSuccessful())
		{
			UE_LOG(LogWatabou, Error, TEXT("SavePackage failed for %s -> %s"), *Package->GetName(), *FileName);
			return false;
		}
		return true;
	}

	static void DeferredSavePackage_Impl(
		UPackage* Package,
		UWatabouAssetBase* Asset,
		const FString& PackagePath,
		const FString& /*PackageFileName*/,   // save path is derived inside SavePackageNow now
		FOnWatabouAssetBuilt OnDone)
	{
		if (!Package || !Asset || !GEditor)
		{
			OnDone.ExecuteIfBound(nullptr,
				FString::Printf(TEXT("DeferredSavePackage: null inputs (pkg=%p asset=%p editor=%p)"),
					Package, Asset, GEditor));
			return;
		}

		// A strong ref to the asset keeps it (and, as its outer, the package) alive across the one-frame
		// gap. The save itself goes through the shared SavePackageNow so policy lives in one place.
		TStrongObjectPtr<UWatabouAssetBase> StrongAsset(Asset);
		const FString AssetClassName = Asset->GetClass()->GetName();

		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[StrongAsset, PackagePath, AssetClassName, OnDone]()
			{
				if (SavePackageNow(StrongAsset.Get()))
				{
					UE_LOG(LogWatabou, Log, TEXT("Saved %s  (class=%s)"), *PackagePath, *AssetClassName);
					OnDone.ExecuteIfBound(StrongAsset.Get(), FString());
				}
				else
				{
					OnDone.ExecuteIfBound(nullptr,
						FString::Printf(TEXT("SavePackage failed (%s)"), *PackagePath));
				}
			}));
	}

	void Register()
	{
		WatabouAssetBuilder::FHostHooks Hooks;
		Hooks.ApplyThumbnail        = &ApplyThumbnail_Impl;
		Hooks.RegisterCreatedAsset  = &RegisterCreatedAsset_Impl;
		Hooks.DeferredSavePackage   = &DeferredSavePackage_Impl;
		WatabouAssetBuilder::SetHostHooks(MoveTemp(Hooks));
	}

	void Unregister()
	{
		WatabouAssetBuilder::SetHostHooks(WatabouAssetBuilder::FHostHooks{});
	}
}
