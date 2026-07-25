// Copyright 2026 Timothé Lapetite

#include "WatabouVersionUtils.h"
#include "Internationalization/Regex.h"

namespace WatabouVersionUtils_Internal
{
	struct FVersionSegment
	{
		int64 Number = 0;
		FString Suffix;   // trailing non-digit tail of the segment ("b" in "7b"), empty for plain numbers
	};

	// Parse "1.12.3b" into numeric segments plus optional trailing suffixes. Returns false
	// (unparseable) when the string is empty or any dot-segment doesn't START with a digit --
	// which rejects the cache's "unknown-<timestamp>" fallback labels by construction.
	inline bool ParseDottedVersion(const FString& In, TArray<FVersionSegment>& Out)
	{
		if (In.IsEmpty()) { return false; }

		TArray<FString> Parts;
		In.ParseIntoArray(Parts, TEXT("."), /*InCullEmpty=*/false);
		if (Parts.Num() == 0) { return false; }

		Out.Reserve(Parts.Num());
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty() || !FChar::IsDigit(Part[0])) { return false; }
			int32 DigitEnd = 1;
			while (DigitEnd < Part.Len() && FChar::IsDigit(Part[DigitEnd])) { ++DigitEnd; }

			FVersionSegment& Seg = Out.AddDefaulted_GetRef();
			Seg.Number = FCString::Atoi64(*Part.Left(DigitEnd));
			Seg.Suffix = Part.Mid(DigitEnd);
		}
		return true;
	}
}

namespace WatabouVersionUtils
{
	FString ExtractBundleVersion(const FString& BundleJsText)
	{
		// meta.h.version="1.8.0" -- the value is always a simple semver / dotted string,
		// double-quoted, immediately after meta.h.version=.
		// Custom raw-string delimiter (RX) because the pattern itself contains `)"` --
		// the default empty delimiter terminates the raw string at the wrong place.
		static const FRegexPattern Pattern(TEXT(R"RX(meta\.h\.version\s*=\s*"([^"]+)")RX"));
		FRegexMatcher Matcher(Pattern, BundleJsText);
		if (Matcher.FindNext())
		{
			return Matcher.GetCaptureGroup(1);
		}
		return FString();
	}

	bool IsStrictlyNewerVersion(const FString& A, const FString& B)
	{
		using namespace WatabouVersionUtils_Internal;

		TArray<FVersionSegment> SegsA, SegsB;
		if (!ParseDottedVersion(A, SegsA) || !ParseDottedVersion(B, SegsB)) { return false; }

		const int32 MaxNum = FMath::Max(SegsA.Num(), SegsB.Num());
		for (int32 Index = 0; Index < MaxNum; ++Index)
		{
			const FVersionSegment SegA = SegsA.IsValidIndex(Index) ? SegsA[Index] : FVersionSegment();
			const FVersionSegment SegB = SegsB.IsValidIndex(Index) ? SegsB[Index] : FVersionSegment();

			if (SegA.Number != SegB.Number) { return SegA.Number > SegB.Number; }

			// Numeric tie: the suffix breaks it ("7b" > "7a" > "7"). Empty sorts lowest.
			const int32 SuffixCmp = SegA.Suffix.Compare(SegB.Suffix, ESearchCase::IgnoreCase);
			if (SuffixCmp != 0) { return SuffixCmp > 0; }
		}
		return false;   // equal
	}
}
