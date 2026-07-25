// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouExportPayload.generated.h"

/**
 * Everything an export from the JS shim carries. Grouped so adding fields
 * (e.g. preview metadata) doesn't churn delegate signatures across the codebase.
 *
 * FileName: name the bundle's saveAs override received (e.g. "city.json").
 * Content: full JSON text the bundle generated.
 * StateUrl: window.location.search at the moment of export -- authoritative
 *   for seed/tags/params, supersedes the panel's URL field if the user tweaked
 *   settings inside the embedded browser.
 * ThumbnailDataUrl: "data:image/png;base64,..." captured from the bundle's
 *   canvas/SVG, or empty if capture failed.
 *
 * Lives in WatabouCore so the shim parser and asset builder can be shared
 * between editor (SWebBrowser-driven flow) and a future runtime (UWebBrowser-
 * driven flow). The UWatabouBridgeJSObject UCLASS that fires this payload as a
 * BlueprintCallable UFUNCTION remains in the editor module.
 */
USTRUCT(BlueprintType)
struct WATABOUCORE_API FWatabouExportPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Watabou|Bridge")
	FString FileName;

	UPROPERTY(BlueprintReadOnly, Category = "Watabou|Bridge")
	FString Content;

	UPROPERTY(BlueprintReadOnly, Category = "Watabou|Bridge")
	FString StateUrl;

	UPROPERTY(BlueprintReadOnly, Category = "Watabou|Bridge")
	FString ThumbnailDataUrl;
};
