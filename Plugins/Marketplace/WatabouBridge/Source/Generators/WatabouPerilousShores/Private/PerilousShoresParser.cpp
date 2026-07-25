// Copyright 2026 Timothé Lapetite

#include "PerilousShoresParser.h"
#include "WatabouAssetBase.h"
#include "WatabouBridge.h"
#include "WatabouFeaturesCollection.h"
#include "WatabouJsonHelpers.h"
#include "WatabouUrlUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/Box2D.h"
#include "StructUtils/InstancedStruct.h"

namespace PerilousShoresParser_Internal
{
	/** Join an array of strings with '|' for stash-in-details fidelity. */
	FString JoinPipe(const TArray<FString>& In)
	{
		return FString::Join(In, TEXT("|"));
	}

	/** Hex offset-coordinate system from the JSON "layout" field. */
	enum class EHexLayout : uint8 { OddR, EvenR, OddQ, EvenQ };

	EHexLayout ParseLayout(const FString& In)
	{
		if (In.Equals(TEXT("even-r"), ESearchCase::IgnoreCase)) { return EHexLayout::EvenR; }
		if (In.Equals(TEXT("odd-q"),  ESearchCase::IgnoreCase)) { return EHexLayout::OddQ; }
		if (In.Equals(TEXT("even-q"), ESearchCase::IgnoreCase)) { return EHexLayout::EvenQ; }
		// Default + "odd-r": the only layout Perilous Shores 1.8.0 has been observed to emit.
		return EHexLayout::OddR;
	}

	/** Parse a "q<int>_r<int>" hex key (e.g. "q4_r8", "q-2_r10") into its offset coordinates. */
	bool ParseHexKey(const FString& Key, int32& OutQ, int32& OutR)
	{
		int32 Underscore = INDEX_NONE;
		if (!Key.FindChar(TEXT('_'), Underscore)) { return false; }
		const FString QPart = Key.Left(Underscore);    // "q4"
		const FString RPart = Key.Mid(Underscore + 1); // "r8"
		if (QPart.Len() < 2 || RPart.Len() < 2) { return false; }
		if (QPart[0] != TEXT('q') || RPart[0] != TEXT('r')) { return false; }
		OutQ = FCString::Atoi(*QPart.Mid(1));
		OutR = FCString::Atoi(*RPart.Mid(1));
		return true;
	}

	/**
	 * Project an offset hex coordinate to a local 2D centre, unit-spaced (adjacent centres 1 apart).
	 * Pointy-top for the row-offset layouts (odd-r / even-r), flat-top for the column-offset ones (odd-q /
	 * even-q).
	 *
	 * Both components are negated: X for the plugin's universal Watabou->UE flip (see FWatabouFeature), and
	 * the row axis because Perilous Shores' r grows southward (screen Y-down), so north ends up. Negating just
	 * one axis mirrors the map -- the bug the first visual pass surfaced.
	 *
	 * Baked here (not in a stateless ProjectPoint hook) because it depends on the per-map layout, mirroring the
	 * city-family parsers' already-projected coords. The map's draw-rotation (Math.PI/6) is NOT reproduced on
	 * this canonical path (a corner-origin lattice can't apply a centre yaw), so any render-matching orientation
	 * is the caller's (node / seed Transform). The rendered-centre path below sidesteps this: warp AND rotation
	 * are already baked into the emitted x/y.
	 */
	FVector2D HexToLocal(EHexLayout Layout, int32 Q, int32 R)
	{
		constexpr double SQRT3_2 = 0.8660254037844386; // sqrt(3)/2 -- pointy-top row spacing at unit width
		double X = 0.0, Y = 0.0;
		switch (Layout)
		{
		case EHexLayout::OddR:  X = Q + 0.5 * (R & 1); Y = SQRT3_2 * R; break;
		case EHexLayout::EvenR: X = Q - 0.5 * (R & 1); Y = SQRT3_2 * R; break;
		case EHexLayout::OddQ:  X = SQRT3_2 * Q; Y = R + 0.5 * (Q & 1); break;
		case EHexLayout::EvenQ: X = SQRT3_2 * Q; Y = R - 0.5 * (Q & 1); break;
		}
		return FVector2D(-X, -Y);
	}

	/** Resolve a "q<int>_r<int>" hex key directly to its local 2D centre (canonical projection). */
	bool HexKeyToLocal(EHexLayout Layout, const FString& Key, FVector2D& Out)
	{
		int32 Q = 0, R = 0;
		if (!ParseHexKey(Key, Q, R)) { return false; }
		Out = HexToLocal(Layout, Q, R);
		return true;
	}

	/**
	 * Convert a hex's RENDERED centre (the bundle's a.data.center, emitted per hex by the getHex patch -- see
	 * FPerilousShoresInfo) from Watabou screen space to UE local 2D. These are the actual drawn positions with
	 * the per-seed warp + rotation baked in, so they reproduce every hex mode (pointy / flat / warped) exactly,
	 * which the canonical lattice cannot. Same handedness as HexToLocal: both axes negated (X-flip + Y-down).
	 */
	FVector2D RenderedToLocal(double ScreenX, double ScreenY)
	{
		return FVector2D(-ScreenX, -ScreenY);
	}

	/**
	 * Parse a PS town/danger sub-object into the host feature's details.
	 * Mirrors all scalar fields under "<KindKey>_<suffix>" keys. The "type" field
	 * is routed to NameValues as bounded vocabulary; other strings are free text.
	 * If a "link" is present it is decoded into a FWatabouSeedRef and stashed
	 * under Details.StructValues[KindKey].
	 */
	void StashLinkLike(
		const TSharedPtr<FJsonObject>& InObj,
		FWatabouFeatureDetails& Details,
		FName KindKey)
	{
		const FString KindStr = KindKey.ToString();
		for (const auto& Pair : InObj->Values)
		{
			if (!Pair.Value.IsValid()) { continue; }
			const FString PairKey(Pair.Key.ToView());
			const FName DetailKey(*FString::Printf(TEXT("%s_%s"), *KindStr, *PairKey));
			switch (Pair.Value->Type)
			{
			case EJson::String:
				if (PairKey.Equals(TEXT("type"), ESearchCase::IgnoreCase))
				{
					Details.NameValues.Add(DetailKey, FName(*Pair.Value->AsString()));
				}
				else
				{
					Details.StringValues.Add(DetailKey, Pair.Value->AsString());
				}
				break;
			case EJson::Number:  Details.NumericValues.Add(DetailKey, Pair.Value->AsNumber()); break;
			case EJson::Boolean: Details.NumericValues.Add(DetailKey, Pair.Value->AsBool() ? 1.0 : 0.0); break;
			default: break;
			}
		}

		FString Link;
		InObj->TryGetStringField(TEXT("link"), Link);
		if (!Link.IsEmpty())
		{
			FWatabouSeedRef Ref;
			WatabouUrlUtils::ParseWatabouUrl(Link, Ref);
			if (Ref.IsValid())
			{
				Details.StructValues.Add(KindKey, FInstancedStruct::Make(Ref));
			}
		}
	}
}

bool FPerilousShoresParser::Parse(const TSharedPtr<FJsonObject>& InJson, UWatabouAssetBase* OutAsset, FString& OutError)
{
	using namespace PerilousShoresParser_Internal;
	using namespace WatabouJsonHelpers;

	if (!InJson.IsValid() || !OutAsset || !OutAsset->Features)
	{
		OutError = TEXT("null JSON, asset, or asset->Features");
		return false;
	}

	UWatabouFeaturesCollection* Root = OutAsset->Features;

	InJson->TryGetStringField(TEXT("name"), OutAsset->Title);

	FString LayoutStr;
	if (InJson->TryGetStringField(TEXT("layout"), LayoutStr))
	{
		Root->Details.StringValues.Add(TEXT("layout"), LayoutStr);
	}
	const EHexLayout Layout = ParseLayout(LayoutStr);

	// Resolved hex centres -> asset Bounds (set after the loop): the map now lives in local units, not the
	// bp's grid_w/grid_h pixel size.
	FBox2D HexBounds(ForceInit);

	// Hex key -> resolved local centre, so rivers / roads / regions thread the SAME positions the hexes use.
	// bAnyRendered records whether any hex carried a real rendered x/y.
	TMap<FString, FVector2D> HexCenters;
	bool bAnyRendered = false;

	// Hex centres in element-index order, for the post-loop facing pass.
	TArray<FVector2D> HexPositions;

	const TSharedPtr<FJsonObject>* BpObj = nullptr;
	if (InJson->TryGetObjectField(TEXT("bp"), BpObj) && BpObj && BpObj->IsValid())
	{
		double W = 0, H = 0;
		if (ReadPair(*BpObj, TEXT("width"), TEXT("height"), W, H))
		{
			Root->Details.NumericValues.Add(TEXT("grid_w"), W);
			Root->Details.NumericValues.Add(TEXT("grid_h"), H);
		}

		const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
		if ((*BpObj)->TryGetArrayField(TEXT("tags"), TagsArr) && TagsArr)
		{
			for (const TSharedPtr<FJsonValue>& Tag : *TagsArr)
			{
				FString Str;
				if (Tag.IsValid() && Tag->TryGetString(Str)) { OutAsset->GenerationTags.Add(Str); }
			}
		}
	}

	const FName HexesId   = TEXT("hexes");
	const FName RiversId  = TEXT("rivers");
	const FName RoadsId   = TEXT("roads");
	const FName RegionsId = TEXT("named_regions");

	const TSharedPtr<FJsonObject>* HexesObj = nullptr;
	if (InJson->TryGetObjectField(TEXT("hexes"), HexesObj) && HexesObj && HexesObj->IsValid())
	{
		UWatabouFeaturesCollection* HexesSub = Root->FindOrAddSubCollection(HexesId, OutAsset);
		HexesSub->Elements.Reserve((*HexesObj)->Values.Num());
		HexCenters.Reserve((*HexesObj)->Values.Num());
		HexPositions.Reserve((*HexesObj)->Values.Num());

		for (const auto& Entry : (*HexesObj)->Values)
		{
			if (!Entry.Value.IsValid()) { continue; }
			const FString EntryKey(Entry.Key.ToView());
			const TSharedPtr<FJsonObject> HexObj = Entry.Value->AsObject();
			if (!HexObj.IsValid()) { continue; }

			int32 Q = 0, R = 0;
			ReadPair(HexObj, TEXT("q"), TEXT("r"), Q, R);

			const int32 Index = HexesSub->Elements.Num();
			FWatabouFeature& Hex = HexesSub->Elements.Emplace_GetRef(EWatabouFeatureType::Point, HexesId);

			// Prefer the bundle's RENDERED centre (x/y, emitted by the getHex bundle patch): it reproduces the
			// drawn map exactly for every hex mode (warp + rotation baked in). Fall back to the canonical odd-r
			// projection when x/y are absent (un-patched or older export). The native q / r stay in details.
			double Rx = 0.0, Ry = 0.0;
			const bool bRendered = HexObj->TryGetNumberField(TEXT("x"), Rx) && HexObj->TryGetNumberField(TEXT("y"), Ry);
			bAnyRendered |= bRendered;
			const FVector2D HexLocal = bRendered ? RenderedToLocal(Rx, Ry) : HexToLocal(Layout, Q, R);
			Hex.Coordinates.Add(HexLocal);
			HexBounds += HexLocal;
			HexCenters.Add(EntryKey, HexLocal);
			HexPositions.Add(HexLocal);

			FWatabouFeatureDetails& Details = HexesSub->ElementsDetails.FindOrAdd(Index);
			Details.StringValues.Add(TEXT("hex_key"), EntryKey);
			Details.NumericValues.Add(TEXT("q"), Q);
			Details.NumericValues.Add(TEXT("r"), R);

			FString TerrainStr;
			if (HexObj->TryGetStringField(TEXT("terrain"), TerrainStr) && !TerrainStr.IsEmpty())
			{
				Details.NameValues.Add(TEXT("terrain"), FName(*TerrainStr));
			}

			// Open-vocabulary flags: anything that looks like {name?, link?} on the hex
			// becomes a Details.StructValues[key] entry. "town" and "danger" are the two
			// we know about today; future flags get the same treatment without code change.
			static const TCHAR* KnownLinkLikeKeys[] = { TEXT("town"), TEXT("danger") };
			for (const TCHAR* Key : KnownLinkLikeKeys)
			{
				const TSharedPtr<FJsonObject>* SubObj = nullptr;
				if (HexObj->TryGetObjectField(Key, SubObj) && SubObj && SubObj->IsValid())
				{
					StashLinkLike(*SubObj, Details, FName(Key));
				}
			}

			OutAsset->BumpIdentifier(EWatabouFeatureType::Point, HexesId);
		}

		// Facing: derive ONE grid-wide direction and write it to every hex (the grid is uniformly oriented; a
		// per-hex direction makes boundary hexes rotate inconsistently). AVERAGE the topmost-neighbour direction
		// over interior hexes (full 6-ring) -- trusting a single hex threw the grid ~90deg off on a near-Y-tie.
		// Topmost neighbour = highest local Y within 1.5x the nearest-neighbour distance. Written as dir_x/dir_y
		// (WatabouFacing::ReadDirection's vector form), in the centres' own local space; gated by Apply Facing.
		const int32 NumHexes = HexPositions.Num();
		FVector2D DirSum = FVector2D::ZeroVector;       // mean over full-ring (interior) hexes
		int32 InteriorSamples = 0;
		FVector2D FallbackDir = FVector2D::ZeroVector;  // most-surrounded hex, if none reach a full ring
		int32 FallbackCount = -1;
		for (int32 i = 0; i < NumHexes; i++)
		{
			const FVector2D Pi = HexPositions[i];

			double NearestSq = TNumericLimits<double>::Max();
			for (int32 j = 0; j < NumHexes; j++)
			{
				if (j == i) { continue; }
				NearestSq = FMath::Min(NearestSq, FVector2D::DistSquared(Pi, HexPositions[j]));
			}
			if (NearestSq <= 0.0 || NearestSq == TNumericLimits<double>::Max()) { continue; } // isolated hex

			const double Threshold2 = NearestSq * (1.5 * 1.5);
			int32 NeighbourCount = 0;
			int32 TopJ = INDEX_NONE;
			for (int32 j = 0; j < NumHexes; j++)
			{
				if (j == i || FVector2D::DistSquared(Pi, HexPositions[j]) > Threshold2) { continue; }
				++NeighbourCount;
				// Topmost (max local Y); tie-break on X so the choice is deterministic.
				if (TopJ == INDEX_NONE
					|| HexPositions[j].Y > HexPositions[TopJ].Y
					|| (FMath::IsNearlyEqual(HexPositions[j].Y, HexPositions[TopJ].Y) && HexPositions[j].X > HexPositions[TopJ].X))
				{
					TopJ = j;
				}
			}
			if (TopJ == INDEX_NONE) { continue; }

			const FVector2D Dir = (HexPositions[TopJ] - Pi).GetSafeNormal();
			if (Dir.IsNearlyZero()) { continue; }

			if (NeighbourCount >= 6) { DirSum += Dir; ++InteriorSamples; } // a full hex ring -> trustworthy
			if (NeighbourCount > FallbackCount) { FallbackCount = NeighbourCount; FallbackDir = Dir; }
		}

		// Average the interior directions (or the single most-surrounded hex if the map is too small for a ring).
		const FVector2D UniformDir = (InteriorSamples > 0) ? DirSum.GetSafeNormal() : FallbackDir;
		if (!UniformDir.IsNearlyZero())
		{
			// ReadDirection yaws +X to (dir_x, dir_y); writing UniformDir rotated -90deg (dir.y, -dir.x) puts the
			// hex's +Y toward the top neighbour (and +X to its right) -- the orientation the warped hexes want.
			const FVector2D FacingX(UniformDir.Y, -UniformDir.X);
			for (int32 i = 0; i < NumHexes; i++)
			{
				FWatabouFeatureDetails& FacingDetails = HexesSub->ElementsDetails.FindOrAdd(i);
				FacingDetails.NumericValues.Add(TEXT("dir_x"), FacingX.X);
				FacingDetails.NumericValues.Add(TEXT("dir_y"), FacingX.Y);
			}
		}
	}

	// Bounds from the resolved hex extents. Z is a thin slab so the box stays valid for downstream queries.
	if (HexBounds.bIsValid)
	{
		OutAsset->Bounds = FBox(
			FVector(HexBounds.Min.X, HexBounds.Min.Y, -1.0),
			FVector(HexBounds.Max.X, HexBounds.Max.Y,  1.0));
	}

	// Resolve a hex key to the centre recorded during the hex loop. Canonical mode can fall back to projecting
	// the key's own q/r; rendered mode skips an unresolved key instead (the q/r fallback would be wrong-scale).
	// Rivers / roads / regions reference existing hexes, so the lookup normally hits -- the miss path is defensive.
	auto ResolveHexKey = [&HexCenters, bAnyRendered, Layout](const FString& Key, FVector2D& Out) -> bool
	{
		if (const FVector2D* P = HexCenters.Find(Key)) { Out = *P; return true; }
		if (!bAnyRendered) { return HexKeyToLocal(Layout, Key, Out); }
		return false;
	};

	// Append a hex-key list to Feature: raw keys -> OutKeys (kept in details), resolved centres -> coordinates.
	auto AppendKeyList = [&ResolveHexKey](const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FString>& OutKeys, FWatabouFeature& Feature)
	{
		OutKeys.Reserve(Values.Num());
		Feature.Coordinates.Reserve(Values.Num());
		for (const TSharedPtr<FJsonValue>& V : Values)
		{
			FString S;
			if (!V.IsValid() || !V->TryGetString(S)) { continue; }
			OutKeys.Add(S);
			FVector2D P;
			if (ResolveHexKey(S, P)) { Feature.Coordinates.Add(P); }
		}
	};

	// Rivers run along hex borders: each "channel" entry is the edge shared by two adjacent hexes, so the
	// LineString threads those edge midpoints. The raw edge-key chain is kept in details.
	const TSharedPtr<FJsonObject>* RiversObj = nullptr;
	if (InJson->TryGetObjectField(TEXT("rivers"), RiversObj) && RiversObj && RiversObj->IsValid())
	{
		UWatabouFeaturesCollection* RiversSub = Root->FindOrAddSubCollection(RiversId, OutAsset);
		RiversSub->Elements.Reserve((*RiversObj)->Values.Num());

		for (const auto& Entry : (*RiversObj)->Values)
		{
			if (!Entry.Value.IsValid()) { continue; }
			const FString EntryKey(Entry.Key.ToView());
			const TSharedPtr<FJsonObject> RiverObj = Entry.Value->AsObject();
			if (!RiverObj.IsValid()) { continue; }

			const int32 Index = RiversSub->Elements.Num();
			FWatabouFeature& River = RiversSub->Elements.Emplace_GetRef(EWatabouFeatureType::LineString, RiversId);

			FWatabouFeatureDetails& Details = RiversSub->ElementsDetails.FindOrAdd(Index);
			Details.StringValues.Add(TEXT("river_key"), EntryKey);

			FString ParentKey;
			if (RiverObj->TryGetStringField(TEXT("parent"), ParentKey))
			{
				Details.StringValues.Add(TEXT("parent_key"), ParentKey);
			}

			const TArray<TSharedPtr<FJsonValue>>* Channel = nullptr;
			if (RiverObj->TryGetArrayField(TEXT("channel"), Channel) && Channel)
			{
				TArray<FString> Edges;
				Edges.Reserve(Channel->Num());
				River.Coordinates.Reserve(Channel->Num());
				for (const TSharedPtr<FJsonValue>& V : *Channel)
				{
					FString S;
					if (!V.IsValid() || !V->TryGetString(S)) { continue; }
					Edges.Add(S);

					// "qA_rB:qC_rD" is the border shared by two adjacent hexes -> its midpoint is a vertex.
					FString AKey, BKey;
					if (!S.Split(TEXT(":"), &AKey, &BKey)) { continue; }
					FVector2D PA, PB;
					if (ResolveHexKey(AKey, PA) && ResolveHexKey(BKey, PB))
					{
						River.Coordinates.Add((PA + PB) * 0.5);
					}
				}
				Details.StringValues.Add(TEXT("channel"), JoinPipe(Edges));
			}

			OutAsset->BumpIdentifier(EWatabouFeatureType::LineString, RiversId);
		}
	}

	const TSharedPtr<FJsonObject>* RoadsObj = nullptr;
	if (InJson->TryGetObjectField(TEXT("roads"), RoadsObj) && RoadsObj && RoadsObj->IsValid())
	{
		UWatabouFeaturesCollection* RoadsSub = Root->FindOrAddSubCollection(RoadsId, OutAsset);
		RoadsSub->Elements.Reserve((*RoadsObj)->Values.Num());

		for (const auto& Entry : (*RoadsObj)->Values)
		{
			if (!Entry.Value.IsValid()) { continue; }
			const FString EntryKey(Entry.Key.ToView());
			const TArray<TSharedPtr<FJsonValue>>& PathArr = Entry.Value->AsArray();
			if (PathArr.Num() <= 0) { continue; }

			const int32 Index = RoadsSub->Elements.Num();
			FWatabouFeature& Road = RoadsSub->Elements.Emplace_GetRef(EWatabouFeatureType::LineString, RoadsId);

			FWatabouFeatureDetails& Details = RoadsSub->ElementsDetails.FindOrAdd(Index);
			Details.StringValues.Add(TEXT("road_key"), EntryKey);

			// A road is an ordered hex-key list -> a polyline through those hex centres.
			TArray<FString> Path;
			AppendKeyList(PathArr, Path, Road);
			Details.StringValues.Add(TEXT("path"), JoinPipe(Path));

			OutAsset->BumpIdentifier(EWatabouFeatureType::LineString, RoadsId);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* FeaturesArr = nullptr;
	if (InJson->TryGetArrayField(TEXT("features"), FeaturesArr) && FeaturesArr)
	{
		UWatabouFeaturesCollection* RegionsSub = Root->FindOrAddSubCollection(RegionsId, OutAsset);
		RegionsSub->Elements.Reserve(FeaturesArr->Num());

		for (const TSharedPtr<FJsonValue>& FeatVal : *FeaturesArr)
		{
			if (!FeatVal.IsValid()) { continue; }
			const TSharedPtr<FJsonObject> FeatObj = FeatVal->AsObject();
			if (!FeatObj.IsValid()) { continue; }

			const int32 Index = RegionsSub->Elements.Num();
			FWatabouFeature& Region = RegionsSub->Elements.Emplace_GetRef(EWatabouFeatureType::MultiPoints, RegionsId);

			FWatabouFeatureDetails& Details = RegionsSub->ElementsDetails.FindOrAdd(Index);

			FString Name;
			if (FeatObj->TryGetStringField(TEXT("name"), Name)) { Details.StringValues.Add(TEXT("name"), Name); }

			// A named region is a set of member hexes -> a MultiPoints cloud of their centres (all sharing the
			// region's feature key, so Load Watabou Properties tags every point with the region name).
			const TArray<TSharedPtr<FJsonValue>>* HexKeysArr = nullptr;
			if (FeatObj->TryGetArrayField(TEXT("hexes"), HexKeysArr) && HexKeysArr)
			{
				TArray<FString> Keys;
				AppendKeyList(*HexKeysArr, Keys, Region);
				Details.StringValues.Add(TEXT("hex_keys"), JoinPipe(Keys));
			}

			OutAsset->BumpIdentifier(EWatabouFeatureType::MultiPoints, RegionsId);
		}
	}

	UWatabouAssetBase::AggregateMetadata(OutAsset);

	UE_LOG(LogWatabou, Log,
		TEXT("FPerilousShoresParser: %d discovered keys"),
		OutAsset->DiscoveredDetailKeys.Num());

	return true;
}
