// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouExportPayload.h"
#include "WatabouSeedRef.h"

class UPackage;
class UWatabouAssetBase;
struct FWatabouGeneratorInfo;

DECLARE_DELEGATE_TwoParams(FOnWatabouAssetBuilt, UWatabouAssetBase* /*Asset*/, const FString& /*Error*/);

/**
 * Slate-free helper that owns the "what to do with an export payload" half of
 * the import pipeline:
 *
 *   1) parse JSON via the generator's IWatabouParser
 *   2) NewObject<UWatabouAssetBase> in a nested package path
 *   3) (editor hook) apply the captured thumbnail
 *   4) (editor hook) MarkPackageDirty + AssetRegistry::AssetCreated
 *   5) (editor hook) defer SavePackage to next tick, or skip at runtime
 *
 * Editor-only operations are injected via FHostHooks, NOT hard-coded with
 * WITH_EDITOR. This keeps Core decoupled from UnrealEd / ImageWrapper / GEditor.
 * The editor module registers hooks in StartupModule; null hooks at runtime
 * mean those steps are skipped silently.
 *
 * Threading: game thread only. OnDone fires on game thread, synchronously when
 * bAutoSave is false (or at runtime regardless), and on the next tick when
 * bAutoSave is true and the editor's DeferredSavePackage hook is registered.
 */
namespace WatabouAssetBuilder
{
	struct FBuildArgs
	{
		TSharedPtr<FWatabouGeneratorInfo> Gen;
		FWatabouExportPayload Payload;

		/**
		 * The seed ref that requested this import, when known (recursive / programmatic imports through
		 * a browser slot). When valid it becomes the asset's SourceSeed, so the asset's identity equals
		 * the seed that asked for it -- which is what FWatabouSeedCache and Seed Ref chaining match on.
		 * Left invalid for manual panel imports, which take their identity from the browser StateUrl.
		 */
		FWatabouSeedRef RequestedRef;

		/** Package path of the parent asset that referenced this seed. Empty = top-level
		 *  (lands at <TopLevelBaseDir>/<AssetName>). Non-empty nests under
		 *  <ParentAssetPath>/<Gen.GetShortId()>/<AssetName>. */
		FString ParentAssetPath;

		/** Content folder for TOP-LEVEL imports (used only when ParentAssetPath is empty).
		 *  Empty = the built-in default "/Game/WatabouImports". Editor callers set this from the
		 *  user-chosen import target dir; kept separate from ParentAssetPath so it doesn't disturb
		 *  the recursion-nesting semantics. */
		FString TopLevelBaseDir;

		/** When true, the DeferredSavePackage hook is invoked. Has no effect when the
		 *  hook isn't registered (runtime). */
		bool bAutoSave = false;
	};

	/**
	 * Hook table the editor module registers at startup. Each hook is optional;
	 * unregistered hooks (TFunction empty) skip that step.
	 */
	struct WATABOUCORE_API FHostHooks
	{
		/** Decode + cache the PNG data URL onto the asset's package as a UE thumbnail.
		 *  Editor-only -- ThumbnailTools lives in UnrealEd. */
		TFunction<void(UWatabouAssetBase* /*Asset*/, const FString& /*DataUrl*/)> ApplyThumbnail;

		/** Post-create asset finalization: MarkPackageDirty + AssetRegistry::AssetCreated.
		 *  Editor-only. */
		TFunction<void(UWatabouAssetBase* /*Asset*/)> RegisterCreatedAsset;

		/** Run SavePackage on the next tick (so Slate paints the "saving..." state first).
		 *  Editor-only. When null, bAutoSave is a no-op and OnDone fires synchronously
		 *  with the unsaved asset. When set, the hook owns calling OnDone after the save
		 *  attempt resolves. */
		TFunction<void(
			UPackage* /*Package*/,
			UWatabouAssetBase* /*Asset*/,
			const FString& /*PackagePath*/,
			const FString& /*PackageFileName*/,
			FOnWatabouAssetBuilt /*OnDone*/)> DeferredSavePackage;
	};

	WATABOUCORE_API void SetHostHooks(FHostHooks Hooks);
	WATABOUCORE_API const FHostHooks& GetHostHooks();

	/** Run the build pipeline. OnDone fires once on the game thread with the
	 *  resulting asset (or an error string). */
	WATABOUCORE_API void BuildAssetFromExport(const FBuildArgs& Args, FOnWatabouAssetBuilt OnDone);
}
