// Copyright 2026 Timothé Lapetite

#include "WatabouUrlUtils.h"
#include "WatabouBridge.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Dom/JsonObject.h"

namespace WatabouUrlUtils
{
	bool ParseWatabouUrl(const FString& Url, FWatabouSeedRef& OutRef)
	{
		// Look for watabou.github.io/<generator-id>/...
		static const FString Marker = TEXT("watabou.github.io/");
		const int32 MarkerIdx = Url.Find(Marker);
		if (MarkerIdx == INDEX_NONE) { return false; }

		const int32 IdStart = MarkerIdx + Marker.Len();
		// Find next '/' or '?' or end of string
		int32 IdEnd = Url.Len();
		for (int32 i = IdStart; i < Url.Len(); ++i)
		{
			const TCHAR C = Url[i];
			if (C == TEXT('/') || C == TEXT('?')) { IdEnd = i; break; }
		}
		if (IdEnd <= IdStart) { return false; }

		OutRef.GeneratorId = Url.Mid(IdStart, IdEnd - IdStart);
		OutRef.Seed.Empty();
		OutRef.Tags.Reset();
		OutRef.Params.Reset();

		// Extract query string after '?', if any.
		const int32 QIdx = Url.Find(TEXT("?"), ESearchCase::IgnoreCase, ESearchDir::FromStart, IdEnd);
		if (QIdx != INDEX_NONE)
		{
			ApplyQueryStringToSeedRef(Url.RightChop(QIdx + 1), OutRef);
		}
		return true;
	}

	FWatabouSeedRef ParseQueryString(const FString& GeneratorId, const FString& Query)
	{
		FWatabouSeedRef Ref;
		Ref.GeneratorId = GeneratorId;
		FString Q = Query;
		if (Q.StartsWith(TEXT("?"))) { Q.RightChopInline(1); }
		ApplyQueryStringToSeedRef(Q, Ref);
		return Ref;
	}

	void ApplyQueryStringToSeedRef(const FString& Query, FWatabouSeedRef& InOutRef)
	{
		TArray<FString> Pairs;
		Query.ParseIntoArray(Pairs, TEXT("&"), /*InCullEmpty=*/true);
		for (const FString& Pair : Pairs)
		{
			int32 EqIdx;
			if (!Pair.FindChar(TEXT('='), EqIdx)) { continue; }
			const FString Key   = Pair.Left(EqIdx);
			const FString Value = FGenericPlatformHttp::UrlDecode(Pair.RightChop(EqIdx + 1));

			if (Key == TEXT("seed"))
			{
				InOutRef.Seed = Value;
			}
			else if (Key == TEXT("tags"))
			{
				Value.ParseIntoArray(InOutRef.Tags, TEXT(","), /*InCullEmpty=*/true);
			}
			else
			{
				InOutRef.Params.Add(Key, Value);
			}
		}
	}

	FString PeekFriendlyName(const TSharedPtr<FJsonObject>& JsonObj, const FWatabouSeedRef& Ref)
	{
		if (const FString* NameParam = Ref.Params.Find(TEXT("name")))
		{
			if (!NameParam->TrimStartAndEnd().IsEmpty()) { return NameParam->TrimStartAndEnd(); }
		}

		if (JsonObj.IsValid())
		{
			for (const TCHAR* Field : { TEXT("title"), TEXT("name") })
			{
				FString Value;
				if (JsonObj->TryGetStringField(Field, Value) && !Value.TrimStartAndEnd().IsEmpty())
				{
					return Value.TrimStartAndEnd();
				}
			}
		}

		return FString();
	}
}
