// Copyright 2026 Timothé Lapetite

#include "DwellingsParser.h"
#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouJsonHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace DwellingsParser_Internal
{
	/**
	 * Read an "edge"-like sub-object: {cell:{i,j}, dir}. Pushes the cell position
	 * into the feature's Coordinates and records (i, j, dir) into the details.
	 * Returns the decoded (I, J) via out-params so callers can accumulate bounds
	 * without round-tripping through the details map.
	 */
	bool ReadEdgeInto(
		const TSharedPtr<FJsonObject>& EdgeObj,
		FWatabouFeature& OutFeature,
		FWatabouFeatureDetails& OutDetails,
		int32& OutI, int32& OutJ)
	{
		if (!EdgeObj.IsValid()) { return false; }

		const TSharedPtr<FJsonObject>* CellObj = nullptr;
		if (!EdgeObj->TryGetObjectField(TEXT("cell"), CellObj) || !CellObj || !CellObj->IsValid()) { return false; }

		if (!WatabouJsonHelpers::ReadPair(*CellObj, TEXT("i"), TEXT("j"), OutI, OutJ)) { return false; }

		OutFeature.Coordinates.Add(FVector2D(static_cast<double>(OutI), static_cast<double>(OutJ)));
		OutDetails.NumericValues.Add(TEXT("i"), OutI);
		OutDetails.NumericValues.Add(TEXT("j"), OutJ);

		FString Dir;
		if (EdgeObj->TryGetStringField(TEXT("dir"), Dir) && !Dir.IsEmpty())
		{
			OutDetails.NameValues.Add(TEXT("dir"), FName(*Dir));
		}

		// Optional endpoint VERTICES (va / vb) emitted by the patched bundle's edge2data. When present, the
		// processor places the opening at their MIDPOINT (the exact wall the bundle drew) instead of the
		// dir-derived half-cell offset -- which is unreliable on upper-floor courtyard / setback edges. Absent
		// (stairs / exit, or an unpatched / pre-endpoint bundle) -> the processor falls back to the dir offset.
		auto ReadVertex = [&EdgeObj, &OutDetails](const TCHAR* Field, const TCHAR* KeyI, const TCHAR* KeyJ)
		{
			const TSharedPtr<FJsonObject>* VObj = nullptr;
			int32 Vi = 0, Vj = 0;
			if (EdgeObj->TryGetObjectField(Field, VObj) && VObj && VObj->IsValid()
				&& WatabouJsonHelpers::ReadPair(*VObj, TEXT("i"), TEXT("j"), Vi, Vj))
			{
				OutDetails.NumericValues.Add(FName(KeyI), Vi);
				OutDetails.NumericValues.Add(FName(KeyJ), Vj);
			}
		};
		ReadVertex(TEXT("va"), TEXT("va_i"), TEXT("va_j"));
		ReadVertex(TEXT("vb"), TEXT("vb_i"), TEXT("vb_j"));
		return true;
	}
}

bool FDwellingsParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	using namespace DwellingsParser_Internal;
	using namespace WatabouJsonHelpers;

	if (!InJson.IsValid() || !OutAsset || !OutAsset->Features)
	{
		OutError = TEXT("null JSON, asset, or asset->Features");
		return false;
	}

	UWatabouFeaturesCollection* Root = OutAsset->Features;

	FBox AccumBounds(ForceInit);

	auto AccumulateCell = [&](int32 I, int32 J)
	{
		AccumBounds += FVector(static_cast<double>(I),       static_cast<double>(J),       0.0);
		AccumBounds += FVector(static_cast<double>(I) + 1.0, static_cast<double>(J) + 1.0, 0.0);
	};

	const TArray<TSharedPtr<FJsonValue>>* FloorsArr = nullptr;
	if (InJson->TryGetArrayField(TEXT("floors"), FloorsArr) && FloorsArr)
	{
		for (const TSharedPtr<FJsonValue>& FloorVal : *FloorsArr)
		{
			if (!FloorVal.IsValid()) { continue; }
			const TSharedPtr<FJsonObject> FloorObj = FloorVal->AsObject();
			if (!FloorObj.IsValid()) { continue; }

			int32 Level = 0;
			ReadNumber(FloorObj, TEXT("level"), Level);

			const FName FloorId(*FString::Printf(TEXT("floor_%d"), Level));
			UWatabouFeaturesCollection* FloorSub = Root->FindOrAddSubCollection(FloorId, OutAsset);
			FloorSub->Details.NumericValues.Add(TEXT("level"), Level);

			// Rooms -> MultiPoint feature (coordinates = cell centers).
			const TArray<TSharedPtr<FJsonValue>>* RoomsArr = nullptr;
			if (FloorObj->TryGetArrayField(TEXT("rooms"), RoomsArr) && RoomsArr)
			{
				const FName RoomsId = TEXT("rooms");
				UWatabouFeaturesCollection* Sub = FloorSub->FindOrAddSubCollection(RoomsId, OutAsset);
				Sub->Elements.Reserve(RoomsArr->Num());

				for (const TSharedPtr<FJsonValue>& RoomVal : *RoomsArr)
				{
					if (!RoomVal.IsValid()) { continue; }
					const TSharedPtr<FJsonObject> RoomObj = RoomVal->AsObject();
					if (!RoomObj.IsValid()) { continue; }

					const int32 Index = Sub->Elements.Num();
					FWatabouFeature& F = Sub->Elements.Emplace_GetRef(EWatabouFeatureType::MultiPoints, RoomsId);

					FString Name;
					if (RoomObj->TryGetStringField(TEXT("name"), Name))
					{
						Sub->ElementsDetails.FindOrAdd(Index).NameValues.Add(TEXT("name"), FName(*Name));
					}

					const TArray<TSharedPtr<FJsonValue>>* CellsArr = nullptr;
					if (RoomObj->TryGetArrayField(TEXT("cells"), CellsArr) && CellsArr)
					{
						F.Coordinates.Reserve(CellsArr->Num());
						for (const TSharedPtr<FJsonValue>& CellVal : *CellsArr)
						{
							if (!CellVal.IsValid()) { continue; }
							int32 I = 0, J = 0;
							if (ReadPair(CellVal->AsObject(), TEXT("i"), TEXT("j"), I, J))
							{
								F.Coordinates.Add(FVector2D(static_cast<double>(I), static_cast<double>(J)));
								AccumulateCell(I, J);
							}
						}
					}

					OutAsset->BumpIdentifier(EWatabouFeatureType::MultiPoints, RoomsId);
				}
			}

			// Doors / Windows / Stairs -> Point features.
			// Doors wrap the cell in an "edge" sub-object and carry a "type"; windows are
			// the bare edge shape; stairs add a "up" bool.
			auto ProcessEdgeArray = [&](const TCHAR* Field, FName SubId, bool bReadDoorType, bool bReadStairUp)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (!FloorObj->TryGetArrayField(Field, Arr) || !Arr) { return; }

				UWatabouFeaturesCollection* Sub = FloorSub->FindOrAddSubCollection(SubId, OutAsset);
				Sub->Elements.Reserve(Arr->Num());

				for (const TSharedPtr<FJsonValue>& Val : *Arr)
				{
					if (!Val.IsValid()) { continue; }
					const TSharedPtr<FJsonObject> Obj = Val->AsObject();
					if (!Obj.IsValid()) { continue; }

					const int32 Index = Sub->Elements.Num();
					FWatabouFeature& F = Sub->Elements.Emplace_GetRef(EWatabouFeatureType::Point, SubId);
					FWatabouFeatureDetails& D = Sub->ElementsDetails.FindOrAdd(Index);

					int32 I = 0, J = 0;
					bool bOk = false;
					const TSharedPtr<FJsonObject>* EdgeObj = nullptr;
					if (Obj->TryGetObjectField(TEXT("edge"), EdgeObj) && EdgeObj && EdgeObj->IsValid())
					{
						bOk = ReadEdgeInto(*EdgeObj, F, D, I, J);
					}
					else
					{
						bOk = ReadEdgeInto(Obj, F, D, I, J);
					}
					if (!bOk) { Sub->Elements.Pop(EAllowShrinking::No); continue; }

					if (bReadDoorType)
					{
						FString Type;
						if (Obj->TryGetStringField(TEXT("type"), Type) && !Type.IsEmpty())
						{
							D.NameValues.Add(TEXT("type"), FName(*Type));
						}
					}
					if (bReadStairUp)
					{
						bool bUp = false;
						if (Obj->TryGetBoolField(TEXT("up"), bUp))
						{
							D.NumericValues.Add(TEXT("up"), bUp ? 1.0 : 0.0);
						}
					}

					AccumulateCell(I, J);
					OutAsset->BumpIdentifier(EWatabouFeatureType::Point, SubId);
				}
			};

			ProcessEdgeArray(TEXT("doors"),   TEXT("doors"),   /*bReadDoorType*/true,  /*bReadStairUp*/false);
			ProcessEdgeArray(TEXT("windows"), TEXT("windows"), /*bReadDoorType*/false, /*bReadStairUp*/false);
			ProcessEdgeArray(TEXT("stairs"),  TEXT("stairs"),  /*bReadDoorType*/false, /*bReadStairUp*/true);
		}
	}

	const TSharedPtr<FJsonObject>* ExitObj = nullptr;
	if (InJson->TryGetObjectField(TEXT("exit"), ExitObj) && ExitObj && ExitObj->IsValid())
	{
		const FName ExitId = TEXT("exit");
		UWatabouFeaturesCollection* Sub = Root->FindOrAddSubCollection(ExitId, OutAsset);
		FWatabouFeature& F = Sub->Elements.Emplace_GetRef(EWatabouFeatureType::Point, ExitId);
		FWatabouFeatureDetails& D = Sub->ElementsDetails.FindOrAdd(0);
		int32 I = 0, J = 0;
		ReadEdgeInto(*ExitObj, F, D, I, J);
		OutAsset->BumpIdentifier(EWatabouFeatureType::Point, ExitId);
	}

	if (AccumBounds.IsValid)
	{
		AccumBounds.Min.Z = -1.0;
		AccumBounds.Max.Z =  1.0;
		OutAsset->Bounds = AccumBounds;
	}

	UWatabouAssetBase::AggregateMetadata(OutAsset);

	UE_LOG(LogWatabou, Log,
		TEXT("FDwellingsParser: %d identifiers, %d discovered keys"),
		OutAsset->Identifiers.Num(), OutAsset->DiscoveredDetailKeys.Num());

	return true;
}
