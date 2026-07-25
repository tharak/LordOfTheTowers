// Copyright 2026 Timothé Lapetite

#include "WatabouSeedRef.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/SecureHash.h"

FString FWatabouSeedRef::GetCanonicalKey() const
{
	// Deterministic ordering: generator id + seed + sorted tags + sorted params.
	TArray<FString> SortedTags = Tags;
	SortedTags.Sort();

	TArray<TPair<FString, FString>> SortedParams;
	SortedParams.Reserve(Params.Num());
	for (const TPair<FString, FString>& Pair : Params)
	{
		SortedParams.Add(Pair);
	}
	SortedParams.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
	{
		return A.Key < B.Key;
	});

	TStringBuilder<256> Sb;
	Sb << GeneratorId << TEXT("|") << Seed;
	for (const FString& Tag : SortedTags) { Sb << TEXT(":") << Tag; }
	for (const TPair<FString, FString>& Pair : SortedParams)
	{
		Sb << TEXT("|") << Pair.Key << TEXT("=") << Pair.Value;
	}
	return Sb.ToString();
}

FString FWatabouSeedRef::GetCanonicalHash() const
{
	// MD5 over the UTF-8 bytes of the canonical key. Hashing the UTF-8 encoding (not the raw TCHAR
	// buffer) keeps the digest identical regardless of TCHAR width across Windows/macOS/Linux.
	const FString Key = GetCanonicalKey();
	const FTCHARToUTF8 Utf8(*Key);

	FMD5 Md5;
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	uint8 Digest[16];
	Md5.Final(Digest);

	// First 8 bytes -> 16 lowercase hex chars. 64 bits is ample to disambiguate the tag/param
	// variants of a seed; the canonical key remains the authoritative identity.
	TStringBuilder<32> Hex;
	for (int32 i = 0; i < 8; ++i) { Hex.Appendf(TEXT("%02x"), Digest[i]); }
	return FString(Hex.ToString());
}

FString FWatabouSeedRef::BuildQueryString() const
{
	// Values are URL-encoded so the round-trip through ApplyQueryStringToSeedRef (which
	// UrlDecodes each value) is lossless for seeds/params containing spaces, '&', '%', etc.
	// Keys are left raw to stay symmetric with the parser, which does not decode keys.
	// Tags are joined with ',' after per-tag encoding; a literal ',' inside a tag is not
	// representable (',' is the delimiter -- Watabou tags never contain commas).
	TStringBuilder<256> Sb;
	Sb << TEXT("?seed=") << FGenericPlatformHttp::UrlEncode(Seed);

	if (Tags.Num() > 0)
	{
		Sb << TEXT("&tags=");
		for (int32 i = 0; i < Tags.Num(); ++i)
		{
			if (i > 0) { Sb << TEXT(","); }
			Sb << FGenericPlatformHttp::UrlEncode(Tags[i]);
		}
	}

	for (const TPair<FString, FString>& Pair : Params)
	{
		Sb << TEXT("&") << Pair.Key << TEXT("=") << FGenericPlatformHttp::UrlEncode(Pair.Value);
	}

	return Sb.ToString();
}

bool FWatabouSeedRef::operator==(const FWatabouSeedRef& Other) const
{
	// Must stay consistent with GetTypeHash, which hashes GetCanonicalKey() (tags + params
	// sorted). Compare GeneratorId/Seed directly for a cheap short-circuit, then compare tags
	// as an order-independent set and params order-independently -- matching the canonical
	// key's order-insensitive semantics. (A positional Tags compare would let two refs with
	// the same tags in different order hash equal yet compare unequal, breaking TMap/TSet.)
	// ResolvedAsset is intentionally NOT compared: it's a resolution cache, not identity.
	if (GeneratorId != Other.GeneratorId || Seed != Other.Seed) { return false; }
	if (Tags.Num() != Other.Tags.Num()) { return false; }
	if (!Params.OrderIndependentCompareEqual(Other.Params)) { return false; }

	TArray<FString> SortedTags = Tags;
	TArray<FString> OtherSortedTags = Other.Tags;
	SortedTags.Sort();
	OtherSortedTags.Sort();
	return SortedTags == OtherSortedTags;
}
