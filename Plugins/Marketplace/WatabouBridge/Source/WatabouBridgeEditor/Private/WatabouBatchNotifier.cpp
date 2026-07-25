// Copyright 2026 Timothé Lapetite

#include "WatabouBatchNotifier.h"

#include "WatabouBridge.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "WatabouBatchNotifier"

FWatabouBatchNotifier::FWatabouBatchNotifier() = default;
FWatabouBatchNotifier::~FWatabouBatchNotifier() = default;

void FWatabouBatchNotifier::Initialize(const TSharedRef<FWatabouRecursiveImporter>& Importer)
{
	WeakImporter = Importer;
	ProgressHandle = Importer->OnProgressChanged.AddSP(this, &FWatabouBatchNotifier::HandleProgress);
}

void FWatabouBatchNotifier::Shutdown()
{
	if (TSharedPtr<FWatabouRecursiveImporter> Importer = WeakImporter.Pin())
	{
		Importer->OnProgressChanged.Remove(ProgressHandle);
	}
	WeakImporter.Reset();
	ProgressHandle.Reset();

	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_None);
		NotificationItem->ExpireAndFadeout();
		NotificationItem.Reset();
	}
}

FText FWatabouBatchNotifier::FormatStatusText(const FWatabouRecursiveImporter::FProgress& Progress) const
{
	if (Progress.State == FWatabouRecursiveImporter::EBatchState::Done)
	{
		return FText::Format(
			LOCTEXT("DoneFmt", "Watabou batch done -- {0} imported, {1} failed"),
			FText::AsNumber(Progress.Done), FText::AsNumber(Progress.Failed));
	}
	if (Progress.State == FWatabouRecursiveImporter::EBatchState::Cancelling)
	{
		return FText::Format(
			LOCTEXT("CancellingFmt", "Cancelling... {0}/{1} done ({2} in flight)"),
			FText::AsNumber(Progress.Done), FText::AsNumber(Progress.Total),
			FText::AsNumber(Progress.InFlight));
	}
	return FText::Format(
		LOCTEXT("RunningFmt", "Watabou batch: {0}/{1} done, {2} failed ({3} in flight)"),
		FText::AsNumber(Progress.Done), FText::AsNumber(Progress.Total),
		FText::AsNumber(Progress.Failed), FText::AsNumber(Progress.InFlight));
}

void FWatabouBatchNotifier::EnsureNotification(const FWatabouRecursiveImporter::FProgress& Progress)
{
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetText(FormatStatusText(Progress));
		return;
	}

	FNotificationInfo Info(FormatStatusText(Progress));
	Info.bFireAndForget = false;
	Info.bUseLargeFont  = false;
	Info.bUseSuccessFailIcons = true;
	Info.bUseThrobber = true;
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("AbortBtn", "Abort"),
		LOCTEXT("AbortBtnTip", "Drop the queue and stop after in-flight imports finish."),
		FSimpleDelegate::CreateLambda([WeakImp = WeakImporter]()
		{
			if (TSharedPtr<FWatabouRecursiveImporter> Imp = WeakImp.Pin()) { Imp->Abort(); }
		}),
		SNotificationItem::CS_Pending));

	NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FWatabouBatchNotifier::DismissNotification(const FWatabouRecursiveImporter::FProgress& Progress)
{
	if (!NotificationItem.IsValid()) { return; }

	NotificationItem->SetText(FormatStatusText(Progress));
	NotificationItem->SetCompletionState(Progress.Failed == 0
		? SNotificationItem::CS_Success
		: SNotificationItem::CS_Fail);
	NotificationItem->SetExpireDuration(4.f);
	NotificationItem->SetFadeOutDuration(1.f);
	NotificationItem->ExpireAndFadeout();
	NotificationItem.Reset();
}

void FWatabouBatchNotifier::HandleProgress(const FWatabouRecursiveImporter::FProgress& Progress)
{
	using EState = FWatabouRecursiveImporter::EBatchState;

	if (Progress.State == EState::Running || Progress.State == EState::Cancelling)
	{
		EnsureNotification(Progress);
		return;
	}
	if (Progress.State == EState::Done)
	{
		DismissNotification(Progress);
		return;
	}

	UE_LOG(LogWatabou, Verbose, TEXT("BatchNotifier: unexpected state %d"), (int32)Progress.State);
}

#undef LOCTEXT_NAMESPACE
