// Copyright 2026 Timothé Lapetite

#include "WatabouResourceUpdater.h"
#include "WatabouBridge.h"
#include "WatabouGeneratorInfo.h"
#include "WatabouGeneratorRegistry.h"
#include "WatabouResourceManifest.h"
#include "WatabouBundlePatchSpec.h"
#include "WatabouCachePaths.h"
#include "WatabouVersionUtils.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Internationalization/Regex.h"
#include "Async/Async.h"                                 // AsyncTask (dispatch the toast to the game thread)
#include "Misc/App.h"                                    // FApp::CanEverRender (skip the toast when headless)
#include "Framework/Notifications/NotificationManager.h" // FSlateNotificationManager / FNotificationInfo
#include "Widgets/Notifications/SNotificationList.h"      // SNotificationItem

namespace WatabouResourceUpdater_Internal
{
	// Raise an in-editor error toast for a rejected (load-bearing) bundle patch. Safe from any thread and
	// a no-op in headless / commandlet runs (FApp::CanEverRender() is false there). The bundle was NOT
	// written, so the caller keeps the previous good copy -- this just makes the rejection discoverable.
	static void NotifyBundlePatchRejected(const FString& GeneratorId, const FString& PatchLabel)
	{
		if (!FApp::CanEverRender()) { return; } // headless -> the Output Log error is the only signal

		const FText Message = FText::Format(
			NSLOCTEXT("WatabouBridge", "BundlePatchRejected",
				"Watabou: '{0}' generator update was rejected.\nThe bundle patch \"{1}\" no longer matches the upstream script, so the download was discarded and the previous copy kept. The patch pattern needs updating."),
			FText::FromString(GeneratorId), FText::FromString(PatchLabel));

		AsyncTask(ENamedThreads::GameThread, [Message]()
		{
			FNotificationInfo Info(Message);
			Info.ExpireDuration = 12.0f;
			Info.bFireAndForget = true;
			TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
			if (Item.IsValid()) { Item->SetCompletionState(SNotificationItem::CS_Fail); }
		});
	}

	static FString JoinUrl(const FString& Base, const FString& Rel)
	{
		FString Out = Base;
		if (Out.EndsWith(TEXT("/"))) { Out.LeftChopInline(1); }
		FString R = Rel;
		while (R.StartsWith(TEXT("/"))) { R.RightChopInline(1); }
		return Out + TEXT("/") + R;
	}

	static bool IsPathSafe(const FString& Rel)
	{
		return !Rel.Contains(TEXT("..")) && !Rel.IsEmpty();
	}

	static FString RegexStripAll(const FString& In, const FRegexPattern& Pattern)
	{
		FRegexMatcher Matcher(Pattern, In);
		FString Out; Out.Reserve(In.Len());
		int32 LastEnd = 0;
		while (Matcher.FindNext())
		{
			Out.Append(*In + LastEnd, Matcher.GetMatchBeginning() - LastEnd);
			LastEnd = Matcher.GetMatchEnding();
		}
		Out.Append(*In + LastEnd, In.Len() - LastEnd);
		return Out;
	}
}

FWatabouResourceUpdater::~FWatabouResourceUpdater()
{
	// Normal completion clears Generators in PromoteAllStagings (which also deletes each staging
	// dir). If we're destroyed before that -- e.g. the owning panel closed mid-download -- delete
	// any leftover .staging-<token> dirs here so they don't accumulate in the cache. Safe: HTTP
	// completions hold only a weak ref, so none can be writing into staging once we're destructing.
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	for (const FGeneratorBatchState& State : Generators)
	{
		if (!State.StagingDir.IsEmpty())
		{
			PF.DeleteDirectoryRecursively(*State.StagingDir);
		}
	}
}

FWatabouResourceUpdater::FUpdateBatch FWatabouResourceUpdater::BuildBatchForAllGenerators()
{
	FUpdateBatch Batch;
	for (const TSharedRef<FWatabouGeneratorInfo>& Info : FWatabouGeneratorRegistry::Get().GetAll())
	{
		AppendBatchForGenerator(Info.Get(), Batch);
	}
	return Batch;
}

void FWatabouResourceUpdater::AppendBatchForGenerator(const FWatabouGeneratorInfo& Info, FUpdateBatch& OutBatch)
{
	using namespace WatabouResourceUpdater_Internal;

	const FWatabouResourceManifest Manifest = Info.GetResourceManifest();
	if (Manifest.UpstreamBaseUrl.IsEmpty()) { return; }

	FGeneratorBatchState State;
	State.GeneratorId      = Info.Id;
	State.UpstreamBaseUrl  = Manifest.UpstreamBaseUrl;
	State.StagingDir       = WatabouCachePaths::GetStagingDir(Info.Id, FGuid::NewGuid().ToString(EGuidFormats::Short));

	auto AddJob = [&](const FString& RelPath, EJobKind Kind, const FString& Fallback = FString())
	{
		if (!IsPathSafe(RelPath)) { return; }
		FFetchJob& J = OutBatch.Jobs.AddDefaulted_GetRef();
		J.Url                 = JoinUrl(Manifest.UpstreamBaseUrl, RelPath);
		J.StagingAbsolutePath = FPaths::Combine(State.StagingDir, RelPath);
		J.Kind                = Kind;
		J.FallbackContent     = Fallback;
		J.Label               = FString::Printf(TEXT("%s/%s"), *Info.Id, *RelPath);
		J.GeneratorId         = Info.Id;
	};

	// index.html
	if (!Manifest.IndexFile.IsEmpty())
	{
		AddJob(Manifest.IndexFile, EJobKind::IndexHtml);
		State.StagingIndexPath = FPaths::Combine(State.StagingDir, Manifest.IndexFile);
	}
	// Bundle JS
	if (!Manifest.BundleScript.IsEmpty())
	{
		AddJob(Manifest.BundleScript, EJobKind::BundleScript);
		State.StagingBundleScriptPath = FPaths::Combine(State.StagingDir, Manifest.BundleScript);
	}
	// Top-level files (favicon etc.)
	for (const FString& Rel : Manifest.TopLevelFiles)
	{
		AddJob(Rel, EJobKind::Raw);
	}
	// Fonts -- each stem expanded to .eot/.woff/.ttf/.svg under AssetsDir
	const FString AssetsDir = Manifest.AssetsDir.IsEmpty() ? TEXT("Assets") : Manifest.AssetsDir;
	static const TCHAR* FontExts[] = { TEXT("eot"), TEXT("woff"), TEXT("ttf"), TEXT("svg") };
	for (const FString& Stem : Manifest.FontStems)
	{
		for (const TCHAR* Ext : FontExts)
		{
			AddJob(FString::Printf(TEXT("%s/%s.%s"), *AssetsDir, *Stem, Ext), EJobKind::Raw);
		}
	}
	// Runtime assets (JSONs, TXTs)
	for (const FString& Asset : Manifest.RuntimeAssets)
	{
		const FString Rel = FString::Printf(TEXT("%s/%s"), *AssetsDir, *Asset);
		const FString* Fallback = Manifest.Placeholders.Find(Asset);
		AddJob(Rel, EJobKind::Raw, Fallback ? *Fallback : FString());
	}

	OutBatch.Generators.Add(MoveTemp(State));
}

void FWatabouResourceUpdater::Run(FUpdateBatch&& Batch,
	FOnWatabouUpdaterProgress OnProgress,
	FOnWatabouUpdaterFinished OnFinished)
{
	ProgressDelegate = OnProgress;
	FinishedDelegate = OnFinished;

	Generators   = MoveTemp(Batch.Generators);
	PendingJobs  = MoveTemp(Batch.Jobs);
	InFlightJobs = 0;

	TotalCount.Set(PendingJobs.Num());
	CompletedCount.Set(0);
	SucceededCount.Set(0);
	FailedCount.Set(0);

	if (PendingJobs.Num() == 0)
	{
		PromoteAllStagings();
		FinishedDelegate.ExecuteIfBound(0, 0, 0);
		return;
	}

	// Ensure each generator's staging directory is clean before writing into it.
	// DeleteDirectoryRecursively / CreateDirectoryTree are both no-ops on absent / present dirs.
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	for (const FGeneratorBatchState& State : Generators)
	{
		PF.DeleteDirectoryRecursively(*State.StagingDir);
		PF.CreateDirectoryTree(*State.StagingDir);
	}

	DispatchMoreFetches();
}

void FWatabouResourceUpdater::DispatchMoreFetches()
{
	FHttpModule& Http = FHttpModule::Get();
	TWeakPtr<FWatabouResourceUpdater> WeakSelf = AsShared();

	while (InFlightJobs < MaxConcurrentFetches && PendingJobs.Num() > 0)
	{
		// Pop from the back -- fetch order is irrelevant (each job writes its own staging path),
		// so this avoids an O(n) RemoveAt(0) per dispatch.
		FFetchJob JobCopy = PendingJobs.Pop(EAllowShrinking::No);
		++InFlightJobs;

		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = Http.CreateRequest();
		Req->SetURL(JobCopy.Url);
		Req->SetVerb(TEXT("GET"));
		Req->SetTimeout(RequestTimeoutSeconds);   // bound a stalled fetch (e.g. Update while offline)
		Req->SetHeader(TEXT("User-Agent"),
			TEXT("Mozilla/5.0 (WatabouBridge UE5.7 plugin)"));

		Req->OnProcessRequestComplete().BindLambda(
			[WeakSelf, JobCopy](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
			{
				TSharedPtr<FWatabouResourceUpdater> Self = WeakSelf.Pin();
				if (!Self.IsValid()) { return; }

				Self->OnHttpComplete(Request, bSucceeded && Response.IsValid(), JobCopy);
			});
		Req->ProcessRequest();
	}
}

void FWatabouResourceUpdater::OnHttpComplete(TSharedPtr<IHttpRequest> Request, bool bSucceeded, FFetchJob Job)
{
	if (InFlightJobs > 0) { --InFlightJobs; }

	bool bWroteSomething = false;
	FString WriteError;
	int32 HttpCode = 0;

	const FHttpResponsePtr Response = Request.IsValid() ? Request->GetResponse() : nullptr;
	if (Response.IsValid()) { HttpCode = Response->GetResponseCode(); }

	if (bSucceeded && Response.IsValid() && HttpCode >= 200 && HttpCode < 300)
	{
		bWroteSomething = WriteResponseToDisk(Job, Response->GetContent(), WriteError);
	}
	else if (HttpCode == 404 && !Job.FallbackContent.IsEmpty())
	{
		TArray<uint8> Bytes;
		FTCHARToUTF8 Conv(*Job.FallbackContent);
		Bytes.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
		bWroteSomething = WriteResponseToDisk(Job, Bytes, WriteError);
		if (bWroteSomething)
		{
			UE_LOG(LogWatabou, Verbose, TEXT("[updater] %s -> placeholder (upstream 404)"), *Job.Label);
		}
	}

	if (bWroteSomething) { SucceededCount.Increment(); }
	else
	{
		FailedCount.Increment();
		UE_LOG(LogWatabou, Verbose, TEXT("[updater] FAIL %s (http=%d, err=%s)"),
			*Job.Label, HttpCode, *WriteError);
	}

	const int32 Done = CompletedCount.Increment();
	const int32 Total = TotalCount.GetValue();
	ProgressDelegate.ExecuteIfBound(Done, Total);

	if (Done >= Total)
	{
		PromoteAllStagings();
		const int32 OK  = SucceededCount.GetValue();
		const int32 Bad = FailedCount.GetValue();
		FinishedDelegate.ExecuteIfBound(OK, Bad, Total);
	}
	else
	{
		// Refill the slot this completion just freed.
		DispatchMoreFetches();
	}
}

bool FWatabouResourceUpdater::WriteResponseToDisk(const FFetchJob& Job, const TArray<uint8>& Bytes, FString& OutError) const
{
	const FString ParentDir = FPaths::GetPath(Job.StagingAbsolutePath);
	IFileManager::Get().MakeDirectory(*ParentDir, /*Tree=*/true);

	if (Job.Kind == EJobKind::IndexHtml)
	{
		FString In;
		FFileHelper::BufferToString(In, Bytes.GetData(), Bytes.Num());
		FString Out;
		if (StripAnalytics(In, Out))
		{
			return FFileHelper::SaveStringToFile(Out, *Job.StagingAbsolutePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		OutError = TEXT("StripAnalytics failed");
		return false;
	}

	if (Job.Kind == EJobKind::BundleScript)
	{
		FString In;
		FFileHelper::BufferToString(In, Bytes.GetData(), Bytes.Num());
		FString Out;
		if (ApplyBundlePatch(Job.GeneratorId, In, Out))
		{
			return FFileHelper::SaveStringToFile(Out, *Job.StagingAbsolutePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
		OutError = TEXT("Patch pattern not found in bundle");
		return false;
	}

	return FFileHelper::SaveArrayToFile(Bytes, *Job.StagingAbsolutePath);
}

bool FWatabouResourceUpdater::StripAnalytics(const FString& InHtml, FString& OutHtml) const
{
	using namespace WatabouResourceUpdater_Internal;
	static const FRegexPattern GtagScriptTag(TEXT(R"(<script[^>]*googletagmanager[^>]*></script>\s*)"));
	// Anchor on `<script>` immediately followed by `window.dataLayer` -- the unmistakable
	// GA-init signature. The older `<script>[\s\S]*?dataLayer` form would greedily eat
	// any earlier inline `<script>` block (e.g. the touchmove handler in urban-places),
	// dragging the `<style>` block and `</head>` along with it.
	static const FRegexPattern GtagInlineConfig(TEXT(R"(<script>\s*window\.dataLayer[\s\S]*?</script>\s*)"));
	// Tidy the orphaned `<!-- Google tag -->` comment some upstream pages leave behind.
	static const FRegexPattern GtagComment(TEXT(R"((?m)^\s*<!--\s*Google tag[^\n]*-->\s*\r?\n)"));
	OutHtml = RegexStripAll(RegexStripAll(RegexStripAll(InHtml, GtagScriptTag), GtagInlineConfig), GtagComment);
	return true;
}

bool FWatabouResourceUpdater::ApplyBundlePatch(const FString& GeneratorId, const FString& InText, FString& OutText) const
{
	// 1. Universal Haxe registry patch: expose the bundle's class registry as window.__hx.
	//    Required -- a bundle whose IIFE doesn't match this shape is unusable by our shim, so we
	//    fail the write and the caller keeps the previously cached/vendored copy.
	const FRegexPattern Pattern(
		TEXT(R"(var\s+([a-zA-Z_$][\w$]{0,3})\s*=\s*\{\s*\}\s*,\s*([a-zA-Z_$][\w$]{0,3})\s*=\s*function\(\)\s*\{\s*return\s+([a-zA-Z_$][\w$]{0,3})\.__string_rec)"));

	FRegexMatcher Matcher(Pattern, InText);
	if (!Matcher.FindNext())
	{
		OutText = InText;
		return false;
	}

	const FString RegLetter = Matcher.GetCaptureGroup(1);
	const FString OtherVar  = Matcher.GetCaptureGroup(2);
	const FString StrRecVar = Matcher.GetCaptureGroup(3);

	const FString Replacement = FString::Printf(
		TEXT("var %s=window.__hx={},%s=function(){return %s.__string_rec"),
		*RegLetter, *OtherVar, *StrRecVar);

	OutText.Reserve(InText.Len() + 16);
	OutText.Append(*InText, Matcher.GetMatchBeginning());
	OutText.Append(Replacement);
	OutText.Append(*InText + Matcher.GetMatchEnding(), InText.Len() - Matcher.GetMatchEnding());

	// 2. Generator-specific secondary rewrites, declared on the generator's BundlePatch and applied in
	//    order to the already-patched text. A REQUIRED patch (Rep.bRequired) that matches 0 sites REJECTS
	//    the bundle (return false -> the caller keeps the previous cached / vendored copy) and raises an
	//    error + an in-editor notification: a 0-match means an upstream change moved the site, which would
	//    otherwise silently ship a broken feature (e.g. Dwellings reverting to the stock MAX_SIZE cap ->
	//    random shapes). An optional patch just warns and continues. Only generators that declare
	//    ExtraReplacements do anything here -- currently just Dwellings (Specs.MAX_SIZE).
	const TSharedPtr<FWatabouGeneratorInfo> Info = FWatabouGeneratorRegistry::Get().FindById(GeneratorId);
	if (Info.IsValid())
	{
		for (const FWatabouBundleReplacement& Rep : Info->BundlePatch.ExtraReplacements)
		{
			const FRegexPattern ExtraPattern(Rep.Pattern);
			FRegexMatcher ExtraMatcher(ExtraPattern, OutText);
			if (!ExtraMatcher.FindNext())
			{
				if (Rep.bRequired)
				{
					// Load-bearing patch went stale: reject the whole download so the broken bundle never
					// ships (the caller keeps the previous good copy), and make the failure impossible to miss.
					UE_LOG(LogWatabou, Error,
						TEXT("[updater] %s: REQUIRED bundle patch '%s' matched 0 sites -- the upstream bundle shape changed. Rejecting this download (keeping the previous copy); update the patch pattern in the generator's BundlePatch."),
						*GeneratorId, *Rep.Label);
					WatabouResourceUpdater_Internal::NotifyBundlePatchRejected(GeneratorId, Rep.Label);
					OutText = InText;
					return false;
				}

				UE_LOG(LogWatabou, Warning,
					TEXT("[updater] %s: optional bundle patch '%s' matched 0 sites -- the upstream bundle shape may have changed; dependent features may misbehave."),
					*GeneratorId, *Rep.Label);
				continue;
			}

			// Expand $1..$9 capture references in the replacement template so context the pattern had
			// to match (to disambiguate the site) is carried through unchanged.
			FString Expanded;
			Expanded.Reserve(Rep.Replacement.Len() + 8);
			for (int32 CharIdx = 0; CharIdx < Rep.Replacement.Len(); ++CharIdx)
			{
				const TCHAR Ch = Rep.Replacement[CharIdx];
				if (Ch == TEXT('$') && (CharIdx + 1) < Rep.Replacement.Len() && FChar::IsDigit(Rep.Replacement[CharIdx + 1]))
				{
					const int32 Group = Rep.Replacement[CharIdx + 1] - TEXT('0');
					Expanded.Append(ExtraMatcher.GetCaptureGroup(Group));
					++CharIdx;
				}
				else
				{
					Expanded.AppendChar(Ch);
				}
			}

			FString Spliced;
			Spliced.Reserve(OutText.Len() + Expanded.Len());
			Spliced.Append(*OutText, ExtraMatcher.GetMatchBeginning());
			Spliced.Append(Expanded);
			Spliced.Append(*OutText + ExtraMatcher.GetMatchEnding(), OutText.Len() - ExtraMatcher.GetMatchEnding());
			OutText = MoveTemp(Spliced);

			UE_LOG(LogWatabou, Log, TEXT("[updater] %s: applied bundle patch '%s'"), *GeneratorId, *Rep.Label);
		}
	}

	return true;
}

void FWatabouResourceUpdater::PromoteAllStagings()
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	for (const FGeneratorBatchState& State : Generators)
	{
		FString VersionOrError;
		if (PromoteGenerator(State, VersionOrError))
		{
			UE_LOG(LogWatabou, Log, TEXT("[updater] %s -> cached as version %s"),
				*State.GeneratorId, *VersionOrError);
		}
		else
		{
			UE_LOG(LogWatabou, Warning,
				TEXT("[updater] %s NOT promoted (%s) -- keeping previously cached/vendored bundle"),
				*State.GeneratorId, *VersionOrError);
		}

		// Always clear staging, success or fail, so partial/failed updates don't accumulate
		// stale .staging-<guid> dirs in the cache.
		PF.DeleteDirectoryRecursively(*State.StagingDir);
	}
	Generators.Reset();
}

bool FWatabouResourceUpdater::PromoteGenerator(const FGeneratorBatchState& State, FString& OutVersionOrError) const
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*State.StagingDir))
	{
		OutVersionOrError = TEXT("staging dir missing");
		return false;
	}

	// Don't promote a partial download over a previously-good cache. If the bundle script or
	// index failed to download/patch, its file won't be in staging -- skip promotion so the
	// prior cached version (or the vendored fallback) keeps serving this generator instead of
	// overwriting it with a broken/empty snapshot.
	if (!State.StagingBundleScriptPath.IsEmpty() && !PF.FileExists(*State.StagingBundleScriptPath))
	{
		OutVersionOrError = TEXT("bundle script absent from staging (download or patch failed)");
		return false;
	}
	if (!State.StagingIndexPath.IsEmpty() && !PF.FileExists(*State.StagingIndexPath))
	{
		OutVersionOrError = TEXT("index.html absent from staging (download failed)");
		return false;
	}

	// Extract bundle version from the just-downloaded JS, falling back to a
	// timestamped "unknown" label if the marker is absent.
	FString Version;
	if (!State.StagingBundleScriptPath.IsEmpty())
	{
		FString JsText;
		if (FFileHelper::LoadFileToString(JsText, *State.StagingBundleScriptPath))
		{
			Version = WatabouVersionUtils::ExtractBundleVersion(JsText);
		}
	}
	if (Version.IsEmpty())
	{
		Version = FString::Printf(TEXT("unknown-%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
	}

	const FString TargetDir = WatabouCachePaths::GetVersionCacheDir(State.GeneratorId, Version);

	// Wipe target if it already exists (re-running update with the same upstream version overwrites it),
	// then copy staging into place + clear staging. DeleteDirectoryRecursively no-ops on absent dir.
	PF.DeleteDirectoryRecursively(*TargetDir);
	PF.CreateDirectoryTree(*TargetDir);

	if (!PF.CopyDirectoryTree(*TargetDir, *State.StagingDir, /*bOverwriteAllExisting=*/true))
	{
		OutVersionOrError = TEXT("CopyDirectoryTree failed");
		return false;
	}
	// Staging cleanup is handled by PromoteAllStagings (runs on success and failure alike).

	WatabouCachePaths::WriteActiveVersion(State.GeneratorId, Version);

	OutVersionOrError = Version;
	return true;
}
