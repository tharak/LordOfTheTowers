// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"

class IHttpRequest;
struct FWatabouGeneratorInfo;

DECLARE_DELEGATE_TwoParams(FOnWatabouUpdaterProgress, int32 /*Completed*/, int32 /*Total*/);
DECLARE_DELEGATE_ThreeParams(FOnWatabouUpdaterFinished, int32 /*Succeeded*/, int32 /*Failed*/, int32 /*Total*/);

/**
 * Downloads + patches the resources declared by every registered generator's
 * FWatabouResourceManifest into the user-owned versioned cache.
 *
 * Flow per generator:
 *   1. Pick a unique staging token (FGuid).
 *   2. Issue HTTP requests in parallel; each writes to
 *      <cache>/<gen-id>/.staging-<token>/<relative-path>.
 *   3. Bundle JS receives the registry patch. index.html has Google Analytics
 *      stripped. Everything else is written verbatim.
 *   4. Once every job in the batch has resolved, the updater extracts the
 *      bundle version (via WatabouVersionUtils), promotes the staging directory
 *      to <cache>/<gen-id>/<version>/ (overwriting any prior copy of that
 *      version), and writes _active.txt = <version>.
 *
 * Older versions in the cache are NOT deleted, so users can roll back if a
 * newer Watabou release introduces a JSON-format break.
 *
 * Async via FHttpModule. Caller owns the TSharedPtr; the request lambdas hold
 * weak references so a dropped owner cleanly abandons in-flight work.
 */
class WATABOUBRIDGEEDITOR_API FWatabouResourceUpdater : public TSharedFromThis<FWatabouResourceUpdater>
{
public:
	enum class EJobKind : uint8
	{
		Raw,
		BundleScript,
		IndexHtml,
	};

	struct FFetchJob
	{
		FString Url;
		FString StagingAbsolutePath;
		EJobKind Kind = EJobKind::Raw;
		FString FallbackContent;
		FString Label;
		FString GeneratorId;   // back-reference for the promotion step
	};

	struct FGeneratorBatchState
	{
		FString GeneratorId;
		FString StagingDir;
		/** Path within StagingDir where the bundle JS lands. Empty if no bundle for this generator. */
		FString StagingBundleScriptPath;
		/** Path within StagingDir where index.html lands. Empty if no index for this generator. */
		FString StagingIndexPath;
		/** Source url to upstream (for logging on success). */
		FString UpstreamBaseUrl;
	};

	struct FUpdateBatch
	{
		TArray<FFetchJob> Jobs;
		TArray<FGeneratorBatchState> Generators;
	};

	/** Build a full batch (jobs + per-generator state) for every registered generator. */
	static FUpdateBatch BuildBatchForAllGenerators();

	/** Build for a single generator. Appends to OutBatch. */
	static void AppendBatchForGenerator(const FWatabouGeneratorInfo& Info, FUpdateBatch& OutBatch);

	/** Kick off the batch. Progress fires after each job; Finished fires after all jobs + promotion. */
	void Run(FUpdateBatch&& Batch,
		FOnWatabouUpdaterProgress OnProgress,
		FOnWatabouUpdaterFinished OnFinished);

	/** Cleans up any leftover staging dirs if the updater is destroyed mid-batch (e.g. the owning
	 *  panel closed before completion, so PromoteAllStagings never ran). */
	~FWatabouResourceUpdater();

	bool IsRunning() const { return TotalCount.GetValue() > CompletedCount.GetValue(); }
	int32 GetCompleted() const { return CompletedCount.GetValue(); }
	int32 GetTotal() const { return TotalCount.GetValue(); }

private:
	/** Issue HTTP requests for queued jobs up to MaxConcurrentFetches in flight. Called from
	 *  Run() and again from each OnHttpComplete to refill the freed slot. */
	void DispatchMoreFetches();
	void OnHttpComplete(TSharedPtr<IHttpRequest> Request, bool bSucceeded, FFetchJob Job);
	bool WriteResponseToDisk(const FFetchJob& Job, const TArray<uint8>& Bytes, FString& OutError) const;
	bool ApplyBundlePatch(const FString& GeneratorId, const FString& InText, FString& OutText) const;
	bool StripAnalytics(const FString& InHtml, FString& OutHtml) const;

	/** After every job has resolved, finalize each generator's staging dir into <cache>/<id>/<version>/. */
	void PromoteAllStagings();
	bool PromoteGenerator(const FGeneratorBatchState& State, FString& OutVersionOrError) const;

	FOnWatabouUpdaterProgress ProgressDelegate;
	FOnWatabouUpdaterFinished FinishedDelegate;

	FThreadSafeCounter CompletedCount;
	FThreadSafeCounter TotalCount;
	FThreadSafeCounter SucceededCount;
	FThreadSafeCounter FailedCount;

	/** Max simultaneous in-flight HTTP fetches. Caps load on upstream and keeps a transient
	 *  failure from cascading across a hundred concurrent requests. */
	static constexpr int32 MaxConcurrentFetches = 6;

	/** Per-request HTTP timeout (seconds). Bounds a stalled fetch -- e.g. Update Generators
	 *  clicked while offline -- so the blocking overlay can't hang waiting on a dead socket. */
	static constexpr float RequestTimeoutSeconds = 30.0f;

	/** Jobs not yet dispatched. Drained by DispatchMoreFetches (popped from the back -- fetch
	 *  order is irrelevant since each job writes to its own staging path). Game-thread only. */
	TArray<FFetchJob> PendingJobs;
	/** Number of HTTP requests currently in flight. Game-thread only (HTTP completions tick
	 *  on the game thread), so a plain int32 is sufficient. */
	int32 InFlightJobs = 0;

	TArray<FGeneratorBatchState> Generators;
};
