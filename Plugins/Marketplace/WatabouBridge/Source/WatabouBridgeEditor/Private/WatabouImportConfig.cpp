// Copyright 2026 Timothé Lapetite

#include "WatabouImportConfig.h"
#include "WatabouBridge.h"      // LogWatabou

#include "Misc/Paths.h"
#include "Misc/PackageName.h"   // FPackageName::IsValidLongPackageName

UWatabouImportConfig* UWatabouImportConfig::Get()
{
	// Config-marked properties on the CDO are loaded from EditorPerProjectUserSettings.ini at
	// class init, so the CDO already carries the last session's values.
	return GetMutableDefault<UWatabouImportConfig>();
}

FString UWatabouImportConfig::GetSavedQuery(const FString& GeneratorId) const
{
	const FString* Found = SavedQueries.Find(GeneratorId);
	return Found ? *Found : FString();
}

void UWatabouImportConfig::SetSavedQuery(const FString& GeneratorId, const FString& Query)
{
	if (const FString* Found = SavedQueries.Find(GeneratorId); Found && *Found == Query)
	{
		return;   // unchanged -- skip the disk write
	}
	SavedQueries.FindOrAdd(GeneratorId) = Query;
	SaveConfig();
}

FString UWatabouImportConfig::GetImportTargetDir() const
{
	return ImportTargetDir.IsEmpty() ? FString(TEXT("/Game/WatabouImports")) : ImportTargetDir;
}

void UWatabouImportConfig::SetImportTargetDir(const FString& Dir)
{
	FString Normalized = Dir;
	Normalized.TrimStartAndEndInline();
	Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
	FPaths::RemoveDuplicateSlashes(Normalized);
	while (Normalized.Len() > 1 && Normalized.EndsWith(TEXT("/")))
	{
		Normalized.LeftChopInline(1);   // strip trailing slash(es) -> no "/Game/Foo//Asset"
	}

	// The Browse picker always yields a valid mounted path, but the editable text box takes free
	// text. Reject anything that isn't a valid long package path (missing leading slash, OS path,
	// unknown mount) so a typo can't persist to config and silently break every top-level import
	// via a failed CreatePackage; fall back to the known-good default instead.
	if (Normalized.IsEmpty() || !FPackageName::IsValidLongPackageName(Normalized))
	{
		if (!Normalized.IsEmpty())
		{
			UE_LOG(LogWatabou, Warning,
				TEXT("Import target '%s' is not a valid content path -- keeping /Game/WatabouImports"),
				*Normalized);
		}
		Normalized = TEXT("/Game/WatabouImports");
	}

	if (ImportTargetDir == Normalized) { return; }   // unchanged -- skip the disk write
	ImportTargetDir = Normalized;
	SaveConfig();
}
