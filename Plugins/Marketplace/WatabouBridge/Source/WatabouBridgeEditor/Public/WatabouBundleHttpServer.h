// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpResultCallback.h"
#include "IHttpRouter.h"

/**
 * Local HTTP server that serves Watabou bundles to the embedded CEF browser.
 *
 * Per-generator source resolution at Start():
 *   The ACTIVE cache version (<Plugin>/Content/Bundles/<gen-id>/<active>/, the user's last
 *   explicit "Update Generators" download) is primary, EXCEPT when the SHIPPED snapshot
 *   (<Plugin>/Resources/Bundles/<gen-id>/, pre-patched, redistributed with watabou's
 *   permission -- see Resources/Bundles/NOTICE.md) is provably newer: both bundle versions
 *   parse and shipped > active, i.e. the plugin was updated past a stale download. Fresh
 *   installs (no cache) serve the shipped snapshot directly.
 *
 *   Fallback order after the primary: the other of the two above, then the newest cache
 *   version carrying a .ok marker (DORMANT -- nothing writes .ok yet), then the shipped
 *   snapshot served unconditionally as the floor.
 *
 *   Nothing on this path touches the network -- downloads happen only through the explicit
 *   "Update Generators" action.
 *
 * The server iterates every registered generator, picks its source directory, and binds
 * one route per file under URL prefix /<gen-id>/. The /<gen-id>/ and /<gen-id> URLs also
 * resolve to the generator's index.html for browser-style "directory" requests.
 *
 * UE's IHttpRouter is path-exact, so each file gets its own route. Bytes are
 * slurped at Start() and captured by the route lambda, so request handling
 * never touches the disk.
 */
class FWatabouBundleHttpServer
{
public:
	bool Start(uint16 PreferredPort = 0);
	void Stop();

	bool IsRunning() const { return Port != 0; }
	uint16 GetPort() const { return Port; }

	/** Returns true if at least one route was bound for the given generator id during Start(). */
	bool HasRoutesForGenerator(const FString& GeneratorId) const { return BoundGeneratorIds.Contains(GeneratorId); }

private:
	struct FGeneratorSource
	{
		FString GeneratorId;
		FString AbsoluteDir;    // either cache/<id>/<version>/ or Resources/Bundles/<id>/
		FString SourceLabel;    // "cache:1.8.0" / "vendored" / etc., for logging
	};

	int32 BindAllStaticRoutes();
	void ResolveGeneratorSources(TArray<FGeneratorSource>& Out) const;

	bool BindFileRoute(const FString& UrlPath, const TSharedRef<TArray<uint8>>& Bytes, const FString& ContentType);

	static uint16 ChooseEphemeralPort();

	uint16 Port = 0;
	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;

	/** Generator ids that successfully bound at least one route this Start cycle. */
	TSet<FString> BoundGeneratorIds;

	/** <Plugin>/Resources/Bundles -- root of the shipped bundle snapshots. */
	FString VendoredRoot;
};
