// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "WatabouRecursiveImporter.h"

class SNotificationItem;

/**
 * Bottom-right toast that mirrors the recursive importer's progress and exposes
 * the Abort button. One sticky notification per running batch; auto-dismisses
 * a few seconds after the batch completes.
 *
 * Module-lifetime singleton (siblings the pool + importer) so the toast is the
 * primary feedback channel when the import panel is closed.
 */
class WATABOUBRIDGEEDITOR_API FWatabouBatchNotifier : public TSharedFromThis<FWatabouBatchNotifier>
{
public:
	FWatabouBatchNotifier();
	~FWatabouBatchNotifier();

	void Initialize(const TSharedRef<FWatabouRecursiveImporter>& Importer);
	void Shutdown();

private:
	void HandleProgress(const FWatabouRecursiveImporter::FProgress& Progress);

	FText FormatStatusText(const FWatabouRecursiveImporter::FProgress& Progress) const;
	void EnsureNotification(const FWatabouRecursiveImporter::FProgress& Progress);
	void DismissNotification(const FWatabouRecursiveImporter::FProgress& Progress);

	TWeakPtr<FWatabouRecursiveImporter> WeakImporter;
	TSharedPtr<SNotificationItem>       NotificationItem;
	FDelegateHandle                     ProgressHandle;
};
