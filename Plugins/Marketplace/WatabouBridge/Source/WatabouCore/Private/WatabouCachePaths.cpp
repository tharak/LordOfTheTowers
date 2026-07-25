// Copyright 2026 Timothé Lapetite

#include "WatabouCachePaths.h"
#include "WatabouBridge.h"

#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Interfaces/IPluginManager.h"

namespace WatabouCachePaths
{
	FString GetCacheRoot()
	{
		// Cache lives under <Plugin>/Content/Bundles/ so it ships with packaged builds
		// via the RuntimeDependencies entry in WatabouCore.Build.cs. This makes JIT
		// generation possible at runtime on Win64/Mac (CEF available).
		//
		// The Content/ subtree is a UE convention for "files that ship with the plugin";
		// raw (non-.uasset) files are staged as-is when marked NonUFS in Build.cs.
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WatabouBridge"));
		if (!Plugin.IsValid())
		{
			UE_LOG(LogWatabou, Error, TEXT("WatabouCachePaths::GetCacheRoot: WatabouBridge plugin not found"));
			return FString();
		}
		FString Root = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content"), TEXT("Bundles"));
		FPaths::NormalizeDirectoryName(Root);
		return Root;
	}

	FString GetGeneratorCacheRoot(const FString& GeneratorId)
	{
		return FPaths::Combine(GetCacheRoot(), GeneratorId);
	}

	FString GetVersionCacheDir(const FString& GeneratorId, const FString& Version)
	{
		return FPaths::Combine(GetGeneratorCacheRoot(GeneratorId), Version);
	}

	FString GetStagingDir(const FString& GeneratorId, const FString& StagingToken)
	{
		return FPaths::Combine(GetGeneratorCacheRoot(GeneratorId), FString::Printf(TEXT(".staging-%s"), *StagingToken));
	}

	FString GetActiveVersionFilePath(const FString& GeneratorId)
	{
		return FPaths::Combine(GetGeneratorCacheRoot(GeneratorId), TEXT("_active.txt"));
	}

	FString ReadActiveVersion(const FString& GeneratorId)
	{
		const FString Path = GetActiveVersionFilePath(GeneratorId);
		FString Out;
		if (FFileHelper::LoadFileToString(Out, *Path))
		{
			Out.TrimStartAndEndInline();
			return Out;
		}
		return FString();
	}

	bool WriteActiveVersion(const FString& GeneratorId, const FString& Version)
	{
		const FString Path = GetActiveVersionFilePath(GeneratorId);
		IFileManager& FM = IFileManager::Get();
		FM.MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		const bool bOk = FFileHelper::SaveStringToFile(Version, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bOk)
		{
			UE_LOG(LogWatabou, Warning, TEXT("Failed to write active version for %s -> %s"), *GeneratorId, *Version);
		}
		return bOk;
	}

	TArray<FString> EnumerateVersions(const FString& GeneratorId)
	{
		TArray<FString> Out;
		const FString GenRoot = GetGeneratorCacheRoot(GeneratorId);
		IFileManager& FM = IFileManager::Get();
		if (!FM.DirectoryExists(*GenRoot)) { return Out; }

		TArray<FString> Subdirs;
		FM.FindFiles(Subdirs, *(GenRoot / TEXT("*")), /*Files=*/false, /*Directories=*/true);
		for (const FString& Sub : Subdirs)
		{
			// Skip staging / hidden directories.
			if (Sub.StartsWith(TEXT(".")) || Sub.StartsWith(TEXT("_"))) { continue; }
			Out.Add(Sub);
		}
		Out.Sort();
		return Out;
	}

	FString GetVerifiedMarkerPath(const FString& GeneratorId, const FString& Version)
	{
		return FPaths::Combine(GetVersionCacheDir(GeneratorId, Version), TEXT(".ok"));
	}

	bool IsVersionVerified(const FString& GeneratorId, const FString& Version)
	{
		if (Version.IsEmpty()) { return false; }
		return IFileManager::Get().FileExists(*GetVerifiedMarkerPath(GeneratorId, Version));
	}

	bool MarkVersionVerified(const FString& GeneratorId, const FString& Version)
	{
		const FString Path = GetVerifiedMarkerPath(GeneratorId, Version);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		// Content = ISO timestamp, so the marker doubles as a human-readable "verified at" record;
		// the mtime is what FindNewestVerifiedVersion sorts on.
		const bool bOk = FFileHelper::SaveStringToFile(
			FDateTime::UtcNow().ToIso8601(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bOk)
		{
			UE_LOG(LogWatabou, Warning, TEXT("Failed to write verified marker for %s -> %s"), *GeneratorId, *Version);
		}
		return bOk;
	}

	FString FindNewestVerifiedVersion(const FString& GeneratorId, const FString& ExcludeVersion)
	{
		FString BestVersion;
		FDateTime BestStamp = FDateTime::MinValue();

		IFileManager& FM = IFileManager::Get();
		for (const FString& Version : EnumerateVersions(GeneratorId))
		{
			if (Version == ExcludeVersion) { continue; }

			const FString Marker = GetVerifiedMarkerPath(GeneratorId, Version);
			if (!FM.FileExists(*Marker)) { continue; }

			// "Newest" = most recently verified, by marker mtime -- robust to non-lexical version
			// strings (e.g. 1.9.0 vs 1.10.0) since it tracks when each passed the smoke test.
			const FDateTime Stamp = FM.GetTimeStamp(*Marker);
			if (BestVersion.IsEmpty() || Stamp > BestStamp)
			{
				BestVersion = Version;
				BestStamp = Stamp;
			}
		}
		return BestVersion;
	}
}
