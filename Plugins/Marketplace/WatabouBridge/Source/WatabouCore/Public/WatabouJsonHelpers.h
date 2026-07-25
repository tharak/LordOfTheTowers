// Copyright 2026 Timothé Lapetite

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Small helpers shared by the per-generator JSON parsers.
 *
 * Watabou's generators all emit number fields as JSON numbers (decoded as double by
 * FJsonObject) even when the value is semantically an integer or a 2D point. These
 * helpers handle the read-and-cast / read-pair-of-fields ceremony so individual
 * parsers don't reinvent it per field.
 */
namespace WatabouJsonHelpers
{
	/** Reads two numeric fields into (OutA, OutB). Returns false if either field is missing. */
	template <typename T>
	inline bool ReadPair(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldA, const TCHAR* FieldB, T& OutA, T& OutB)
	{
		if (!Obj.IsValid()) { return false; }
		double A = 0, B = 0;
		if (!Obj->TryGetNumberField(FieldA, A) || !Obj->TryGetNumberField(FieldB, B)) { return false; }
		OutA = static_cast<T>(A);
		OutB = static_cast<T>(B);
		return true;
	}

	/** Reads a numeric field with cast. Returns false if missing. */
	template <typename T>
	inline bool ReadNumber(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, T& Out)
	{
		if (!Obj.IsValid()) { return false; }
		double V = 0;
		if (!Obj->TryGetNumberField(Field, V)) { return false; }
		Out = static_cast<T>(V);
		return true;
	}

	/** Iterates an object-array field, invoking Func(const TSharedPtr<FJsonObject>&) per entry. */
	template <typename Fn>
	inline void ForEachObjectInArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, Fn&& Func)
	{
		if (!Obj.IsValid()) { return; }
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Field, Arr) || !Arr) { return; }
		for (const TSharedPtr<FJsonValue>& Val : *Arr)
		{
			if (!Val.IsValid()) { continue; }
			const TSharedPtr<FJsonObject> Child = Val->AsObject();
			if (!Child.IsValid()) { continue; }
			Func(Child);
		}
	}
}
