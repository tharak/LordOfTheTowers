// Copyright 2026 Timothé Lapetite

#include "OnePageDungeonParser.h"
#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouJsonHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace OnePageDungeonParser_Internal
{
	// Match Dungeon.js, which draws rotundas as a 36-gon (ViewScene round-room corner builder ~line 455).
	constexpr int32 RotundaSegments = 36;

	/** Write the shared room-rect details (x/y/w/h + floor level + optional rotunda/ending flags). */
	void WriteRectDetails(FWatabouFeatureDetails& D, double X, double Y, double W, double H, int32 Level, bool bHasRotunda, bool bRotunda, bool bHasEnding, bool bEnding)
	{
		D.NumericValues.Add(TEXT("x"), X);
		D.NumericValues.Add(TEXT("y"), Y);
		D.NumericValues.Add(TEXT("w"), W);
		D.NumericValues.Add(TEXT("h"), H);
		D.NumericValues.Add(TEXT("level"), static_cast<double>(Level));
		if (bHasRotunda) { D.NumericValues.Add(TEXT("rotunda"), bRotunda ? 1.0 : 0.0); }
		if (bHasEnding)  { D.NumericValues.Add(TEXT("ending"),  bEnding  ? 1.0 : 0.0); }
	}

	/**
	 * Room OUTLINE coordinates (raw native space): square -> four rect corners; rotunda -> a RotundaSegments-gon
	 * circle at the rect center, radius sqrt(w^2+1)/2 (the bundle's contour, just outside the floor tiles).
	 */
	void BuildOutline(FWatabouFeature& F, double X, double Y, double W, double H, bool bRotunda)
	{
		if (bRotunda)
		{
			const FVector2D Center(X + W * 0.5, Y + H * 0.5);
			const double Radius = FMath::Sqrt(W * W + 1.0) * 0.5;
			F.Coordinates.Reserve(RotundaSegments);
			for (int32 S = 0; S < RotundaSegments; ++S)
			{
				const double Ang = 2.0 * PI * static_cast<double>(S) / static_cast<double>(RotundaSegments);
				F.Coordinates.Add(Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Radius);
			}
		}
		else
		{
			F.Coordinates = { FVector2D(X, Y), FVector2D(X + W, Y), FVector2D(X + W, Y + H), FVector2D(X, Y + H) };
		}
	}

	/**
	 * Room TILE-OCCUPANCY coordinates (raw native space): a point at each covered tile center. Square ->
	 * every tile in [x, x+w) x [y, y+h); rotunda -> only tiles whose center is within w/2 of the room center
	 * (the drawCircGrid floor clip, Dungeon.js ~line 450). Rotundas are always square, so the clip is a circle.
	 */
	void BuildTileOccupancy(FWatabouFeature& F, int32 X, int32 Y, int32 W, int32 H, bool bRotunda)
	{
		if (W <= 0 || H <= 0) { return; }

		const FVector2D Center(X + W * 0.5, Y + H * 0.5);
		const double RadiusSq = (W * 0.5) * (W * 0.5);

		F.Coordinates.Reserve(W * H);
		for (int32 Tx = X; Tx < X + W; ++Tx)
		{
			for (int32 Ty = Y; Ty < Y + H; ++Ty)
			{
				const FVector2D TileCenter(Tx + 0.5, Ty + 0.5);
				if (bRotunda && (TileCenter - Center).SizeSquared() > RadiusSq) { continue; }
				F.Coordinates.Add(TileCenter);
			}
		}
	}

	/** Door stair classification from its Watabou type; NAME_None for flat passages (types 0/1/2/4/5/7). */
	FName StairKindFromType(int32 Type)
	{
		switch (Type)
		{
		case 3:  return FName(TEXT("entrance"));   // open exit to the surface (from == null)
		case 8:  return FName(TEXT("down_exit"));  // stairs down at the deepest room (to == null)
		case 9:  return FName(TEXT("step"));       // internal level change (the "deep"/"flat" doors)
		default: return NAME_None;
		}
	}

	/**
	 * Reconstruct a floor level for every rect + a stair kind for every door, HEALED from the room/door
	 * graph. OPD is single-plane ("floor" is our semantic) and the generator emits stair-wise inconsistent
	 * layouts (a stair looping back into its own floor, a flat door where a stair belongs), so we let the
	 * DECLARED STAIRS drive the levels and relabel each door by the gap it actually spans:
	 *
	 *   1. SKELETON: a spanning forest that adds declared stairs (type 9) FIRST, then flat doors. Every
	 *      stair that isn't part of a stair-cycle becomes a real +/-1 step; flat doors only fill same-level
	 *      gaps. (`dir` is unreliable, so it only LOCATES a door's two sides, never the level sign.)
	 *   2. LEVEL: root the forest at the entrance(s) (type 3) = 0 and propagate; a stair edge drops one
	 *      level going outward (outward == down -- magnitude is all stair_kind needs). Orphan components
	 *      (no entrance) anchor at 0.
	 *   3. RELABEL by each door's realized level gap: 1 -> "step" (incl. a flat door PROMOTED to a stair
	 *      because a level genuinely propagates through it); >1 -> "ladder"; 0 -> "flat" for a declared
	 *      stair (a step that turned out level -- pre-classified so PCG needn't compare neighbour levels)
	 *      or plain floor for an ordinary door. So a level NEVER changes without a step/ladder door.
	 *
	 * rects[] (getData) is [rooms/corridors ..., one 1x1 tile per door ...], so RoomCount = NumRects -
	 * NumDoors and door-tile rect (RoomCount + d) pairs with doors[d]. Door tiles aren't graph nodes (they
	 * sit between two rects); each takes the SHALLOWER (higher) of its door's sides + its door's kind.
	 */
	void ComputeFloorPlan(
		const TArray<TSharedPtr<FJsonValue>>* Rects,
		const TArray<TSharedPtr<FJsonValue>>* Doors,
		TArray<int32>& OutRectLevel,
		TArray<FName>& OutRectStairKind,
		TArray<FName>& OutDoorStairKind)
	{
		using namespace WatabouJsonHelpers;

		const int32 NumRects = Rects ? Rects->Num() : 0;
		const int32 NumDoors = Doors ? Doors->Num() : 0;
		OutRectLevel.Init(0, NumRects);
		OutRectStairKind.Init(NAME_None, NumRects);
		OutDoorStairKind.Init(NAME_None, NumDoors);

		const int32 RoomCount = NumRects - NumDoors;
		if (RoomCount <= 0) { return; } // degenerate (no rooms or malformed); leave everything at level 0

		// Cell -> rect map over the room/corridor rects (they precede the door tiles in rects[]).
		TMap<FIntPoint, int32> CellToRect;
		for (int32 i = 0; i < RoomCount; ++i)
		{
			const TSharedPtr<FJsonObject> Obj = (*Rects)[i].IsValid() ? (*Rects)[i]->AsObject() : nullptr;
			if (!Obj.IsValid()) { continue; }
			double X = 0, Y = 0, W = 0, H = 0;
			ReadPair(Obj, TEXT("x"), TEXT("y"), X, Y);
			ReadPair(Obj, TEXT("w"), TEXT("h"), W, H);
			const int32 IX = FMath::RoundToInt(X), IY = FMath::RoundToInt(Y);
			const int32 IW = FMath::RoundToInt(W), IH = FMath::RoundToInt(H);
			for (int32 Cx = IX; Cx < IX + IW; ++Cx)
			{
				for (int32 Cy = IY; Cy < IY + IH; ++Cy)
				{
					CellToRect.FindOrAdd(FIntPoint(Cx, Cy), i); // first rect wins (same-floor rects don't overlap)
				}
			}
		}

		auto RectAt = [&CellToRect](int32 X, int32 Y) -> int32
		{
			const int32* Found = CellToRect.Find(FIntPoint(X, Y));
			return Found ? *Found : INDEX_NONE;
		};

		// Resolve every door to its two straddled rects + raw type; set the base stair kind; seed entrances.
		// `dir` only LOCATES the two sides (x +/- dir), never the level sign (unreliable on bad layouts).
		struct FDoorRec { int32 A = INDEX_NONE; int32 B = INDEX_NONE; int32 Type = 0; };
		TArray<FDoorRec> DoorRecs;
		DoorRecs.SetNum(NumDoors);
		TArray<int32> EntranceRects;

		for (int32 d = 0; d < NumDoors; ++d)
		{
			const TSharedPtr<FJsonObject> Obj = (*Doors)[d].IsValid() ? (*Doors)[d]->AsObject() : nullptr;
			if (!Obj.IsValid()) { continue; }

			double DX = 0, DY = 0, TypeD = 0;
			ReadPair(Obj, TEXT("x"), TEXT("y"), DX, DY);
			ReadNumber(Obj, TEXT("type"), TypeD);
			const int32 X = FMath::RoundToInt(DX), Y = FMath::RoundToInt(DY);
			const int32 Type = FMath::RoundToInt(TypeD);

			OutDoorStairKind[d] = StairKindFromType(Type);
			OutRectStairKind[RoomCount + d] = OutDoorStairKind[d]; // door-tile rect (RoomCount + d) <-> door d

			int32 Ux = 0, Uy = 0;
			const TSharedPtr<FJsonObject>* DirObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("dir"), DirObj) && DirObj && DirObj->IsValid())
			{
				double Dx = 0, Dy = 0;
				ReadPair(*DirObj, TEXT("x"), TEXT("y"), Dx, Dy);
				Ux = FMath::RoundToInt(Dx); Uy = FMath::RoundToInt(Dy);
			}

			const int32 SideA = RectAt(X - Ux, Y - Uy); // one side of the opening
			const int32 SideB = RectAt(X + Ux, Y + Uy); // the other
			DoorRecs[d] = { SideA, SideB, Type };

			if (Type == 3) // surface entrance: the room it opens into seeds level 0
			{
				const int32 Room = (SideA != INDEX_NONE) ? SideA : SideB;
				if (Room != INDEX_NONE) { EntranceRects.AddUnique(Room); }
			}
		}

		// Level skeleton, STAIRS-FIRST: a spanning forest that adds declared stairs (type 9) before flat
		// doors, so every stair that isn't part of a stair-cycle becomes a real +/-1 step and flat doors only
		// fill same-level gaps. An edge that would close a cycle is left out (relabeled later by realized gap).
		TArray<int32> Parent;
		Parent.SetNum(RoomCount);
		for (int32 i = 0; i < RoomCount; ++i) { Parent[i] = i; }
		auto Find = [&Parent](int32 X) -> int32
		{
			while (Parent[X] != X) { Parent[X] = Parent[Parent[X]]; X = Parent[X]; }
			return X;
		};

		TArray<TArray<TPair<int32, bool>>> TreeAdj; // rect -> (neighbour, bIsStair) along the chosen tree
		TreeAdj.SetNum(RoomCount);
		auto TryTreeEdge = [&Parent, &Find, &TreeAdj](int32 A, int32 B, bool bStair)
		{
			if (A == INDEX_NONE || B == INDEX_NONE) { return; }
			const int32 RA = Find(A), RB = Find(B);
			if (RA == RB) { return; } // would close a cycle -> leave as a chord
			Parent[RA] = RB;
			TreeAdj[A].Emplace(B, bStair);
			TreeAdj[B].Emplace(A, bStair);
		};
		for (const FDoorRec& R : DoorRecs) { if (R.Type == 9) { TryTreeEdge(R.A, R.B, true); } }  // stairs first
		for (const FDoorRec& R : DoorRecs) { if (R.Type != 9) { TryTreeEdge(R.A, R.B, false); } } // then flat doors

		// Root the forest at the entrance(s) = 0 and propagate; a stair edge drops one level going outward
		// (outward == down -- magnitude is all stair_kind needs). Components with no entrance anchor at 0.
		TArray<int32> Level;
		Level.Init(MIN_int32, RoomCount);
		TArray<int32> Queue;
		Queue.Reserve(RoomCount);
		int32 Head = 0;
		auto Drain = [&Queue, &Head, &Level, &TreeAdj]()
		{
			for (; Head < Queue.Num(); ++Head)
			{
				const int32 U = Queue[Head];
				for (const TPair<int32, bool>& E : TreeAdj[U])
				{
					if (Level[E.Key] != MIN_int32) { continue; }
					Level[E.Key] = Level[U] - (E.Value ? 1 : 0);
					Queue.Add(E.Key);
				}
			}
		};
		for (const int32 Seed : EntranceRects)
		{
			if (Level[Seed] == MIN_int32) { Level[Seed] = 0; Queue.Add(Seed); }
		}
		Drain();
		for (int32 i = 0; i < RoomCount; ++i) // orphan components (nothing reaches an entrance) anchor at 0
		{
			if (Level[i] == MIN_int32) { Level[i] = 0; Queue.Add(i); Drain(); }
		}

		for (int32 i = 0; i < RoomCount; ++i) { OutRectLevel[i] = (Level[i] == MIN_int32) ? 0 : Level[i]; }

		// Door tiles aren't graph nodes -> take the SHALLOWER (higher) of the door's two sides so the
		// doorway stays on the map through a level change.
		for (int32 d = 0; d < NumDoors; ++d)
		{
			const int32 RectIdx = RoomCount + d;
			if (!OutRectLevel.IsValidIndex(RectIdx)) { continue; }
			const FDoorRec& R = DoorRecs[d];
			if (R.A != INDEX_NONE && R.B != INDEX_NONE)
			{
				OutRectLevel[RectIdx] = FMath::Max(OutRectLevel[R.A], OutRectLevel[R.B]);
			}
			else
			{
				const int32 Home = (R.A != INDEX_NONE) ? R.A : R.B;
				OutRectLevel[RectIdx] = (Home != INDEX_NONE) ? OutRectLevel[Home] : 0;
			}
		}

		// Relabel each door by the level gap the skeleton produced (so a level never changes without a
		// step/ladder door). entrance/down_exit keep their kind; 1 -> "step" (incl. a flat door PROMOTED to
		// a stair), >1 -> "ladder", 0 -> "flat" for a declared stair (a pre-classified bad stair, so the PCG
		// side needn't compare neighbour levels) or plain floor (NAME_None) for an ordinary door.
		for (int32 d = 0; d < NumDoors; ++d)
		{
			const FDoorRec& R = DoorRecs[d];
			if (R.Type == 3 || R.Type == 8) { continue; }             // entrance / down_exit untouched
			if (R.A == INDEX_NONE || R.B == INDEX_NONE) { continue; }  // opening to outside -- keep base kind
			const int32 Diff = FMath::Abs(OutRectLevel[R.A] - OutRectLevel[R.B]);
			FName Kind;
			if (Diff >= 2)      { Kind = FName(TEXT("ladder")); }
			else if (Diff == 1) { Kind = FName(TEXT("step")); }
			else                { Kind = (R.Type == 9) ? FName(TEXT("flat")) : NAME_None; }
			OutDoorStairKind[d] = Kind;
			OutRectStairKind[RoomCount + d] = Kind;
		}
	}
}

bool FOnePageDungeonParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	using namespace OnePageDungeonParser_Internal;
	using namespace WatabouJsonHelpers;

	if (!InJson.IsValid() || !OutAsset || !OutAsset->Features)
	{
		OutError = TEXT("null JSON, asset, or asset->Features");
		return false;
	}

	UWatabouFeaturesCollection* Root = OutAsset->Features;

	InJson->TryGetStringField(TEXT("title"), OutAsset->Title);
	FString Version, Story;
	if (InJson->TryGetStringField(TEXT("version"), Version)) { Root->Details.StringValues.Add(TEXT("version"), Version); }
	if (InJson->TryGetStringField(TEXT("story"),   Story))   { Root->Details.StringValues.Add(TEXT("story"),   Story); }

	FBox AccumBounds(ForceInit);

	const FName RectsId   = TEXT("rects");
	const FName TilesId   = TEXT("tiles");
	const FName DoorsId   = TEXT("doors");
	const FName NotesId   = TEXT("notes");
	const FName ColumnsId = TEXT("columns");
	const FName WaterId   = TEXT("water");

	// Floor levels (per rect) + stair kinds (per door): floodfilled from the surface entrance through
	// step doors. Imposed, since OPD has no native floor concept -- see ComputeFloorPlan.
	const TArray<TSharedPtr<FJsonValue>>* RectsArr = nullptr;
	InJson->TryGetArrayField(TEXT("rects"), RectsArr);
	const TArray<TSharedPtr<FJsonValue>>* DoorsArr = nullptr;
	InJson->TryGetArrayField(TEXT("doors"), DoorsArr);

	TArray<int32> RectLevel;
	TArray<FName> RectStairKind;
	TArray<FName> DoorStairKind;
	ComputeFloorPlan(RectsArr, DoorsArr, RectLevel, RectStairKind, DoorStairKind);

	// rects[] -> a Polygon OUTLINE in "rects" AND a MultiPoints TILE-OCCUPANCY cloud in "tiles", both carrying
	// the same room details (load-by-key works from either). Door corridor tiles are 1x1 rects here too.
	if (RectsArr)
	{
		UWatabouFeaturesCollection* OutlineSub = Root->FindOrAddSubCollection(RectsId, OutAsset);
		UWatabouFeaturesCollection* TilesSub   = Root->FindOrAddSubCollection(TilesId, OutAsset);
		OutlineSub->Elements.Reserve(RectsArr->Num());
		TilesSub->Elements.Reserve(RectsArr->Num());

		int32 RectRaw = -1;
		for (const TSharedPtr<FJsonValue>& Val : *RectsArr)
		{
			++RectRaw; // raw rects[] position (advances on skips too), keying RectLevel / door-tile stair_kind
			if (!Val.IsValid()) { continue; }
			const TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) { continue; }

			double X = 0, Y = 0, W = 0, H = 0;
			ReadPair(Obj, TEXT("x"), TEXT("y"), X, Y);
			ReadPair(Obj, TEXT("w"), TEXT("h"), W, H);

			bool bRotunda = false, bEnding = false;
			const bool bHasRotunda = Obj->TryGetBoolField(TEXT("rotunda"), bRotunda);
			const bool bHasEnding  = Obj->TryGetBoolField(TEXT("ending"),  bEnding);

			const int32 Floor = RectLevel.IsValidIndex(RectRaw) ? RectLevel[RectRaw] : 0;
			// Door tiles carry their door's stair kind (set in ComputeFloorPlan), so the tiles cloud
			// tells stairs from flat floor on its own; rooms leave it unset.
			const FName RectStair = RectStairKind.IsValidIndex(RectRaw) ? RectStairKind[RectRaw] : NAME_None;

			// Outline (Polygon, closed).
			{
				const int32 Index = OutlineSub->Elements.Num();
				FWatabouFeature& F = OutlineSub->Elements.Emplace_GetRef(EWatabouFeatureType::Polygon, RectsId);
				BuildOutline(F, X, Y, W, H, bRotunda);
				FWatabouFeatureDetails& D = OutlineSub->ElementsDetails.FindOrAdd(Index);
				WriteRectDetails(D, X, Y, W, H, Floor, bHasRotunda, bRotunda, bHasEnding, bEnding);
				if (!RectStair.IsNone()) { D.NameValues.Add(TEXT("stair_kind"), RectStair); }
				OutAsset->BumpIdentifier(EWatabouFeatureType::Polygon, RectsId);
			}

			// Tile occupancy (MultiPoints). Round-then-cast the exact-integer rect coords for the grid loop.
			{
				const int32 IX = static_cast<int32>(FMath::RoundToDouble(X));
				const int32 IY = static_cast<int32>(FMath::RoundToDouble(Y));
				const int32 IW = static_cast<int32>(FMath::RoundToDouble(W));
				const int32 IH = static_cast<int32>(FMath::RoundToDouble(H));

				const int32 Index = TilesSub->Elements.Num();
				FWatabouFeature& F = TilesSub->Elements.Emplace_GetRef(EWatabouFeatureType::MultiPoints, TilesId);
				BuildTileOccupancy(F, IX, IY, IW, IH, bRotunda);
				FWatabouFeatureDetails& D = TilesSub->ElementsDetails.FindOrAdd(Index);
				WriteRectDetails(D, X, Y, W, H, Floor, bHasRotunda, bRotunda, bHasEnding, bEnding);
				if (!RectStair.IsNone()) { D.NameValues.Add(TEXT("stair_kind"), RectStair); }
				OutAsset->BumpIdentifier(EWatabouFeatureType::MultiPoints, TilesId);
			}

			AccumBounds += FVector(X, Y, 0.0);
			AccumBounds += FVector(X + W, Y + H, 0.0);
		}
	}

	// doors[] -> Point at the corridor-tile center; the processor's edge offset nudges it onto the wall along
	// dir. dir is kept RAW ({dir_x, dir_y}) -- the processor flips X to match positions.
	if (DoorsArr)
	{
		UWatabouFeaturesCollection* Sub = Root->FindOrAddSubCollection(DoorsId, OutAsset);
		Sub->Elements.Reserve(DoorsArr->Num());

		int32 DoorRaw = -1;
		for (const TSharedPtr<FJsonValue>& Val : *DoorsArr)
		{
			++DoorRaw; // raw doors[] position (advances on skips too), keying DoorStairKind
			if (!Val.IsValid()) { continue; }
			const TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) { continue; }

			double X = 0, Y = 0;
			ReadPair(Obj, TEXT("x"), TEXT("y"), X, Y);

			const int32 Index = Sub->Elements.Num();
			FWatabouFeature& F = Sub->Elements.Emplace_GetRef(EWatabouFeatureType::Point, DoorsId);
			F.Coordinates.Add(FVector2D(X + 0.5, Y + 0.5));

			FWatabouFeatureDetails& D = Sub->ElementsDetails.FindOrAdd(Index);

			const TSharedPtr<FJsonObject>* DirObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("dir"), DirObj) && DirObj && DirObj->IsValid())
			{
				double Dx = 0, Dy = 0;
				ReadPair(*DirObj, TEXT("x"), TEXT("y"), Dx, Dy);
				D.NumericValues.Add(TEXT("dir_x"), Dx);
				D.NumericValues.Add(TEXT("dir_y"), Dy);
			}

			double Type = 0;
			ReadNumber(Obj, TEXT("type"), Type);
			D.NumericValues.Add(TEXT("type"), Type);

			// Stair classification (entrance / down_exit / step); absent on flat passages.
			if (DoorStairKind.IsValidIndex(DoorRaw) && !DoorStairKind[DoorRaw].IsNone())
			{
				D.NameValues.Add(TEXT("stair_kind"), DoorStairKind[DoorRaw]);
			}

			OutAsset->BumpIdentifier(EWatabouFeatureType::Point, DoorsId);
		}
	}

	// notes[] -> Point at pos (already a tile CENTER, float), carrying the room description text + ref number.
	const TArray<TSharedPtr<FJsonValue>>* NotesArr = nullptr;
	if (InJson->TryGetArrayField(TEXT("notes"), NotesArr) && NotesArr)
	{
		UWatabouFeaturesCollection* Sub = Root->FindOrAddSubCollection(NotesId, OutAsset);
		Sub->Elements.Reserve(NotesArr->Num());

		for (const TSharedPtr<FJsonValue>& Val : *NotesArr)
		{
			if (!Val.IsValid()) { continue; }
			const TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) { continue; }

			const int32 Index = Sub->Elements.Num();
			FWatabouFeature& F = Sub->Elements.Emplace_GetRef(EWatabouFeatureType::Point, NotesId);

			const TSharedPtr<FJsonObject>* PosObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("pos"), PosObj) && PosObj && PosObj->IsValid())
			{
				FVector2D Pos;
				ReadPair(*PosObj, TEXT("x"), TEXT("y"), Pos.X, Pos.Y);
				F.Coordinates.Add(Pos);
			}

			FWatabouFeatureDetails& D = Sub->ElementsDetails.FindOrAdd(Index);
			FString Str;
			if (Obj->TryGetStringField(TEXT("text"), Str)) { D.StringValues.Add(TEXT("text"), Str); }
			if (Obj->TryGetStringField(TEXT("ref"),  Str)) { D.NameValues.Add(TEXT("ref"),  FName(*Str)); }

			OutAsset->BumpIdentifier(EWatabouFeatureType::Point, NotesId);
		}
	}

	// columns[] -> raw float position; water[] -> tile center (raw water cells are integer tile corners).
	auto ParsePointArray = [&](const TCHAR* Field, FName Id, double Offset)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!InJson->TryGetArrayField(Field, Arr) || !Arr) { return; }
		UWatabouFeaturesCollection* Sub = Root->FindOrAddSubCollection(Id, OutAsset);
		Sub->Elements.Reserve(Arr->Num());
		for (const TSharedPtr<FJsonValue>& Val : *Arr)
		{
			if (!Val.IsValid()) { continue; }
			FVector2D P;
			if (!ReadPair(Val->AsObject(), TEXT("x"), TEXT("y"), P.X, P.Y)) { continue; }
			FWatabouFeature& F = Sub->Elements.Emplace_GetRef(EWatabouFeatureType::Point, Id);
			F.Coordinates.Add(P + FVector2D(Offset, Offset));
			OutAsset->BumpIdentifier(EWatabouFeatureType::Point, Id);
		}
	};

	ParsePointArray(TEXT("columns"), ColumnsId, 0.0);
	ParsePointArray(TEXT("water"),   WaterId,   0.5);

	if (AccumBounds.IsValid)
	{
		AccumBounds.Min.Z = -1.0;
		AccumBounds.Max.Z =  1.0;
		OutAsset->Bounds = AccumBounds;
	}

	UWatabouAssetBase::AggregateMetadata(OutAsset);

	UE_LOG(LogWatabou, Log,
		TEXT("FOnePageDungeonParser: '%s' -- %d identifiers, %d discovered keys"),
		*OutAsset->Title, OutAsset->Identifiers.Num(), OutAsset->DiscoveredDetailKeys.Num());

	return true;
}
