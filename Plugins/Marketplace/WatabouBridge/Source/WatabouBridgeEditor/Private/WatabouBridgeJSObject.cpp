// Copyright 2026 Timothé Lapetite

#include "WatabouBridgeJSObject.h"
#include "WatabouBridge.h"
#include "WatabouShim.h"

void UWatabouBridgeJSObject::OnBridgeMessage(const FString& RawSentinel)
{
	FString Event;
	FWatabouExportPayload Payload;
	FString ErrorMessage;
	if (!WatabouShim::TryParseBridgeMessage(RawSentinel, Event, Payload, ErrorMessage))
	{
		// Only bridge sentinels are ever sent to this bound method, so a miss means a shim/version
		// mismatch rather than ordinary console chatter (bundle logs go to console.log, not here).
		UE_LOG(LogWatabou, Verbose, TEXT("OnBridgeMessage: unrecognized payload (%d chars)"), RawSentinel.Len());
		return;
	}

	// Reaching here means the window.ue transport is live (console.log never routes through this
	// bound method). Verbose transport diagnostic -- the console.log fallback logs a matching
	// "console.log sentinel" line in the panel's / slot's HandleConsoleMessage, so
	// -LogCmds="LogWatabou Verbose" reveals which channel a given backend actually used.
	UE_LOG(LogWatabou, Verbose, TEXT("Bridge transport: window.ue (event='%s')"), *Event);

	// Funnel into the same typed handlers the console-log fallback uses (OnConsoleMessage ->
	// TryParseBridgeMessage -> these), so both transports share one dispatch path.
	if (Event == TEXT("export"))     { OnExport(Payload); }
	else if (Event == TEXT("ready")) { OnReady(); }
	else if (Event == TEXT("error")) { OnError(ErrorMessage); }
}

void UWatabouBridgeJSObject::OnExport(const FWatabouExportPayload& Payload)
{
	UE_LOG(LogWatabou, Log, TEXT("Export received: %s (%d chars, state='%s', thumb=%d bytes)"),
		*Payload.FileName, Payload.Content.Len(), *Payload.StateUrl, Payload.ThumbnailDataUrl.Len());
	OnExportReceived.Broadcast(Payload);
	OnExportReceivedNative.Broadcast(Payload);
}

void UWatabouBridgeJSObject::OnReady()
{
	UE_LOG(LogWatabou, Verbose, TEXT("Bundle ready"));
	OnReadySignal.Broadcast(FString());
	OnReadySignalNative.Broadcast(FString());
}

void UWatabouBridgeJSObject::OnError(const FString& Message)
{
	UE_LOG(LogWatabou, Warning, TEXT("JS error: %s"), *Message);
	OnErrorSignal.Broadcast(Message);
	OnErrorSignalNative.Broadcast(Message);
}
