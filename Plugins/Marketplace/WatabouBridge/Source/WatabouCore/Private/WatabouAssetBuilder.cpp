// Copyright 2026 Timothé Lapetite

#include "WatabouAssetBuilder.h"

#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouCachePaths.h"
#include "WatabouGeneratorInfo.h"
#include "IWatabouParser.h"
#include "WatabouUrlUtils.h"
#include "WatabouSeedRef.h"
#include "WatabouSeedCache.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"   // INVALID_OBJECTNAME_CHARACTERS
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace WatabouAssetBuilder
{
	// Module-lifetime hook table. Default-constructed empty -- editor registers
	// real callbacks at StartupModule. Runtime leaves them empty.
	static FHostHooks GHooks;

	void SetHostHooks(FHostHooks Hooks)
	{
		GHooks = MoveTemp(Hooks);
	}

	const FHostHooks& GetHostHooks()
	{
		return GHooks;
	}

	/** Replacement for editor-only ObjectTools::SanitizeObjectName. Walks the input
	 *  replacing every character listed in INVALID_OBJECTNAME_CHARACTERS with '_'.
	 *  INVALID_OBJECTNAME_CHARACTERS is defined in Core (CoreUObject), so this is
	 *  runtime-safe. */
	static FString SanitizeForAssetName(const FString& In)
	{
		FString Result = In;
		const TCHAR* Invalid = INVALID_OBJECTNAME_CHARACTERS;
		for (const TCHAR* Ch = Invalid; *Ch; ++Ch)
		{
			Result.ReplaceCharInline(*Ch, TEXT('_'), ESearchCase::CaseSensitive);
		}
		return Result;
	}

	/** Bound a name stem: when longer than 2*KeepEachEnd, collapse the middle and keep the first and
	 *  last KeepEachEnd chars (joined by '_'). Both ends stay recognizable. Cosmetic only -- if two
	 *  long titles collapse to the same stem, the caller's hash disambiguator still keeps paths unique. */
	static FString TruncateMiddle(const FString& In, int32 KeepEachEnd)
	{
		if (KeepEachEnd <= 0 || In.Len() <= KeepEachEnd * 2) { return In; }
		return In.Left(KeepEachEnd) + TEXT("_") + In.Right(KeepEachEnd);
	}

	/** ASCII-only slug for an asset-name stem: keep [A-Za-z0-9] runs, collapse every gap (spaces,
	 *  punctuation, and multibyte / non-ASCII chars) to a single '_', with no leading/trailing '_'.
	 *  Keeps package filenames portable across case/encoding-sensitive filesystems; the canonical hash
	 *  remains the uniqueness guarantee. The result is single-unit ASCII, so a later TruncateMiddle
	 *  can't split a UTF-16 surrogate pair. */
	static FString MakeAsciiSlug(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		bool bPendingSep = false;
		for (const TCHAR C : In)
		{
			const bool bKeep = (C >= TEXT('0') && C <= TEXT('9'))
				|| (C >= TEXT('A') && C <= TEXT('Z'))
				|| (C >= TEXT('a') && C <= TEXT('z'));
			if (bKeep)
			{
				if (bPendingSep && !Out.IsEmpty()) { Out.AppendChar(TEXT('_')); }
				Out.AppendChar(C);
				bPendingSep = false;
			}
			else
			{
				bPendingSep = true;
			}
		}
		return Out;
	}

	void BuildAssetFromExport(const FBuildArgs& Args, FOnWatabouAssetBuilt OnDone)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WatabouAssetBuilder::BuildAssetFromExport);

		const TSharedPtr<FWatabouGeneratorInfo> Gen = Args.Gen;
		if (!Gen.IsValid())
		{
			OnDone.ExecuteIfBound(nullptr, TEXT("no generator"));
			return;
		}

		const TSharedPtr<IWatabouParser> Parser = Gen->CreateParser();
		if (!Parser.IsValid())
		{
			UE_LOG(LogWatabou, Log, TEXT("[%s] no parser registered -- raw JSON dropped, no asset saved"),
				*Gen->Id);
			OnDone.ExecuteIfBound(nullptr,
				FString::Printf(TEXT("no parser for generator '%s'"), *Gen->Id));
			return;
		}

		TSharedPtr<FJsonObject> JsonObj;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WatabouAssetBuilder::DeserializeJson);
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Args.Payload.Content);
			if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
			{
				OnDone.ExecuteIfBound(nullptr, TEXT("JSON parse failed"));
				return;
			}
		}

		// Prefer the requesting seed ref (recursive / programmatic imports through a browser slot): the
		// asset's identity then equals the seed that asked for it, which is what FWatabouSeedCache and
		// Seed Ref chaining match on. Manual panel imports leave it invalid and fall back to the browser
		// StateUrl (authoritative -- reflects in-browser tweaks).
		const FWatabouSeedRef SeedRef = Args.RequestedRef.IsValid()
			? Args.RequestedRef
			: WatabouUrlUtils::ParseQueryString(Gen->Id, Args.Payload.StateUrl);

		UClass* AssetClass = UWatabouAssetBase::StaticClass();

		// Locate an existing import by IDENTITY so a re-import updates it IN PLACE wherever the user has
		// moved/renamed it (preserving every PCG-graph / level reference) instead of cloning at a fresh
		// path. The canonical-key registry tag is name/path-independent, so Find is the AUTHORITATIVE
		// locator. The ref's ResolvedAsset pointer is only a hint: used as a fallback when Find misses
		// (e.g. registry not yet indexed) and ONLY when it still resolves to an asset of the SAME
		// canonical key -- so a drifted/stale pointer can never clobber a different-identity asset.
		UWatabouAssetBase* Asset = nullptr;
		{
			const TSoftObjectPtr<UWatabouAssetBase> ByKey = FWatabouSeedCache::Find(SeedRef);
			if (!ByKey.IsNull()) { Asset = ByKey.LoadSynchronous(); }
		}
		if (!Asset && !SeedRef.ResolvedAsset.IsNull())
		{
			UWatabouAssetBase* Hinted = SeedRef.ResolvedAsset.LoadSynchronous();
			if (Hinted && Hinted->SourceSeed.GetCanonicalKey() == SeedRef.GetCanonicalKey())
			{
				Asset = Hinted;
			}
		}
		bool bUpdatingExisting = (Asset != nullptr);

		UPackage* Package = nullptr;
		if (Asset)
		{
			// In-place update: keep the asset's current package/name. Reset() clears the prior parse
			// (Title / tags / Bounds / Identifiers / Features) so nothing accumulates.
			Package = Asset->GetPackage();
			Package->FullyLoad();
			Asset->Modify();
			Asset->Reset();
			Asset->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		}
		else
		{
			// New identity: name it by the generator's title when present, else the stable hash. The hash
			// is also the disambiguator if the friendly path is already taken by an unrelated asset.
			const FString Hash = SeedRef.GetCanonicalHash();
			// ASCII-fold the friendly title (drops spaces/punctuation/non-ASCII -> portable filenames),
			// then bound its length. Empty (no title, or an all-non-ASCII title) falls back to the hash.
			FString Stem = TruncateMiddle(MakeAsciiSlug(WatabouUrlUtils::PeekFriendlyName(JsonObj, SeedRef)), 20);
			if (Stem.IsEmpty()) { Stem = Hash; }

			const FString TopLevelDir = Args.TopLevelBaseDir.IsEmpty()
				? FString(TEXT("/Game/WatabouImports"))
				: Args.TopLevelBaseDir;
			const FString PackageDir = Args.ParentAssetPath.IsEmpty()
				? TopLevelDir
				: Args.ParentAssetPath + TEXT("/") + Gen->GetShortId();

			FString AssetName = FString::Printf(TEXT("WTB_%s_%s"),
				*SanitizeForAssetName(Gen->GetShortId()), *Stem);
			FString PackagePath = PackageDir + TEXT("/") + AssetName;

			// A same-identity asset would have been found above, so any occupant of the friendly path is
			// unrelated -- fall back to the pure hash, which is unique per canonical key.
			const bool bOccupied = FPackageName::DoesPackageExist(PackagePath)
				|| FindPackage(nullptr, *PackagePath) != nullptr;
			if (Stem != Hash && bOccupied)
			{
				AssetName = FString::Printf(TEXT("WTB_%s_%s"),
					*SanitizeForAssetName(Gen->GetShortId()), *Hash);
				PackagePath = PackageDir + TEXT("/") + AssetName;
			}

			Package = CreatePackage(*PackagePath);
			if (!Package)
			{
				OnDone.ExecuteIfBound(nullptr,
					FString::Printf(TEXT("CreatePackage failed for %s"), *PackagePath));
				return;
			}
			Package->FullyLoad();

			const FName AssetFName(*AssetName);

			// A foreign object of a DIFFERENT class squatting this exact path is trashed so NewObject can
			// claim the name (rare -- only a non-Watabou asset already at the friendly/hash path).
			{
				TArray<UObject*> InnerObjects;
				GetObjectsWithOuter(Package, InnerObjects, EGetObjectsFlags::None);
				for (UObject* Obj : InnerObjects)
				{
					if (!Obj || Obj->GetFName() != AssetFName) { continue; }
					if (Obj->GetClass() == AssetClass)
					{
						Asset = CastChecked<UWatabouAssetBase>(Obj);
						Asset->Modify();
						Asset->Reset();
						Asset->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
						bUpdatingExisting = true;   // adopting an asset already sitting at this path
					}
					else
					{
						UE_LOG(LogWatabou, Log, TEXT("trashing existing %s (class %s) -- class mismatch at import path"),
							*AssetName, *Obj->GetClass()->GetName());
						Obj->ClearFlags(RF_Public | RF_Standalone);
						Obj->Rename(/*NewName*/ nullptr, /*NewOuter*/ GetTransientPackage(),
							REN_DontCreateRedirectors | REN_NonTransactional);
					}
					break; // only one object can hold the name
				}
			}

			if (!Asset)
			{
				Asset = NewObject<UWatabouAssetBase>(
					Package, AssetClass, AssetFName,
					RF_Public | RF_Standalone | RF_Transactional);
			}

			// Identity is set ONCE, at creation. An existing asset found by identity above keeps its
			// original SourceSeed, so a drift-tolerant pointer match can't silently migrate its key.
			Asset->SourceSeed = SeedRef;
			Asset->SourceSeed.ResolvedAsset.Reset();   // identity field carries no self-pointer
		}

		Asset->BundleVersion = Gen->BundleVersion;

		FString ParseError;
		bool bParsed = false;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WatabouAssetBuilder::ParserDispatch);
			bParsed = Parser->Parse(JsonObj, Asset, ParseError);
		}

		if (!bParsed)
		{
			// A re-import that fails to parse must NOT overwrite the previously-good asset on disk. Reset()
			// already cleared the in-memory copy, but the saved copy is untouched until DeferredSavePackage
			// -- so skip the save and bail, leaving disk intact (a reload restores the in-memory object).
			if (bUpdatingExisting)
			{
				UE_LOG(LogWatabou, Warning, TEXT("[%s] re-import parse failed (%s) -- keeping the existing saved asset, not overwriting"),
					*Gen->Id, *ParseError);
				OnDone.ExecuteIfBound(nullptr,
					FString::Printf(TEXT("parse failed for '%s'; existing asset preserved"), *Gen->Id));
				return;
			}
			UE_LOG(LogWatabou, Warning, TEXT("[%s] parser reported: %s -- saving new asset with seed metadata only (no parsed features)"),
				*Gen->Id, *ParseError);
		}

		if (Asset->GenerationTags.IsEmpty())
		{
			Asset->GenerationTags = Asset->SourceSeed.Tags;
		}
		if (Asset->Title.IsEmpty())
		{
			// Same friendly-name source the asset name uses (name param / JSON title / JSON name),
			// centralized in PeekFriendlyName; fall back to the raw seed when none is supplied.
			const FString Friendly = WatabouUrlUtils::PeekFriendlyName(JsonObj, Asset->SourceSeed);
			Asset->Title = Friendly.IsEmpty() ? Asset->SourceSeed.Seed : Friendly;
		}
		if (Asset->BundleVersion.IsEmpty())
		{
			Asset->BundleVersion = WatabouCachePaths::ReadActiveVersion(Gen->Id);
		}

		// Link this asset's nested seed refs to any children already imported (the drift-immune fast
		// path for consumers). No-op when it has no refs or its children aren't imported yet; recursive
		// batches re-run this at Finish once every child exists.
		FWatabouSeedCache::RelinkAsset(Asset);

		if (GHooks.ApplyThumbnail)
		{
			GHooks.ApplyThumbnail(Asset, Args.Payload.ThumbnailDataUrl);
		}

		if (GHooks.RegisterCreatedAsset)
		{
			GHooks.RegisterCreatedAsset(Asset);
		}

		// Derived from the actual package -- for in-place updates this is the asset's existing home,
		// which may differ from any path we would have computed (the user may have moved it).
		const FString PackagePath = Package->GetName();

		if (!Args.bAutoSave || !GHooks.DeferredSavePackage)
		{
			UE_LOG(LogWatabou, Log, TEXT("Imported %s (unsaved%s)"),
				*PackagePath, Args.bAutoSave ? TEXT(" -- no save hook registered") : TEXT(""));
			OnDone.ExecuteIfBound(Asset, FString());
			return;
		}

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());

		GHooks.DeferredSavePackage(Package, Asset, PackagePath, PackageFileName, OnDone);
	}
}
