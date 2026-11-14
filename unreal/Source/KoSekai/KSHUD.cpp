#include "KSHUD.h"
#include "KSGameMode.h"
#include "KSTracking.h"
#include "KSAudio.h"
#include "KSUIData.h"
#include "KSCineData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "Fonts/CompositeFont.h"

using namespace KSColors;

namespace
{
	// Every picture the interface uses: /Game/KS/UI/T_UI_<name>, made by art/ui (gen_ui.py, cut_ui.py, import_ui.py).
	const TCHAR* const ArtNames[] = {
		TEXT("title_bg"), TEXT("logo"), TEXT("hpbar"), TEXT("timer"), TEXT("panel"), TEXT("banner"), TEXT("vs"), TEXT("glove"), TEXT("bubble"),
		TEXT("burst_hit"), TEXT("burst_hurt"),
		TEXT("w_counter"), TEXT("w_dodge"), TEXT("w_blocked"), TEXT("w_fake"), TEXT("w_guardbreak"), TEXT("w_pow"), TEXT("w_bam"), TEXT("w_wham"), TEXT("w_krak"),
		TEXT("t_fight"), TEXT("t_ko"), TEXT("t_youwin"), TEXT("t_youlose"), TEXT("t_knockedout"), TEXT("t_time"), TEXT("t_down"), TEXT("t_continue"), TEXT("t_paused"),
		// The fighters' portraits for the select screen: photographs of the game's own models (-KSPortraits, art/ui/make_avatars.py).
		TEXT("av_SergeantMulletV2"), TEXT("av_ChannelZero"), TEXT("av_SirLasercorn"), TEXT("av_AerobicsAnnihilator"), TEXT("av_GrandmaThunder"),
		TEXT("av_Turbotron"), TEXT("av_CountHairspray") };

	// The words the game pops up or announces, and the picture that says each one.
	struct FWordArt { const TCHAR* Word; const TCHAR* Art; };
	const FWordArt WordArt[] = {
		{ TEXT("COUNTER!"), TEXT("w_counter") }, { TEXT("DODGE!"), TEXT("w_dodge") }, { TEXT("BLOCKED"), TEXT("w_blocked") }, { TEXT("FAKE!"), TEXT("w_fake") },
		{ TEXT("GUARD BREAK!"), TEXT("w_guardbreak") }, { TEXT("POW!"), TEXT("w_pow") }, { TEXT("BAM!"), TEXT("w_bam") }, { TEXT("WHAM!"), TEXT("w_wham") },
		{ TEXT("KRAK!"), TEXT("w_krak") },
		{ TEXT("FIGHT!"), TEXT("t_fight") }, { TEXT("K.O.!"), TEXT("t_ko") }, { TEXT("TIME!"), TEXT("t_time") }, { TEXT("DOWN!"), TEXT("t_down") } };
	const TCHAR* ArtFor(const FString& Word)
	{
		for (const FWordArt& W : WordArt) if (Word == W.Word) return W.Art;
		return nullptr;
	}

	/** How big a badge is T seconds after it appeared: it jumps out too big and settles. */
	float PopScale(float T)
	{
		if (T < 0.09f) return FMath::Lerp(0.25f, 1.25f, T / 0.09f);
		if (T < 0.17f) return FMath::Lerp(1.25f, 1.f, (T - 0.09f) / 0.08f);
		return 1.f;
	}
	/** A title slams in from far too big. */
	float SlamScale(float T) { return T < 0.13f ? FMath::Lerp(3.2f, 0.94f, FMath::Square(T / 0.13f)) : T < 0.22f ? FMath::Lerp(0.94f, 1.f, (T - 0.13f) / 0.09f) : 1.f; }
	/** The engine keeps a text item's size to itself; this lets the HUD ask for it. */
	struct FMeasuredText : FCanvasTextItem { using FCanvasTextItem::FCanvasTextItem; using FCanvasTextItem::GetTextSize; };
	float Hash(float Seed) { return FMath::Frac(FMath::Sin(Seed * 12.9898f) * 43758.5453f); }
	FLinearColor Faded(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, C.A * A); }
}

/* ================= drawing tools ================= */

void AKSHUD::LoadArt()
{
	bArtLoaded = true;
	int32 Missing = 0;
	for (const TCHAR* Name : ArtNames)
	{
		UTexture2D* T = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/KS/UI/T_UI_%s.T_UI_%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (T) Textures.Add(FName(Name), T); else Missing++;
	}
	auto MakeFont = [this](const TCHAR* Name) -> UFont*
	{
		// The fonts are plain files beside the pictures (Unreal's font importer cannot run without a window).
		const FString File = FPaths::ProjectContentDir() / TEXT("KS/UI/Fonts") / Name + TEXT(".ttf");
		if (!FPaths::FileExists(File)) return nullptr;
		UFont* Font = NewObject<UFont>(this);
		Font->FontCacheType = EFontCacheType::Runtime;
		FTypefaceEntry Entry(TEXT("Regular"));
		Entry.Font = FFontData(File, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(Entry);
		return Font;
	};
	HudFont = MakeFont(TEXT("F_RussoOne"));
	ComicFont = MakeFont(TEXT("F_Bangers"));
	// One line for tools/check.sh: a missing picture or font silently falls back to the plain look.
	UE_LOG(LogTemp, Display, TEXT("KSTEST ui pictures=%d missing=%d fonts=%d"), Textures.Num(), Missing, (HudFont ? 1 : 0) + (ComicFont ? 1 : 0));
}

UTexture2D* AKSHUD::Tex(const TCHAR* Name) const
{
	const TObjectPtr<UTexture2D>* Found = Textures.Find(FName(Name));
	return Found && *Found && (*Found)->GetResource() ? Found->Get() : nullptr;
}

FVector2D AKSHUD::TextSize(const FString& Str, float Px, EKSFont Which) const
{
	UFont* Font = Which == EKSFont::Comic ? ComicFont : HudFont;
	if (!Font)
	{
		float W = 0.f, H = 0.f;
		const_cast<AKSHUD*>(this)->GetTextSize(Str, W, H, GEngine->GetLargeFont(), Px / 20.f * Unit);
		return FVector2D(W, H);
	}
	const FMeasuredText Item(FVector2D::ZeroVector, FText::FromString(Str), FSlateFontInfo(Font, FMath::Max(5.f, FMath::RoundToFloat(Px * Unit * 0.75f))), FLinearColor::White);
	return Item.GetTextSize(Canvas->Canvas->GetDPIScale());
}

float AKSHUD::Fit(const FString& Str, float Px, EKSFont Font, float MaxW) const
{
	const float Max = MaxW * SW;
	float Wide = TextSize(Str, Px, Font).X;
	if (Wide <= Max) return Px;
	Px = FMath::Max(8.f, FMath::FloorToFloat(Px * Max / Wide));
	// Font sizes are whole pixels, so the first guess can still be a hair too wide.
	while (Px > 8.f && TextSize(Str, Px, Font).X > Max) Px -= 1.f;
	return Px;
}

bool AKSHUD::Lines(const FString& Str, float X, float Y, float MaxW, int32 MaxLines, float Px, const FLinearColor& Color, EKSFont Font, float Edge, const FLinearColor& EdgeColor)
{
	if (Str.IsEmpty()) return true;
	const float Max = MaxW * SW;
	TArray<FString> Wrapped; bool bFits = false;
	for (; Px >= 9.f; Px -= 1.f)
	{
		Wrapped = Wrap(Str, Px, Font, Max);
		bFits = Wrapped.Num() <= MaxLines;
		for (const FString& L : Wrapped) bFits &= TextSize(L, Px, Font).X <= Max;
		if (bFits) break;
	}
	const float Step = Px * 1.22f / 720.f;
	for (int32 i = 0; i < Wrapped.Num(); i++) Text(Wrapped[i], X, Y + (i - (Wrapped.Num() - 1) * 0.5f) * Step, Px, Color, 0, Font, Edge, EdgeColor);
	return bFits;
}

void AKSHUD::Text(const FString& Str, float X, float Y, float Px, const FLinearColor& Color, int32 Align, EKSFont Which, float Edge, const FLinearColor& EdgeColor, float MaxW)
{
	if (Str.IsEmpty() || Color.A <= 0.004f) return;
	if (MaxW > 0.f) Px = Fit(Str, Px, Which, MaxW);
	const FVector2D Size = TextSize(Str, Px, Which);
	const FVector2D At(X * SW - (Align == 0 ? Size.X * 0.5f : Align > 0 ? Size.X : 0.f), Y * SH - Size.Y * 0.5f);
	const float E = Edge * Unit * FMath::Clamp(Px / 22.f, 0.7f, 2.2f);
	UFont* Font = Which == EKSFont::Comic ? ComicFont : HudFont;
	if (!Font)
	{
		UFont* Plain = GEngine->GetLargeFont();
		DrawText(Str, Faded(EdgeColor, Color.A), At.X + E, At.Y + E, Plain, Px / 20.f * Unit);
		DrawText(Str, Color, At.X, At.Y, Plain, Px / 20.f * Unit);
		return;
	}
	FCanvasTextItem Item(At, FText::FromString(Str), FSlateFontInfo(Font, FMath::Max(5.f, FMath::RoundToFloat(Px * Unit * 0.75f))), Color);
	if (Edge > 0.f)
	{
		// The edge: the same words in the dark colour, set a little way off in every direction, and a drop shadow.
		Item.SetColor(Faded(EdgeColor, Color.A));
		const int32 Steps = E > 2.4f ? 12 : 8;
		for (int32 i = 0; i < Steps; i++)
		{
			const float A = 2.f * PI * i / Steps;
			Canvas->DrawItem(Item, At + FVector2D(FMath::Cos(A), FMath::Sin(A)) * E);
		}
		Canvas->DrawItem(Item, At + FVector2D(E * 0.8f, E * 1.9f));
		Item.SetColor(Color);
	}
	Canvas->DrawItem(Item, At);
}

TArray<FString> AKSHUD::Wrap(const FString& Str, float Px, EKSFont Font, float MaxPixels) const
{
	TArray<FString> Words, Lines;
	Str.ParseIntoArray(Words, TEXT(" "), true);
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Longer = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		if (!Line.IsEmpty() && TextSize(Longer, Px, Font).X > MaxPixels) { Lines.Add(Line); Line = Word; }
		else Line = Longer;
	}
	if (!Line.IsEmpty()) Lines.Add(Line);
	return Lines;
}

void AKSHUD::Fill(float X, float Y, float W, float H, const FLinearColor& Color)
{
	DrawRect(Color, X * SW, Y * SH, W * SW, H * SH);
}

void AKSHUD::Part(UTexture2D* Texture, float PX, float PY, float PW, float PH, float U0, float V0, float U1, float V1, const FLinearColor& Color)
{
	if (!Texture || PW <= 0.f || PH <= 0.f) return;
	FCanvasTileItem Tile(FVector2D(PX, PY), Texture->GetResource(), FVector2D(PW, PH), FVector2D(U0, V0), FVector2D(U1, V1), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

bool AKSHUD::Pic(const TCHAR* Name, float X, float Y, float H, float Alpha, bool bFlip, const FLinearColor& Tint)
{
	UTexture2D* T = Tex(Name);
	if (!T) return false;
	const float PH = H * SH, PW = PH * T->GetSizeX() / FMath::Max(1, T->GetSizeY());
	Part(T, X * SW - PW * 0.5f, Y * SH - PH * 0.5f, PW, PH, bFlip ? 1.f : 0.f, 0.f, bFlip ? 0.f : 1.f, 1.f, Faded(Tint, Alpha));
	return true;
}

bool AKSHUD::Frame(const TCHAR* Name, float X, float Y, float W, float H, float CapU, float CapV, float CapPixels, float Alpha, const FLinearColor& Tint)
{
	UTexture2D* T = Tex(Name);
	if (!T) return false;
	const float PX = X * SW, PY = Y * SH, PW = W * SW, PH = H * SH;
	const float CX = FMath::Min(CapPixels * Unit, PW * 0.5f);
	// The corners keep the picture's own proportions.
	const float CY = CapV > 0.f ? FMath::Min(CX * (CapV * T->GetSizeY()) / FMath::Max(1.f, CapU * T->GetSizeX()), PH * 0.5f) : 0.f;
	const float Xs[4] = { PX, PX + CX, PX + PW - CX, PX + PW }, Us[4] = { 0.f, CapU, 1.f - CapU, 1.f };
	const float Ys[4] = { PY, PY + CY, PY + PH - CY, PY + PH }, Vs[4] = { 0.f, CapV, 1.f - CapV, 1.f };
	const FLinearColor Color = Faded(Tint, Alpha);
	for (int32 Row = 0; Row < 3; Row++)
	{
		if (CapV <= 0.f && Row != 1) continue;
		const float Y0 = CapV > 0.f ? Ys[Row] : PY, Y1 = CapV > 0.f ? Ys[Row + 1] : PY + PH, V0 = CapV > 0.f ? Vs[Row] : 0.f, V1 = CapV > 0.f ? Vs[Row + 1] : 1.f;
		for (int32 Col = 0; Col < 3; Col++) Part(T, Xs[Col], Y0, Xs[Col + 1] - Xs[Col], Y1 - Y0, Us[Col], V0, Us[Col + 1], V1, Color);
	}
	return true;
}

void AKSHUD::Panel(float X, float Y, float W, float H, float Alpha)
{
	if (!Frame(TEXT("panel"), X, Y, W, H, 0.10f, 0.19f, 44.f, Alpha)) Fill(X, Y, W, H, FLinearColor(0.03f, 0.01f, 0.07f, 0.88f * Alpha));
}

void AKSHUD::Plate(float X, float Y, float W, float H, float Alpha, const FLinearColor& Tint)
{
	if (!Frame(TEXT("banner"), X, Y, W, H, 0.17f, 0.f, H * SH * 0.64f / Unit, Alpha, Tint)) Fill(X, Y, W, H, FLinearColor(0.f, 0.f, 0.f, 0.7f * Alpha));
}

void AKSHUD::PushSpin(float X, float Y, float Scale, float Degrees)
{
	const FVector P(X * SW, Y * SH, 0.f);
	Canvas->Canvas->PushRelativeTransform(FTranslationMatrix(-P) * FScaleMatrix(FVector(Scale, Scale, 1.f)) * FRotationMatrix(FRotator(0.f, Degrees, 0.f)) * FTranslationMatrix(P));
}

void AKSHUD::PopSpin() { Canvas->Canvas->PopTransform(); }

/* ================= where things go ================= */

namespace
{
	/** How much two boxes overlap, as a fraction of the smaller one. */
	float Cover(const FBox2D& A, const FBox2D& B)
	{
		const float W = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X), H = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
		if (W <= 0.f || H <= 0.f) return 0.f;
		return W * H / FMath::Max(1.f, FMath::Min(A.GetArea(), B.GetArea()));
	}
	FBox2D BoxAt(const FVector2D& Centre, const FVector2D& Size) { return FBox2D(Centre - Size * 0.5f, Centre + Size * 0.5f); }
}

void AKSHUD::BeginLayout(const AKSGameMode& G)
{
	Heads.Reset(); Taken.Reset();
	// Below the health bars and above the bottom edge; on the VS card only the strip the fighters are seen through.
	Region = G.Phase == EKSPhase::Card ? FBox2D(FVector2D(0.01f * SW, 0.16f * SH), FVector2D(0.99f * SW, 0.59f * SH))
		: FBox2D(FVector2D(0.01f * SW, 0.185f * SH), FVector2D(0.99f * SW, 0.93f * SH));
	Cell = FMath::Max(4.f, SH / 48.f);
	GridW = FMath::CeilToInt(SW / Cell); GridH = FMath::CeilToInt(SH / Cell);
	TArray<uint8> On;
	On.SetNumZeroed(GridW * GridH);
	TArray<FVector2D> Points;
	const TPair<const TArray<FVector>*, FVector> Fighters[] = { { &G.OppBody, G.OppHead }, { &G.PlayerBody, G.PlayerHead } };
	for (const TPair<const TArray<FVector>*, FVector>& F : Fighters)
	{
		Points.Reset();
		FBox2D Whole(ForceInit), Seen(ForceInit);
		for (const FVector& Point : *F.Key)
		{
			const FVector S = Project(Point);
			if (S.Z <= 0.f) continue;
			Points.Add(FVector2D(S.X, S.Y));
			Whole += FVector2D(S.X, S.Y);
			Seen += FVector2D(FMath::Clamp(S.X, 0.f, SW), FMath::Clamp(S.Y, 0.f, SH));
		}
		if (!Seen.bIsValid || Seen.GetArea() < 16.f) continue;
		// The points are bones: flesh, hair and gloves reach this much further, by the fighter's size in the picture.
		const float Tall = FMath::Min(Whole.GetSize().Y, 1.5f * SH), Flesh = 0.075f * Tall;
		// The head as one more point, a big one: hair, a horn or a television set reach well behind and above the face.
		const FVector Head = Project(F.Value);
		const int32 HeadAt = Head.Z > 0.f ? Points.Add(FVector2D(Head.X, Head.Y)) : INDEX_NONE;
		for (int32 i = 0; i < Points.Num(); i++)
		{
			const FVector2D P = Points[i];
			const float Round = i == HeadAt ? 0.13f * Tall : Flesh;
			const int32 X0 = FMath::Max(0, FMath::FloorToInt((P.X - Round) / Cell)), X1 = FMath::Min(GridW - 1, FMath::FloorToInt((P.X + Round) / Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt((P.Y - Round) / Cell)), Y1 = FMath::Min(GridH - 1, FMath::FloorToInt((P.Y + Round) / Cell));
			for (int32 Y = Y0; Y <= Y1; Y++) for (int32 X = X0; X <= X1; X++)
				if (FVector2D::DistSquared(P, FVector2D((X + 0.5f) * Cell, (Y + 0.5f) * Cell)) <= Round * Round) On[Y * GridW + X] = 1;
		}
		if (Head.Z > 0.f)
		{
			const float R = FMath::Max(0.05f * SH, 0.12f * Seen.GetSize().Y);
			Heads.Add(BoxAt(FVector2D(Head.X, Head.Y), FVector2D(2.f * R, 2.4f * R)));
		}
	}
	BodySum.SetNumZeroed((GridW + 1) * (GridH + 1));
	for (int32 Y = 0; Y < GridH; Y++) for (int32 X = 0; X < GridW; X++)
		BodySum[(Y + 1) * (GridW + 1) + X + 1] = On[Y * GridW + X] + BodySum[Y * (GridW + 1) + X + 1] + BodySum[(Y + 1) * (GridW + 1) + X] - BodySum[Y * (GridW + 1) + X];
	// A badge keeps its place for as long as it is up, unless a fighter moves under it (DrawPopup); the ones that are gone free theirs.
	Placed.RemoveAll([&G](const FPlacedBadge& B) { return !G.Popups.ContainsByPredicate([&B](const FKSPopup& P) { return P.Start == B.Start && P.Text == B.Text; }); });

	// A new line pops its bubble out afresh.
	if (G.SpokenLine.IsEmpty()) { BubbleLine.Reset(); bBubbleSet = false; }
	else if (G.SpokenLine != BubbleLine)
	{
		BubbleLine = G.SpokenLine; BubbleStart = Now; bBubbleSet = false;
		KSAudio::Play(this, EKSSfx::Pop, 0.6f);
		if (!bLoggedBubble) { bLoggedBubble = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui speech bubble shown picture=%d"), Tex(TEXT("bubble")) ? 1 : 0); }
	}
}

float AKSHUD::BodyCover(const FBox2D& Box) const
{
	if (BodySum.Num() != (GridW + 1) * (GridH + 1)) return 0.f;
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(Box.Min.X / Cell), 0, GridW), X1 = FMath::Clamp(FMath::CeilToInt(Box.Max.X / Cell), 0, GridW);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(Box.Min.Y / Cell), 0, GridH), Y1 = FMath::Clamp(FMath::CeilToInt(Box.Max.Y / Cell), 0, GridH);
	if (X1 <= X0 || Y1 <= Y0) return 0.f;
	const int32 Row = GridW + 1;
	return float(BodySum[Y1 * Row + X1] - BodySum[Y0 * Row + X1] - BodySum[Y1 * Row + X0] + BodySum[Y0 * Row + X0]) / float((X1 - X0) * (Y1 - Y0));
}

void AKSHUD::Reserve(float X, float Y, float W, float H)
{
	Taken.Add(FBox2D(FVector2D(X * SW, Y * SH), FVector2D((X + W) * SW, (Y + H) * SH)));
}

float AKSHUD::SpotCost(const FBox2D& Box, int32 Self) const
{
	// A face is never to be covered, a fixed piece or another badge hardly ever, a body only when there is no room beside it.
	float Cost = 0.f;
	for (const FBox2D& B : Heads) Cost += 20.f * Cover(Box, B);
	for (const FBox2D& B : Taken) Cost += 10.f * Cover(Box, B);
	for (int32 i = 0; i < Placed.Num(); i++) if (i != Self) Cost += 8.f * Cover(Box, BoxAt(Placed[i].At, Placed[i].Size));
	// A body, and a little room round it: a fighter who is hit falls back, and a badge right at his shoulder is under him at once.
	Cost += 5.f * BodyCover(Box) + 2.f * BodyCover(Box.ExpandBy(0.4f * Box.GetSize().Y));
	return Cost;
}

FVector2D AKSHUD::FindSpot(const FVector2D& Want, const FVector2D& Size, int32 Self, float* OutCost) const
{
	// Where the centre may be so that the whole thing stays inside the region.
	FVector2D Lo = Region.Min + Size * 0.5f, Hi = Region.Max - Size * 0.5f;
	if (Hi.X < Lo.X) Lo.X = Hi.X = (Region.Min.X + Region.Max.X) * 0.5f;
	if (Hi.Y < Lo.Y) Lo.Y = Hi.Y = (Region.Min.Y + Region.Max.Y) * 0.5f;
	FVector2D Best = Want; float BestCost = BIG_NUMBER;
	auto Try = [&](FVector2D C)
	{
		C.X = FMath::Clamp(C.X, Lo.X, Hi.X); C.Y = FMath::Clamp(C.Y, Lo.Y, Hi.Y);
		// Staying near where it belongs matters, but less than not covering anyone.
		const float Cost = SpotCost(BoxAt(C, Size), Self) + 1.2f * FVector2D::Distance(C, Want) / SH;
		if (Cost < BestCost) { BestCost = Cost; Best = C; }
	};
	Try(Want);
	const float Step = 0.03f * SH;
	for (float Y = Lo.Y; Y <= Hi.Y + 0.5f; Y += Step) for (float X = Lo.X; X <= Hi.X + 0.5f; X += Step) Try(FVector2D(X, Y));
	if (OutCost) *OutCost = BestCost;
	return Best;
}

void AKSHUD::PlaceBubble(const AKSGameMode& G)
{
	if (G.SpokenLine.IsEmpty()) return;
	UTexture2D* B = Tex(TEXT("bubble"));
	const float H = 0.26f * SH;
	BubbleSize = FVector2D(H * (B ? (float)B->GetSizeX() / B->GetSizeY() : 1.6f), H);
	const bool bOpp = G.bSpokenByOpp;
	const FVector Head = Project(bOpp ? G.OppHead : G.PlayerHead);
	BubbleHead = Head.Z > 0.f ? FVector2D(Head.X, Head.Y) : FVector2D(0.5f * SW, (bOpp ? 0.40f : 0.70f) * SH);
	// Beside the head and a little above it, so the tail points at the mouth: on the right, or on the left, whichever has room.
	// The opponent's bubble would rather be on the right, the player's on the left.
	const FVector2D Right = BubbleHead + FVector2D(0.60f * BubbleSize.X, -0.34f * BubbleSize.Y), Left = BubbleHead + FVector2D(-0.60f * BubbleSize.X, -0.34f * BubbleSize.Y);
	float CostR = 0.f, CostL = 0.f;
	const FVector2D SpotR = FindSpot(Right, BubbleSize, INDEX_NONE, &CostR), SpotL = FindSpot(Left, BubbleSize, INDEX_NONE, &CostL);
	(bOpp ? CostL : CostR) += 0.15f;
	const FVector2D Spot = CostR <= CostL ? SpotR : SpotL;
	if (!bBubbleSet) { bBubbleSet = true; BubbleGoal = BubbleAt = Spot; }
	else
	{
		// It stays put while its place is still fair, and glides over when the fighters have moved under it.
		const float Keep = SpotCost(BoxAt(BubbleGoal, BubbleSize), INDEX_NONE) + 1.2f * FMath::Min(FVector2D::Distance(BubbleGoal, Right), FVector2D::Distance(BubbleGoal, Left)) / SH;
		if (FMath::Min(CostR, CostL) + 0.6f < Keep) BubbleGoal = Spot;
		BubbleAt = FMath::Vector2DInterpTo(BubbleAt, BubbleGoal, GetWorld()->GetDeltaSeconds(), 7.f);
	}
	Taken.Add(BoxAt(BubbleGoal, BubbleSize));
}

/* ================= pieces ================= */

void AKSHUD::HealthBar(float X, float W, float Fraction, float Trail, const FLinearColor& Color, bool bRightToLeft, float Jolt)
{
	UTexture2D* T = Tex(TEXT("hpbar"));
	const float PW = W * SW, PH = T ? PW * T->GetSizeY() / FMath::Max(1, T->GetSizeX()) : 0.03f * SH;
	// A bar that has just lost health jumps about for a moment.
	const float PX = X * SW + FMath::Sin(Now * 90.f) * 7.f * Unit * Jolt, PY = 0.078f * SH - PH * 0.5f + FMath::Cos(Now * 70.f) * 4.f * Unit * Jolt;
	float H0 = T ? KSUI::HpHole[0] : 0.f, H1 = T ? KSUI::HpHole[2] : 1.f;
	if (bRightToLeft) { const float Keep = H0; H0 = 1.f - H1; H1 = 1.f - Keep; }
	const float V0 = T ? KSUI::HpHole[1] : 0.f, V1 = T ? KSUI::HpHole[3] : 1.f;
	// The hollow, taken a little larger so the fill runs under the frame's rim.
	const float Grow = (V1 - V0) * PH * 0.16f;
	const float IX = PX + H0 * PW - Grow, IW = (H1 - H0) * PW + 2.f * Grow, IY = PY + V0 * PH - Grow, IH = (V1 - V0) * PH + 2.f * Grow;
	DrawRect(FLinearColor(0.07f, 0.02f, 0.14f, 0.92f), IX, IY, IW, IH);
	auto Span = [&](float F, float Top, float Height, const FLinearColor& C)
	{
		F = FMath::Clamp(F, 0.f, 1.f);
		DrawRect(C, bRightToLeft ? IX + IW * (1.f - F) : IX, IY + IH * Top, IW * F, IH * Height);
	};
	Span(Trail, 0.f, 1.f, FLinearColor(1.f, 0.95f, 0.75f, 0.9f));
	const bool bLow = Fraction < 0.25f;
	const float Beat = bLow ? 0.75f + 0.25f * FMath::Sin(Now * 14.f) : 1.f;
	const FLinearColor Body = bLow ? Red * Beat : Color;
	Span(Fraction, 0.f, 1.f, FLinearColor(Body.R * 0.55f, Body.G * 0.55f, Body.B * 0.55f, 1.f));    // the shaded lower half
	Span(Fraction, 0.f, 0.62f, FLinearColor(Body.R, Body.G, Body.B, 1.f));
	Span(Fraction, 0.14f, 0.16f, FLinearColor(1.f, 1.f, 1.f, 0.55f));                                // the shine
	// Notches every tenth, like a ruler.
	for (int32 i = 1; i < 10; i++) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), IX + IW * i / 10.f - Unit, IY, 2.f * Unit, IH);
	if (T) Part(T, PX, PY, PW, PH, bRightToLeft ? 1.f : 0.f, 0.f, bRightToLeft ? 0.f : 1.f, 1.f, FLinearColor::White);
}

void AKSHUD::Subtitles(const AKSGameMode& G)
{
	if (!G.Subtitle.IsEmpty())
	{
		Fill(0.f, 0.775f, 1.f, 0.09f, FLinearColor(0.02f, 0.f, 0.06f, 0.62f));
		Fill(0.f, 0.775f, 1.f, 0.004f, Faded(Magenta, 0.8f));
		Fill(0.f, 0.861f, 1.f, 0.004f, Faded(Cyan, 0.8f));
		Lines(G.Subtitle, 0.5f, 0.82f, 0.94f, 2, 24.f, Lilac);
	}
	if (!G.Prompt.IsEmpty())
	{
		const float Pulse = 0.65f + 0.35f * FMath::Sin(Now * 5.f);
		Label(G.Prompt, 0.5f, 0.915f, 1.0f, Faded(Yellow, Pulse), 0, 0.94f);
	}
}

void AKSHUD::DrawIntro(const AKSGameMode& G)
{
	Fill(0.f, 0.f, 1.f, 1.f, FLinearColor::Black);
	const int32 Count = UE_ARRAY_COUNT(KSCineShots);
	if (CineSheets.Num() == 0)
	{
		for (const FKSCineShot& Shot : KSCineShots) CineSheets.Add(LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/KS/Cine/%s.%s"), Shot.Texture, Shot.Texture), nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	// Which shot, and how far into it.
	float T = G.PhaseTime; int32 Index = 0;
	while (Index < Count - 1 && T >= KSCineShots[Index].Frames / KSCineFps) { T -= KSCineShots[Index].Frames / KSCineFps; Index++; }
	const FKSCineShot& Shot = KSCineShots[Index];
	if (UTexture2D* Sheet = CineSheets[Index])
	{
		const int32 Frame = FMath::Clamp(FMath::FloorToInt(T * KSCineFps), 0, FMath::Max(0, Shot.Frames - 1));
		const float U = (Frame % KSCineCols) * KSCineCellU, V = (Frame / KSCineCols) * KSCineCellV, In = 0.5f / 4096.f;
		Part(Sheet, 0.f, 0.f, SW, SH, U + In, V + In, U + KSCineCellU - In, V + KSCineCellV - In, FLinearColor::White);
	}
	// Every cut lands with a white flash, like a splice in the tape.
	if (T < 0.14f) Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(1.f, 1.f, 1.f, 0.85f * (1.f - T / 0.14f)));
	// Cinema bars, the words of the story, and how to get out.
	Fill(0.f, 0.f, 1.f, 0.085f, FLinearColor::Black);
	Fill(0.f, 0.915f, 1.f, 0.085f, FLinearColor::Black);
	const float Rise = FMath::Clamp((T - 0.25f) / 0.2f, 0.f, 1.f);
	Text(Shot.Caption, 0.5f, 0.815f + 0.02f * (1.f - Rise), 40.f, FLinearColor(1.f, 0.9f, 0.3f, Rise), 0, EKSFont::Comic, 3.f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f * Rise), 0.92f);
	// Steady, and worded by whether a camera is plugged in, not by whether it sees a body this frame: the first version
	// blinked on a timer and swapped its words whenever the tracker lost or found the player ("jab, enter, jab, enter").
	const bool bJabSkips = G.Tracking() && G.Tracking()->IsConnected();
	Label(bJabSkips ? TEXT("ENTER  or  JAB LEFT + RIGHT  skip") : TEXT("ENTER  skip"), 0.985f, 0.957f, 0.7f, Lilac, 1);
	// The tape itself: scan lines and a band of noise that wanders up the picture.
	for (float Y = 0.f; Y < SH; Y += 4.f * Unit) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.16f), 0.f, Y, SW, 1.5f * Unit);
	Fill(0.f, 1.f - FMath::Frac(Now * 0.11f), 1.f, 0.014f, FLinearColor(1.f, 1.f, 1.f, 0.06f));
	if (!bLoggedIntro)
	{
		bLoggedIntro = true; int32 Loaded = 0;
		for (UTexture2D* Sheet : CineSheets) Loaded += Sheet ? 1 : 0;
		UE_LOG(LogTemp, Display, TEXT("KSTEST intro shown shots=%d loaded=%d"), Count, Loaded);   // for tools/check.sh
	}
}

void AKSHUD::DrawTracker(const AKSGameMode& G, bool bWithSkeleton)
{
	const UKSTrackingSubsystem* T = G.Tracking();
	if (!T) return;
	FString Status; FLinearColor Color = Muted;
	if (!T->IsConnected()) Status = TEXT("WEBCAM OFF - KEYBOARD");
	else if (!T->IsBodySeen()) { Status = TEXT("WEBCAM: STEP INTO VIEW"); Color = Yellow; }
	else if (!T->IsTracking()) { Status = FString::Printf(TEXT("WEBCAM: HOLD STILL %d%%"), FMath::RoundToInt(FMath::Max(0.f, T->GetCalibrationProgress()) * 100.f)); Color = Yellow; }
	else if (T->AreHandsOutOfView()) { Status = TEXT("WEBCAM: STEP BACK, HANDS OUT OF VIEW"); Color = Yellow; }
	else { Status = TEXT("WEBCAM TRACKING"); Color = Green; }
	Label(Status, 0.985f, 0.975f, 0.7f, Color, 1);
	// Too close to the camera is the commonest reason punches are missed, so say it where it cannot be overlooked.
	// It used to be 64-point letters across the middle of the screen, over the fighters, the callouts and the speech
	// bubbles, and it flashed on for every frame the hands slipped out. Now: a small plate low in the middle, only in
	// the ring (the menu and the calibration screen say it their own way), and only once it has lasted over half a second.
	if (!T->AreHandsOutOfView()) HandsOutSince = -1.f;
	else if (HandsOutSince < 0.f) HandsOutSince = Now;
	const bool bRing = G.Phase != EKSPhase::Menu && G.Phase != EKSPhase::Calibrate && G.Phase != EKSPhase::Intro && G.Phase != EKSPhase::Select;
	if (bRing && HandsOutSince >= 0.f && Now - HandsOutSince > 0.6f)
	{
		const float Pulse = 0.8f + 0.2f * FMath::Sin(Now * 6.f);
		Plate(0.5f - 0.15f, 0.842f, 0.30f, 0.084f, 0.9f);
		Text(TEXT("STEP BACK"), 0.5f, 0.872f, 24.f, FLinearColor(1.f, 0.9f, 0.3f, Pulse), 0, EKSFont::Hud, 2.f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), 0.26f);
		Label(TEXT("the camera must see your elbows and fists"), 0.5f, 0.902f, 0.58f, Lilac, 0, 0.27f);
	}

	if (!bWithSkeleton || !T->HasLandmarks()) return;
	// A small mirror of what the tracker sees, so you can tell it has you.
	const TArray<FVector>& L = T->GetWorldLandmarks();
	const float BX = 0.015f, BY = 0.70f, BW = 0.15f, BH = 0.27f;
	Fill(BX, BY, BW, BH, FLinearColor(0.05f, 0.02f, 0.1f, 0.7f));
	const FVector2D Centre((BX + BW * 0.5f) * SW, (BY + BH * 0.62f) * SH);
	const float Scale = BH * SH * 1.05f;
	auto Pt = [&](int32 i) { return FVector2D(Centre.X - L[i].X * Scale, Centre.Y + L[i].Y * Scale); };
	auto Seg = [&](int32 A, int32 B, const FLinearColor& C) { const FVector2D P = Pt(A), Q = Pt(B); DrawLine(P.X, P.Y, Q.X, Q.Y, C, 3.f * Unit); };
	Seg(11, 12, Lilac); Seg(11, 23, Muted); Seg(12, 24, Muted); Seg(23, 24, Muted);
	Seg(11, 13, Cyan); Seg(13, 15, Cyan); Seg(12, 14, Magenta); Seg(14, 16, Magenta);
	const FVector2D Head = Pt(0);
	DrawRect(Yellow, Head.X - 5.f * Unit, Head.Y - 5.f * Unit, 10.f * Unit, 10.f * Unit);
}

void AKSHUD::DrawPopup(const AKSGameMode& G, const FKSPopup& P)
{
	const float T = G.Clock - P.Start, U = FMath::Clamp(T / P.Life, 0.f, 1.f);
	const float Alpha = U < 0.7f ? 1.f : 1.f - (U - 0.7f) / 0.3f;
	const bool bNumber = P.Kind == 1 || P.Kind == 2;
	const TCHAR* Art = bNumber ? (P.Kind == 1 ? TEXT("burst_hit") : TEXT("burst_hurt")) : ArtFor(P.Text);
	UTexture2D* Picture = Art ? Tex(Art) : nullptr;
	const float Big = P.bBig ? 1.25f : 1.f, High = 0.15f, WordPx = P.bBig ? 60.f : 40.f;

	// Where it goes is decided when it appears: as near to where it belongs as it can be without covering a fighter.
	int32 Slot = Placed.IndexOfByPredicate([&P](const FPlacedBadge& B) { return B.Start == P.Start && B.Text == P.Text; });
	if (Slot == INDEX_NONE)
	{
		const FVector2D Size = Picture ? FVector2D(High * SH * Picture->GetSizeX() / FMath::Max(1, Picture->GetSizeY()), High * SH) * Big
			: TextSize(P.Text, WordPx, EKSFont::Comic) + FVector2D(16.f, 8.f) * Unit;
		FVector2D Want(P.Pos.X * SW, P.Pos.Y * SH);
		if (P.Kind == 1 || P.Kind == 3)
		{
			// Dealt to the opponent: beside the opponent's head; Pos is how far off.
			const FVector Head = Project(G.OppHead);
			Want = Head.Z > 0.f ? FVector2D(Head.X + P.Pos.X * SW, Head.Y + P.Pos.Y * SH) : FVector2D((0.6f + P.Pos.X) * SW, (0.38f + P.Pos.Y) * SH);
		}
		else if (P.Kind == 2)
		{
			// Taken by the player: beside the player's fighter, on the outer side.
			const FVector Head = Project(G.PlayerHead);
			if (Head.Z > 0.f) Want = FVector2D(Head.X + (Head.X < 0.5f * SW ? -0.13f : 0.13f) * SW, Head.Y + (P.Pos.Y - 0.63f) * SH);
		}
		const FVector2D At = FindSpot(Want, Size, INDEX_NONE);
		Slot = Placed.Add({ P.Start, P.Text, At, Size, At });
		// For tools/check.sh: how well the badges keep off the fighters.
		const FBox2D Box = BoxAt(At, Size);
		bool bOnHead = false;
		for (const FBox2D& B : Heads) bOnHead |= Cover(Box, B) > 0.2f;
		const float OnBody = BodyCover(Box);
		WorstCover = FMath::Max(WorstCover, OnBody);
		// A badge that had to go over a body says where it wanted to be, so the reason can be read from the log.
		if (OnBody > 0.35f) UE_LOG(LogTemp, Display, TEXT("KSTEST ui badge on body cover=%.2f kind=%d text=%s phase=%d want=%.0f,%.0f at=%.0f,%.0f size=%.0f,%.0f screen=%.0f,%.0f fixed=%d badges=%d"),
			OnBody, P.Kind, *P.Text, (int32)G.Phase, Want.X, Want.Y, At.X, At.Y, Size.X, Size.Y, SW, SH, Taken.Num(), Placed.Num() - 1);
		BadgesPlaced++; BadgesOnHeads += bOnHead ? 1 : 0;
		// Every ten badges, the state so far: the check reads the last of these lines, so it covers the whole run.
		// worst_body_cover is at the moment of placing; covered_frames is the share of the frames badges were fully
		// shown in which a third or more of one lay on a fighter (he was hit and fell back into it, or stepped there).
		if (BadgesPlaced % 10 == 0)
			UE_LOG(LogTemp, Display, TEXT("KSTEST ui layout badges=%d on_heads=%d worst_body_cover=%.2f covered_frames=%.3f stay=%d"), BadgesPlaced, BadgesOnHeads, WorstCover,
				BadgeFramesCovered / float(FMath::Max(1, BadgeFrames)), FParse::Param(FCommandLine::Get(), TEXT("KSBadgesStay")) ? 1 : 0);
	}
	{
		// The fighters do not stand still: the one who is hit falls back, both step about the ring. A badge that a
		// fighter has moved under slides to the nearest free place (-KSBadgesStay: it stays, as it used to).
		static const bool bStay = FParse::Param(FCommandLine::Get(), TEXT("KSBadgesStay"));
		FPlacedBadge& B = Placed[Slot];
		const float Here = BodyCover(BoxAt(B.Goal, B.Size));
		if (!bStay && Here > 0.08f)
		{
			const FVector2D Spot = FindSpot(B.Goal, B.Size, Slot);
			if (BodyCover(BoxAt(Spot, B.Size)) + 0.1f < Here) B.Goal = Spot;
		}
		B.At = FMath::Vector2DInterpTo(B.At, B.Goal, GetWorld()->GetDeltaSeconds(), 16.f);
		if (U < 0.7f) { BadgeFrames++; BadgeFramesCovered += BodyCover(BoxAt(B.At, B.Size)) >= 0.33f ? 1 : 0; }
	}
	const float X = Placed[Slot].At.X / SW, Y = Placed[Slot].At.Y / SH - 0.025f * U;
	const float Tilt = (Hash(P.Start * 7.3f + P.Pos.X) - 0.5f) * 22.f;
	PushSpin(X, Y, PopScale(T) * Big * (1.f + 0.03f * FMath::Sin(T * 40.f)), Tilt);
	if (bNumber)
	{
		// A damage number on its burst.
		const bool bDealt = P.Kind == 1;
		Pic(Art, X, Y, High, Alpha);
		Text(P.Text, X, Y - 0.003f, 44.f, Faded(bDealt ? FLinearColor(0.95f, 0.08f, 0.05f) : FLinearColor(0.75f, 0.f, 0.3f), Alpha), 0, EKSFont::Comic, 2.4f, FLinearColor(0.f, 0.f, 0.f, 1.f), High * 0.62f * SH / SW);
		if (!bLoggedDamage) { bLoggedDamage = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui damage badge shown text=%s"), *P.Text); }
	}
	else if (Art && Pic(Art, X, Y, High, Alpha)) {}
	else Text(P.Text, X, Y, WordPx / Big, Faded(P.Color, Alpha), 0, EKSFont::Comic, 3.f, FLinearColor::Black);
	PopSpin();
}

void AKSHUD::DrawAnnouncement(const AKSGameMode& G)
{
	// The game only says until when an announcement stays; when it began is noticed here.
	if (G.Clock < G.AnnounceUntil && (G.Announcement != AnnounceText || G.AnnounceUntil != AnnounceEnd))
	{
		AnnounceText = G.Announcement; AnnounceEnd = G.AnnounceUntil; AnnounceStart = G.Clock;
		if (ArtFor(AnnounceText)) KSAudio::Play(this, EKSSfx::Slam, 0.55f);
	}
	if (G.Clock >= G.AnnounceUntil) return;
	const float T = G.Clock - AnnounceStart, Left = G.AnnounceUntil - G.Clock;
	const float Alpha = FMath::Clamp(Left / 0.25f, 0.f, 1.f);
	const float Shiver = T < 0.45f ? (0.45f - T) * 0.02f : 0.f;
	const float X = 0.5f + FMath::Sin(Now * 95.f) * Shiver, Y = 0.40f + FMath::Cos(Now * 80.f) * Shiver;
	PushSpin(X, Y, SlamScale(T) * (1.f + 0.015f * FMath::Sin(Now * 7.f)), -3.f);
	const TCHAR* Art = ArtFor(G.Announcement);
	if (!Art || !Pic(Art, X, Y, 0.30f, Alpha))
	{
		const float W = TextSize(G.Announcement, 72.f, EKSFont::Comic).X / SW + 0.16f;
		Plate(X - W * 0.5f, Y - 0.075f, W, 0.15f, Alpha);
		Text(G.Announcement, X, Y, 72.f, Faded(Yellow, Alpha), 0, EKSFont::Comic, 3.f, FLinearColor::Black);
	}
	PopSpin();
}

void AKSHUD::SpeechBubble(const AKSGameMode& G)
{
	// PlaceBubble has found it a place beside the speaker, clear of both fighters.
	if (G.SpokenLine.IsEmpty() || !bBubbleSet) return;
	const float T = Now - BubbleStart;
	UTexture2D* B = Tex(TEXT("bubble"));
	const float H = BubbleSize.Y / SH, W = BubbleSize.X / SW, X = BubbleAt.X / SW, Y = BubbleAt.Y / SH;
	const bool bOpp = G.bSpokenByOpp;
	const bool bFlip = BubbleAt.X < BubbleHead.X;      // the picture's tail points down and left; turned over when the speaker is on the right

	PushSpin(X, Y, PopScale(T) * (1.f + 0.018f * FMath::Sin(T * 17.f)), (bFlip ? 3.f : -3.f) + 1.6f * FMath::Sin(T * 8.f));
	if (!Pic(TEXT("bubble"), X, Y, H, 1.f, bFlip))
	{
		Fill(X - W * 0.5f, Y - H * 0.3f, W, H * 0.6f, FLinearColor::White);
	}
	// The writing space inside the bubble.
	float I0 = B ? KSUI::BubbleInside[0] : 0.05f, I1 = B ? KSUI::BubbleInside[2] : 0.95f;
	if (bFlip) { const float Keep = I0; I0 = 1.f - I1; I1 = 1.f - Keep; }
	const float V0 = B ? KSUI::BubbleInside[1] : 0.25f, V1 = B ? KSUI::BubbleInside[3] : 0.75f;
	const float CX = X + ((I0 + I1) * 0.5f - 0.5f) * W, CY = Y + ((V0 + V1) * 0.5f - 0.5f) * H;
	// The white of the bubble is a little larger than the measured box, but only in the middle rows.
	const float RoomW = (I1 - I0) * W * SW * 1.08f, RoomH = (V1 - V0) * H * SH * 1.15f;

	const FString Line = G.SpokenLine.ToUpper();
	float Px = 40.f; TArray<FString> Wrapped;
	for (; Px >= 12.f; Px -= 2.f)
	{
		Wrapped = Wrap(Line, Px, EKSFont::Comic, RoomW);
		bool bFits = Wrapped.Num() * Px * Unit * 1.02f <= RoomH;
		for (const FString& L : Wrapped) bFits &= TextSize(L, Px, EKSFont::Comic).X <= RoomW;
		if (bFits) break;
	}
	// The longest word is the one that gets shouted in red.
	TArray<FString> Words; Line.ParseIntoArray(Words, TEXT(" "), true);
	FString Shout;
	for (const FString& Word : Words) if (Word.Len() > Shout.Len()) Shout = Word;
	const float Step = Px * 1.02f / 720.f;
	for (int32 i = 0; i < Wrapped.Num(); i++)
	{
		const float LY = CY + (i - (Wrapped.Num() - 1) * 0.5f) * Step;
		Text(Wrapped[i], CX, LY, Px, FLinearColor(0.04f, 0.02f, 0.06f), 0, EKSFont::Comic, 0.f);
		const int32 Found = Shout.Len() >= 4 ? Wrapped[i].Find(Shout, ESearchCase::CaseSensitive) : INDEX_NONE;
		if (Found != INDEX_NONE)
		{
			const float Whole = TextSize(Wrapped[i], Px, EKSFont::Comic).X, Before = Found > 0 ? TextSize(Wrapped[i].Left(Found), Px, EKSFont::Comic).X : 0.f;
			Text(Shout, CX + (Before - Whole * 0.5f) / SW, LY, Px, FLinearColor(0.9f, 0.03f, 0.08f), -1, EKSFont::Comic, 0.f);
			Shout.Reset();
		}
	}
	// Who is talking, on a name tag at the bubble's top corner.
	const float TagW = FMath::Min(W * 0.8f, TextSize(G.SpokenBy, 16.f, EKSFont::Hud).X / SW + 0.05f), TagX = X + (bFlip ? 0.14f : -0.14f) * W;
	Plate(TagX - TagW * 0.5f, Y - H * 0.47f, TagW, 0.044f, 1.f, bOpp ? FLinearColor::White : FLinearColor(0.45f, 0.9f, 1.f));
	Text(G.SpokenBy, TagX, Y - H * 0.47f + 0.021f, 16.f, FLinearColor::White, 0, EKSFont::Hud, 2.f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), TagW - 0.04f);
	PopSpin();
}

void AKSHUD::SpeechCaption(const AKSGameMode& G)
{
	// On the result and continue screens the middle belongs to the panel: the line goes on a plate along the bottom.
	if (G.SpokenLine.IsEmpty()) return;
	const float Grow = PopScale(Now - BubbleStart);
	const FString Line = G.SpokenLine.ToUpper();
	const float W = FMath::Clamp(TextSize(Line, 24.f, EKSFont::Comic).X / SW + 0.12f, 0.30f, 0.64f);
	PushSpin(0.5f, 0.895f, Grow, 0.f);
	Plate(0.5f - W * 0.5f, 0.855f, W, 0.08f, 0.96f, G.bSpokenByOpp ? FLinearColor::White : FLinearColor(0.45f, 0.9f, 1.f));
	Lines(Line, 0.5f, 0.894f, W - 0.10f, 2, 24.f, FLinearColor::White, EKSFont::Comic, 2.f, FLinearColor::Black);
	const float TagW = TextSize(G.SpokenBy, 15.f, EKSFont::Hud).X / SW + 0.03f;
	Fill(0.5f - TagW * 0.5f, 0.826f, TagW, 0.032f, FLinearColor(0.04f, 0.f, 0.10f, 0.9f));
	Text(G.SpokenBy, 0.5f, 0.842f, 15.f, G.bSpokenByOpp ? Magenta : Cyan);
	PopSpin();
}

/* ================= the screens ================= */

void AKSHUD::Backdrop()
{
	// The background painting, cropped to cover the screen whatever its shape.
	if (UTexture2D* Bg = Tex(TEXT("title_bg")))
	{
		const float Shape = (SW / SH) / ((float)Bg->GetSizeX() / Bg->GetSizeY());
		const float U = Shape < 1.f ? (1.f - Shape) * 0.5f : 0.f, V = Shape > 1.f ? (1.f - 1.f / Shape) * 0.5f : 0.f;
		// It breathes: a very slow zoom.
		const float Zoom = 0.012f + 0.010f * FMath::Sin(Now * 0.35f);
		Part(Bg, 0.f, 0.f, SW, SH, U + Zoom, V + Zoom, 1.f - U - Zoom, 1.f - V - Zoom, FLinearColor::White);
	}
	else Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.04f, 0.f, 0.10f, 1.f));
	Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.03f, 0.f, 0.08f, 0.28f));
}

void AKSHUD::DrawMenu(const AKSGameMode& G)
{
	const float T = G.PhaseTime;
	Backdrop();

	// The logo drops in, then floats.
	const float LogoY = 0.235f + 0.008f * FMath::Sin(Now * 1.7f);
	PushSpin(0.5f, LogoY, SlamScale(T) * (1.f + 0.012f * FMath::Sin(Now * 3.4f)), -2.f);
	if (!Pic(TEXT("logo"), 0.5f, LogoY, 0.36f))
	{
		Text(TEXT("KO-SEKAI"), 0.503f, LogoY + 0.004f, 130.f, Cyan, 0, EKSFont::Comic, 0.f);
		Text(TEXT("KO-SEKAI"), 0.5f, LogoY, 130.f, Magenta, 0, EKSFont::Comic, 4.f);
	}
	PopSpin();
	const float In = FMath::Clamp((T - 0.35f) / 0.4f, 0.f, 1.f);
	Label(TEXT("T H E   G A M E   T H A T   W A S   N E V E R   R E L E A S E D"), 0.5f, 0.435f, 0.95f, Faded(Lilac, In), 0, 0.94f);

	if (G.MenuIndex != SeenMenu) { SeenMenu = G.MenuIndex; MenuAt = Now; }
	static const TCHAR* Items[] = { TEXT("FIGHT"), TEXT("CAMERA CALIBRATION"), TEXT("WATCH THE INTRO"), TEXT("PLAY ONLINE"), TEXT("QUIT") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Items); i++)
	{
		const bool bOn = i == G.MenuIndex;
		// Each plate slides in from the side a moment after the one above it.
		const float Slide = 1.f - FMath::Clamp((T - 0.45f - 0.09f * i) / 0.25f, 0.f, 1.f);
		const float Y = 0.520f + 0.075f * i, X = 0.5f + Slide * Slide * (i % 2 ? 0.7f : -0.7f);   // five lines between the motto and the key help
		const float Bump = bOn ? PopScale(Now - MenuAt) * (1.06f + 0.012f * FMath::Sin(Now * 6.f)) : 0.92f;
		PushSpin(X, Y, Bump, 0.f);
		Plate(X - 0.21f, Y - 0.033f, 0.42f, 0.066f, bOn ? 1.f : 0.8f, bOn ? FLinearColor::White : FLinearColor(0.42f, 0.36f, 0.6f));
		Text(Items[i], X, Y - 0.002f, 26.f, bOn ? FLinearColor(1.f, 0.95f, 0.45f) : Lilac, 0, EKSFont::Hud, 2.2f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), 0.33f);
		if (bOn)
		{
			// The glove punches at the chosen line.
			const float Jab = FMath::Abs(FMath::Sin(Now * 5.f)) * 0.012f;
			if (!Pic(TEXT("glove"), X - 0.255f + Jab, Y, 0.085f)) Text(TEXT(">"), X - 0.24f + Jab, Y, 34.f, Yellow);
		}
		PopSpin();
	}

	const UKSTrackingSubsystem* Tr = G.Tracking();
	const bool bPunches = Tr && Tr->IsTracking();
	Label(bPunches ? TEXT("UP / DOWN  or  SWEEP A HAND UP / DOWN  choose          ENTER  or  JAB LEFT + RIGHT  select")
		: TEXT("UP / DOWN  choose          ENTER  select"), 0.5f, 0.905f, 0.8f, Faded(Lilac, In), 0, 0.94f);
	if (FMath::Frac(Now * 0.8f) < 0.62f) Label(TEXT("INSERT YOURSELF"), 0.5f, 0.955f, 0.95f, Faded(Yellow, In));
	Label(TEXT("(C) 1987   CANCELLED BEFORE LAUNCH"), 0.015f, 0.975f, 0.6f, Faded(Muted, In), -1);

	// Scan lines, as on the tube of an old cabinet.
	for (float Y = 0.f; Y < SH; Y += 4.f * Unit) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.13f), 0.f, Y, SW, 1.5f * Unit);
	if (!bLoggedMenu) { bLoggedMenu = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui menu shown background=%d logo=%d"), Tex(TEXT("title_bg")) ? 1 : 0, Tex(TEXT("logo")) ? 1 : 0); }
	DrawTracker(G, false);
}

void AKSHUD::DrawOnline(const AKSGameMode& G)
{
	const float T = G.PhaseTime;
	Backdrop();
	const float LogoY = 0.175f + 0.006f * FMath::Sin(Now * 1.7f);
	if (!Pic(TEXT("logo"), 0.5f, LogoY, 0.25f)) Text(TEXT("KO-SEKAI"), 0.5f, LogoY, 96.f, Magenta, 0, EKSFont::Comic, 4.f);
	Text(TEXT("PLAY ONLINE"), 0.5f, 0.345f, 46.f, Yellow, 0, EKSFont::Comic, 2.6f, FLinearColor::Black);
	// What went wrong last time (nobody answered, the opponent left) stays up until the next attempt.
	if (!G.LobbyNote.IsEmpty()) Label(G.LobbyNote, 0.5f, 0.415f, 1.0f, FLinearColor(1.f, 0.45f, 0.35f), 0, 0.9f);

	if (G.Lobby == EKSLobby::Choose)
	{
		if (G.LobbyIndex != SeenLobby) { SeenLobby = G.LobbyIndex; MenuAt = Now; }
		static const TCHAR* Items[] = { TEXT("HOST A MATCH"), TEXT("JOIN A MATCH"), TEXT("BACK") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Items); i++)
		{
			const bool bOn = i == G.LobbyIndex;
			const float Slide = 1.f - FMath::Clamp((T - 0.05f - 0.07f * i) / 0.22f, 0.f, 1.f);
			const float Y = 0.515f + 0.100f * i, X = 0.5f + Slide * Slide * (i % 2 ? 0.7f : -0.7f);
			PushSpin(X, Y, bOn ? PopScale(Now - MenuAt) * (1.06f + 0.012f * FMath::Sin(Now * 6.f)) : 0.92f, 0.f);
			Plate(X - 0.21f, Y - 0.041f, 0.42f, 0.082f, bOn ? 1.f : 0.8f, bOn ? FLinearColor::White : FLinearColor(0.42f, 0.36f, 0.6f));
			Text(Items[i], X, Y - 0.002f, 29.f, bOn ? FLinearColor(1.f, 0.95f, 0.45f) : Lilac, 0, EKSFont::Hud, 2.2f);
			if (bOn)
			{
				const float Jab = FMath::Abs(FMath::Sin(Now * 5.f)) * 0.012f;
				if (!Pic(TEXT("glove"), X - 0.255f + Jab, Y, 0.09f)) Text(TEXT(">"), X - 0.24f + Jab, Y, 34.f, Yellow);
			}
			PopSpin();
		}
		Label(TEXT("HOST: a friend joins by your address.     JOIN: type a friend's address, or a relay server's."), 0.5f, 0.845f, 0.78f, Lilac, 0, 0.94f);
		Label(TEXT("UP / DOWN  choose          ENTER  select          ESC  back"), 0.5f, 0.905f, 0.8f, Lilac, 0, 0.94f);
	}
	else if (G.Lobby == EKSLobby::Hosting)
	{
		Panel(0.17f, 0.45f, 0.66f, 0.40f);
		const int32 Dots = (int32)(Now * 2.f) % 4;
		Text(G.LobbyStatus + FString::ChrN(Dots, TEXT('.')), 0.5f, 0.515f, 30.f, FLinearColor(1.f, 0.95f, 0.45f));
		Label(TEXT("Your friend chooses JOIN A MATCH and types this address:"), 0.5f, 0.585f, 0.9f, Lilac, 0, 0.6f);
		FString Addresses;
		for (int32 i = 0; i < G.HostAddresses.Num() && i < 3; i++) Addresses += (i ? TEXT("     or     ") : TEXT("")) + G.HostAddresses[i];
		if (Addresses.IsEmpty()) Addresses = TEXT("(this machine has no network address)");
		if (G.HostPort != 7777) Addresses += FString::Printf(TEXT("   port %d"), G.HostPort);
		Text(Addresses, 0.5f, 0.665f, 40.f, Cyan, 0, EKSFont::Comic, 2.4f, FLinearColor::Black, 0.6f);
		Label(TEXT("That works on the same network or over a VPN. For a friend far away, both of you"), 0.5f, 0.745f, 0.76f, Muted, 0, 0.6f);
		Label(TEXT("JOIN the same relay server instead, or forward this port in your router."), 0.5f, 0.785f, 0.76f, Muted, 0, 0.6f);
		Label(TEXT("ESC  cancel"), 0.5f, 0.905f, 0.8f, Lilac);
	}
	else if (G.Lobby == EKSLobby::Typing)
	{
		Panel(0.17f, 0.45f, 0.66f, 0.36f);
		Label(TEXT("ADDRESS OF THE HOST, OR OF A RELAY SERVER"), 0.5f, 0.515f, 0.95f, Lilac, 0, 0.6f);
		Plate(0.24f, 0.565f, 0.52f, 0.095f);
		const bool bCursor = FMath::Frac(Now * 1.6f) < 0.55f;
		Text(G.JoinAddress + (bCursor ? TEXT("_") : TEXT(" ")), 0.5f, 0.610f, 34.f, FLinearColor(1.f, 0.95f, 0.45f), 0, EKSFont::Hud, 2.f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), 0.44f);
		Label(TEXT("for example   192.168.1.20   or   myserver.net:7777#ourroom"), 0.5f, 0.705f, 0.76f, Muted, 0, 0.6f);
		Label(TEXT("#room on a relay server meets only the friend who types the same word"), 0.5f, 0.745f, 0.76f, Muted, 0, 0.6f);
		Label(TEXT("ENTER  connect          CTRL+V  paste          DEL  clear          ESC  back"), 0.5f, 0.905f, 0.8f, Lilac, 0, 0.94f);
	}
	else
	{
		Panel(0.17f, 0.45f, 0.66f, 0.30f);
		const int32 Dots = (int32)(Now * 2.f) % 4;
		Text(G.LobbyStatus + FString::ChrN(Dots, TEXT('.')), 0.5f, 0.535f, 30.f, FLinearColor(1.f, 0.95f, 0.45f), 0, EKSFont::Hud, 2.f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), 0.6f);
		Text(G.JoinAddress, 0.5f, 0.625f, 34.f, Cyan, 0, EKSFont::Comic, 2.4f, FLinearColor::Black, 0.6f);
		Label(TEXT("ESC  cancel"), 0.5f, 0.905f, 0.8f, Lilac);
	}
	Label(FString::Printf(TEXT("YOU FIGHT AS %s"), G.PlayerFighter().Name), 0.985f, 0.975f, 0.6f, Cyan, 1);

	for (float Y = 0.f; Y < SH; Y += 4.f * Unit) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.13f), 0.f, Y, SW, 1.5f * Unit);
	if (!bLoggedLobby) { bLoggedLobby = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui lobby shown")); }   // for tools/check.sh
}

void AKSHUD::DrawSelect(const AKSGameMode& G)
{
	if (G.bPortraits) return;   // the portrait photographs want a bare picture
	const TArray<FKSFighterDef>& Roster = AKSGameMode::Roster();
	const int32 Count = Roster.Num(), On = FMath::Clamp(G.SelectIndex, 0, Count - 1);
	const FKSFighterDef& F = Roster[On];
	const float T = G.PhaseTime, Since = G.Clock - G.SelectMovedAt;
	const bool bChosen = G.SelectChosenAt >= 0.f;
	const float ChosenFor = bChosen ? G.Clock - G.SelectChosenAt : 0.f;
	const FLinearColor Dark(0.02f, 0.f, 0.06f, 1.f), Edge(0.04f, 0.f, 0.10f, 0.95f);

	// The fighter stands on the right; the left is shaded so the writing reads, with a band above and below.
	for (int32 i = 0; i < 18; i++) Fill(i * 0.032f, 0.f, 0.0325f, 1.f, Faded(Dark, 0.78f * FMath::Pow(1.f - i / 18.f, 1.3f)));
	// (The bands end where KSSelectFrame says the fighter's part of the picture begins; TickSelect frames him into it.)
	Fill(0.f, 0.f, 1.f, KSSelectFrame::Top, Faded(Dark, 0.85f));
	Fill(0.f, KSSelectFrame::Top - 0.002f, 1.f, 0.005f, Magenta);
	Fill(0.f, KSSelectFrame::Bottom, 1.f, 1.f - KSSelectFrame::Bottom, Faded(Dark, 0.85f));
	Fill(0.f, KSSelectFrame::Bottom - 0.002f, 1.f, 0.005f, Cyan);

	PushSpin(0.5f, 0.076f, SlamScale(T), -1.5f);
	Text(TEXT("CHOOSE YOUR FIGHTER"), 0.5f, 0.076f, 62.f, Yellow, 0, EKSFont::Comic, 3.f, FLinearColor::Black, 0.9f);
	PopSpin();

	// Who it is: the name slides in afresh for every fighter looked at.
	const float In = FMath::Clamp(Since / 0.18f, 0.f, 1.f), Slide = (1.f - In) * (1.f - In);
	Label(FString::Printf(TEXT("CUT FIGHTER %d OF %d"), On + 1, Count), 0.27f - Slide * 0.2f, 0.225f, 0.9f, Faded(Cyan, In), 0, 0.44f);
	const FLinearColor NameColor = bChosen && FMath::Frac(ChosenFor * 5.f) < 0.5f ? FLinearColor::White : Yellow;
	PushSpin(0.27f, 0.32f, bChosen ? PopScale(ChosenFor) : 1.f, -2.f);
	Text(F.Name, 0.27f - Slide * 0.25f, 0.32f, 84.f, Faded(NameColor, In), 0, EKSFont::Comic, 3.f, FLinearColor::Black, 0.47f);
	PopSpin();
	Lines(F.Tagline, 0.27f, 0.435f, 0.44f, 2, 22.f, Faded(Lilac, In));
	if (bChosen)
	{
		// He is yours: what he says about it, in his own words.
		if (!G.SpokenLine.IsEmpty()) Lines(FString::Printf(TEXT("\"%s\""), *G.SpokenLine), 0.27f, 0.555f, 0.44f, 3, 34.f, FLinearColor::White, EKSFont::Comic, 2.5f, FLinearColor::Black);
		PushSpin(0.27f, 0.655f, PopScale(ChosenFor), 3.f);
		Text(TEXT("READY!"), 0.27f, 0.655f, 40.f, Green, 0, EKSFont::Comic, 3.f, FLinearColor::Black, 0.3f);
		PopSpin();
	}

	// The seven portraits. The one looked at is bigger, framed in yellow and carries the P1 tag; drawn last, over its neighbours.
	const float Size = 0.145f * SH, Gap = 0.02f * SH, Row = Count * Size + (Count - 1) * Gap, X0 = (SW - Row) * 0.5f, CY = 0.835f * SH;
	int32 Avatars = 0;
	for (int32 Pass = 0; Pass < 2; Pass++)
	{
		for (int32 i = 0; i < Count; i++)
		{
			const bool bOn = i == On;
			if (bOn != (Pass == 1)) continue;
			// Each tile drops in a moment after the one before it.
			const float Drop = 1.f - FMath::Clamp((T - 0.15f - 0.05f * i) / 0.22f, 0.f, 1.f);
			const float Scale = bOn ? 1.17f * PopScale(Since) * (bChosen ? 1.f + 0.04f * FMath::Sin(Now * 14.f) : 1.f) : 1.f;
			const float S = Size * Scale, CX = X0 + i * (Size + Gap) + Size * 0.5f, Y = CY - (bOn ? 0.012f * SH : 0.f) + Drop * Drop * 0.3f * SH;
			const float B = (bOn ? 5.f : 2.5f) * Unit;
			const FLinearColor Rim = bOn ? (bChosen && FMath::Frac(ChosenFor * 5.f) < 0.5f ? FLinearColor::White : Yellow) : Muted;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), CX - S * 0.5f - B + 4.f * Unit, Y - S * 0.5f - B + 5.f * Unit, S + 2.f * B, S + 2.f * B);
			DrawRect(Rim, CX - S * 0.5f - B, Y - S * 0.5f - B, S + 2.f * B, S + 2.f * B);
			const float Dim = bOn ? 1.f : bChosen ? 0.22f : 0.5f;
			UTexture2D* Face = Roster[i].Asset ? Tex(*FString::Printf(TEXT("av_%s"), Roster[i].Asset)) : nullptr;
			if (Face) { Part(Face, CX - S * 0.5f, Y - S * 0.5f, S, S, 0.f, 0.f, 1.f, 1.f, FLinearColor(Dim, Dim, Dim * 1.08f, 1.f)); if (Pass == 0 || bOn) Avatars++; }
			else
			{
				// No portrait in the build: the fighter's colour and initial.
				DrawRect(FLinearColor(Roster[i].Body.R * Dim, Roster[i].Body.G * Dim, Roster[i].Body.B * Dim, 1.f), CX - S * 0.5f, Y - S * 0.5f, S, S);
				Text(FString(Roster[i].Name).Left(1), CX / SW, Y / SH, 44.f * Scale, FLinearColor::White, 0, EKSFont::Comic, 2.f, FLinearColor::Black);
			}
			if (bOn)
			{
				const float Bob = bChosen ? 0.f : FMath::Abs(FMath::Sin(Now * 4.f)) * 0.008f;
				Text(TEXT("P1"), CX / SW, (Y - S * 0.5f) / SH - 0.03f - Bob, 22.f, Cyan, 0, EKSFont::Hud, 2.f, Edge);
			}
		}
	}

	const UKSTrackingSubsystem* Tr = G.Tracking();
	Label(Tr && Tr->IsTracking() ? TEXT("LEFT / RIGHT  or  SWEEP A HAND  choose      ENTER  or  JAB LEFT + RIGHT  select      ESC  menu")
		: TEXT("LEFT / RIGHT  choose          ENTER  select          ESC  menu"), 0.5f, 0.968f, 0.72f, Lilac, 0, 0.52f);
	for (float Y = 0.f; Y < SH; Y += 4.f * Unit) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.10f), 0.f, Y, SW, 1.5f * Unit);
	if (!bLoggedSelect) { bLoggedSelect = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui select shown fighters=%d avatars=%d"), Count, Avatars); }   // for tools/check.sh
	DrawTracker(G, false);
}

void AKSHUD::DrawStreet(const AKSGameMode& G)
{
	if (G.Phase == EKSPhase::Street && G.PhaseTime < 7.f)
	{
		const float A = FMath::Clamp(FMath::Min(G.PhaseTime / 1.2f, (7.f - G.PhaseTime) / 1.5f), 0.f, 1.f);
		if (!Pic(TEXT("logo"), 0.5f, 0.27f, 0.30f, A))
		{
			Text(TEXT("KO-SEKAI"), 0.503f, 0.283f, 120.f, Faded(Cyan, A), 0, EKSFont::Comic, 0.f);
			Text(TEXT("KO-SEKAI"), 0.5f, 0.28f, 120.f, Faded(Magenta, A), 0, EKSFont::Comic, 4.f);
		}
		Label(TEXT("THE GAME THAT WAS NEVER RELEASED"), 0.5f, 0.45f, 1.1f, Faded(Lilac, A));
	}
	Subtitles(G);
	DrawTracker(G, false);
}

void AKSHUD::DrawFight(const AKSGameMode& G)
{
	const FKSFighterDef& F = G.Fighter();
	BeginLayout(G);
	const bool bPanels = G.Phase == EKSPhase::Result || G.Phase == EKSPhase::Continue;

	if (G.Phase == EKSPhase::Card && G.PhaseTime < 2.6f)
	{
		PlaceBubble(G);
		const float T = G.PhaseTime, A = FMath::Clamp(FMath::Min(T / 0.3f, (2.6f - T) / 0.3f), 0.f, 1.f);
		Fill(0.f, 0.f, 1.f, 0.15f, FLinearColor(0.02f, 0.f, 0.06f, 0.85f * A));
		Fill(0.f, 0.146f, 1.f, 0.005f, Faded(Magenta, A));
		Fill(0.f, 0.60f, 1.f, 0.40f, FLinearColor(0.02f, 0.f, 0.06f, 0.85f * A));
		Fill(0.f, 0.598f, 1.f, 0.005f, Faded(Cyan, A));
		Plate(0.33f, 0.035f, 0.34f, 0.085f, A);
		Label(G.bOnline ? FString(TEXT("ONLINE MATCH")) : FString::Printf(TEXT("CUT FIGHTER %d OF %d"), G.FighterIndex + 1, G.NumFights()), 0.5f, 0.076f, 1.15f, Faded(FLinearColor::White, A), 0, 0.25f);
		// The name slides in from the right, the VS badge bangs in on the left.
		const float Slide = 1.f - FMath::Clamp(T / 0.28f, 0.f, 1.f);
		// Everything on the card's lower band stays right of the VS badge.
		Text(F.Name, 0.60f + Slide * Slide * 0.8f, 0.705f, 92.f, Faded(Yellow, A), 0, EKSFont::Comic, 3.f, FLinearColor::Black, 0.72f);
		PushSpin(0.115f, 0.71f, SlamScale(FMath::Max(0.f, T - 0.15f)) * (1.f + 0.03f * FMath::Sin(Now * 9.f)), -8.f);
		if (T > 0.15f) Pic(TEXT("vs"), 0.115f, 0.71f, 0.24f, A);
		PopSpin();
		Label(G.bOnline ? TEXT("Another player. Nobody knows what they will do.") : F.Tagline, 0.60f, 0.805f, 1.15f, Faded(Lilac, A), 0, 0.72f);
		Label(FString::Printf(TEXT("YOU FIGHT AS %s"), G.PlayerFighter().Name), 0.60f, 0.862f, 1.05f, Faded(Cyan, A), 0, 0.72f);
		const bool bTracked = G.Tracking() && G.Tracking()->IsConnected();
		Label(bTracked ? TEXT("PUNCH, BLOCK, LEAN AND DUCK FOR REAL - THE CABINET IS WATCHING")
			: TEXT("A/Q/Z left straight/hook/uppercut   D/E/C right   SPACE block   ARROWS evade + duck   P pause   ESC menu"), 0.5f, 0.94f, 0.78f, Faded(Muted, A), 0, 0.80f);
		DrawTracker(G, true);
		return;
	}

	// After an online bout: who has asked for another, in place of the cabinet's own next step.
	const FString Rematch = G.bOnlineReady ? (G.bOnlineOppReady ? TEXT("HERE WE GO AGAIN") : TEXT("WAITING FOR THE OTHER PLAYER"))
		: (G.bOnlineOppReady ? TEXT("THE OTHER PLAYER WANTS A REMATCH") : TEXT("REMATCH?"));
	const TCHAR* const RematchKeys = TEXT("ENTER or jab left + right: rematch          ESC: leave");
	if (G.bOnline) Label(G.OnlinePingMs >= 0.f ? FString::Printf(TEXT("ONLINE   %d ms"), FMath::RoundToInt(G.OnlinePingMs)) : FString(TEXT("ONLINE")), 0.015f, 0.975f, 0.6f, Muted, -1);

	/* health, names, clock */
	if (SeenPlayerHP >= 0.f && G.PlayerHP < SeenPlayerHP - 0.01f) PlayerJoltAt = Now;
	if (SeenOppHP >= 0.f && G.OppHP < SeenOppHP - 0.01f) OppJoltAt = Now;
	SeenPlayerHP = G.PlayerHP; SeenOppHP = G.OppHP;
	auto Jolt = [this](float At) { return FMath::Clamp(1.f - (Now - At) / 0.28f, 0.f, 1.f); };
	// The result and continue screens are panels of their own: the bars would only sit under their titles.
	if (!bPanels)
	{
		const FLinearColor Edge(0.04f, 0.f, 0.10f, 0.95f);
		HealthBar(0.025f, 0.405f, G.PlayerHP / 100.f, G.PlayerTrail / 100.f, Cyan, false, Jolt(PlayerJoltAt));
		HealthBar(0.57f, 0.405f, G.OppHP / G.OppMaxHP, G.OppTrail / G.OppMaxHP, Magenta, true, Jolt(OppJoltAt));
		Text(FString::Printf(TEXT("YOU  -  %s"), G.PlayerFighter().Name), 0.07f, 0.150f, 19.f, Cyan, -1, EKSFont::Hud, 2.f, Edge, 0.36f);
		Text(G.bOnline ? FString::Printf(TEXT("%s  -  RIVAL"), F.Name) : FString(F.Name), 0.93f, 0.150f, 19.f, Magenta, 1, EKSFont::Hud, 2.f, Edge, 0.36f);
		// Special abilities are not played online yet (each game judges only its own health), so no meters there.
		if (!G.bOnline)
		{
			// The special meters: a thin bar under each name. A full one flashes, and the player is told what to shout.
			auto Meter = [this](float X, float W, float V, const FLinearColor& Color, bool bFromRight)
			{
				V = FMath::Clamp(V, 0.f, 1.f);
				Fill(X - 0.002f, 0.1765f, W + 0.004f, 0.014f, FLinearColor(0.02f, 0.f, 0.06f, 0.85f));
				const bool bFull = V >= 1.f;
				Fill(bFromRight ? X + W * (1.f - V) : X, 0.179f, W * V, 0.009f, bFull ? (FMath::Frac(Now * 4.f) < 0.5f ? FLinearColor::White : Yellow) : Color);
			};
			Meter(0.07f, 0.24f, G.PlayerMeter, Cyan, false);
			Meter(0.69f, 0.24f, G.OppMeter, Magenta, true);
			if (G.Phase == EKSPhase::Fight && G.PlayerMeter >= 1.f && !G.bSpecialOn)
			{
				Reserve(0.05f, 0.19f, 0.42f, 0.085f);
				const float Beat = 1.f + 0.05f * FMath::Abs(FMath::Sin(Now * 6.f));
				PushSpin(0.07f, 0.225f, Beat, 0.f);
				Text(FString::Printf(TEXT("%s  %s"), G.bMicOpen ? TEXT("SHOUT") : TEXT("PRESS S"), *G.PlayerCry()), 0.07f, 0.225f, 30.f, Yellow, -1, EKSFont::Comic, 2.5f, FLinearColor::Black, 0.40f);
				PopSpin();
				if (G.bMicOpen)
				{
					// How loud the microphone hears the room, so a player whose shout does nothing can see why.
					Label(TEXT("MIC"), 0.07f, 0.262f, 0.55f, Lilac, -1);
					Fill(0.105f, 0.257f, 0.12f, 0.009f, FLinearColor(0.02f, 0.f, 0.06f, 0.85f));
					Fill(0.105f, 0.257f, 0.12f * G.MicLevel, 0.009f, Green);
					Label(TEXT("or press S"), 0.235f, 0.262f, 0.55f, Muted, -1);
				}
			}
			else Label(TEXT("SPECIAL"), 0.07f, 0.2f, 0.5f, Muted, -1);
		}
		const int32 Secs = FMath::CeilToInt(FMath::Max(0.f, G.TimeLeft));
		const bool bLate = Secs <= 10 && G.Phase == EKSPhase::Fight;
		PushSpin(0.5f, 0.088f, bLate ? 1.f + 0.08f * FMath::Abs(FMath::Sin(Now * PI)) : 1.f, 0.f);
		Pic(TEXT("timer"), 0.5f, 0.088f, 0.165f);
		Text(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60), 0.5f, 0.086f, 34.f, bLate ? Red : FLinearColor(1.f, 0.95f, 0.6f), 0, EKSFont::Hud, 2.f, Edge, 0.07f);
		PopSpin();
	}

	if (G.Phase == EKSPhase::Fight)
	{
		if (!G.CueMain.IsEmpty())
		{
			// What is coming at you and what to do about it, together on one plate close under the clock, so that it
			// stays above the opponent's head; an opening flashes.
			const float Beat = G.bCueGood ? 1.f + 0.06f * FMath::Sin(Now * 16.f) : 1.f;
			const float CY = 0.240f, CH = 0.118f;
			const float W = FMath::Min(0.9f, FMath::Max(TextSize(G.CueMain, 34.f, EKSFont::Comic).X, TextSize(G.CueHint, 14.f, EKSFont::Hud).X) / SW + 0.13f);
			PushSpin(0.5f, CY, Beat, 0.f);
			Plate(0.5f - W * 0.5f, CY - CH * 0.5f, W, CH, 0.95f, G.bCueGood ? FLinearColor(0.5f, 1.f, 0.6f) : FLinearColor::White);
			Text(G.CueMain, 0.5f, CY - 0.022f, 34.f, G.bCueGood ? FLinearColor(0.75f, 1.f, 0.4f) : Yellow, 0, EKSFont::Comic, 2.4f, FLinearColor::Black, W - 0.11f);
			Text(G.CueHint, 0.5f, CY + 0.015f, 14.f, FLinearColor::White, 0, EKSFont::Hud, 1.6f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), W - 0.10f);
			PopSpin();
			Reserve(0.5f - W * 0.5f, CY - CH * 0.5f, W, CH);
		}
		if (G.Combo != SeenCombo) { SeenCombo = G.Combo; ComboAt = Now; }
		if (G.Combo > 1)
		{
			// The combo count has a corner of its own, under the opponent's name.
			const float X = 0.925f, Y = 0.265f, High = 0.15f;
			PushSpin(X, Y, PopScale(Now - ComboAt), 9.f);
			Pic(TEXT("burst_hit"), X, Y, High);
			Text(FString::Printf(TEXT("x%d"), G.Combo), X, Y - 0.012f, 38.f, FLinearColor(0.95f, 0.08f, 0.05f), 0, EKSFont::Comic, 2.2f, FLinearColor::Black, 0.06f);
			Text(TEXT("COMBO"), X, Y + 0.027f, 15.f, FLinearColor::Black, 0, EKSFont::Comic, 0.f);
			PopSpin();
			Reserve(X - 0.065f, Y - High * 0.5f, 0.13f, High);
		}
	}
	// The little mirror of the tracker in the bottom corner is not for covering either.
	if (G.Tracking() && G.Tracking()->HasLandmarks()) Reserve(0.015f, 0.70f, 0.15f, 0.27f);
	if (!bPanels) PlaceBubble(G);
	for (const FKSPopup& P : G.Popups) DrawPopup(G, P);
	DrawAnnouncement(G);

	if (G.Phase == EKSPhase::Result)
	{
		const float T = G.PhaseTime;
		Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.02f, 0.f, 0.06f, 0.45f));
		Panel(0.16f, 0.27f, 0.68f, 0.55f);
		PushSpin(0.5f, 0.155f, SlamScale(T), -2.f);
		if (!Pic(TEXT("t_youwin"), 0.5f, 0.155f, 0.26f)) Text(TEXT("YOU WIN"), 0.5f, 0.155f, 90.f, Green, 0, EKSFont::Comic, 3.f);
		PopSpin();
		if (G.bLastResultDecision) Text(TEXT("ON POINTS"), 0.5f, 0.372f, 24.f, Yellow, 0, EKSFont::Comic, 2.f, FLinearColor::Black);
		Lines(G.bOnline ? TEXT("A real opponent, and you put them down.") : F.DefeatLine, 0.5f, 0.435f, 0.58f, 2, 19.f, Lilac);
		// The numbers count in one after another.
		const FKSStats& S = G.Stats;
		const TPair<const TCHAR*, int32> Rows[] = { { TEXT("LANDED"), S.Landed }, { TEXT("COUNTERS"), S.Counters }, { TEXT("DODGES"), S.Dodges }, { TEXT("BLOCKS"), S.Blocks },
			{ TEXT("HITS TAKEN"), S.Taken }, { TEXT("BEST COMBO"), S.BestCombo }, { TEXT("HEALTH LEFT"), FMath::RoundToInt(G.PlayerHP) } };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); i++)
		{
			const float Shown = T - 0.35f - 0.11f * i;
			if (Shown < 0.f) continue;
			const float X = 0.235f + 0.0885f * i;
			PushSpin(X, 0.545f, PopScale(Shown), 0.f);
			Text(FString::FromInt(Rows[i].Value), X, 0.535f, 50.f, i == 4 ? Magenta : Cyan, 0, EKSFont::Comic, 2.4f, FLinearColor::Black, 0.08f);
			PopSpin();
			Text(Rows[i].Key, X, 0.598f, 12.5f, Lilac, 0, EKSFont::Hud, 2.f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), 0.084f);
		}
		const bool bLast = G.FighterIndex + 1 >= G.NumFights();
		Plate(0.27f, 0.632f, 0.46f, 0.07f);
		Label(G.bOnline ? Rematch : bLast ? FString(TEXT("The screen cracks. There is a way out.")) : FString::Printf(TEXT("NEXT: %s"), G.LadderFighter(G.FighterIndex + 1).Name), 0.5f, 0.666f, 1.05f, Yellow, 0, 0.36f);
		if (FMath::Frac(Now * 0.9f) < 0.65f) Label(G.bOnline ? RematchKeys : TEXT("Press ENTER or jab left + right"), 0.5f, 0.733f, 0.9f, Lilac, 0, 0.58f);
	}
	if (G.Phase == EKSPhase::Continue)
	{
		const float T = G.PhaseTime;
		Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.f, 0.f, 0.f, G.BlackFade));
		PushSpin(0.5f, 0.24f, SlamScale(T), -2.f);
		const TCHAR* Art = G.bLastResultDecision ? TEXT("t_youlose") : TEXT("t_knockedout");
		if (!Pic(Art, 0.5f, 0.24f, 0.34f)) Text(G.bLastResultDecision ? TEXT("LOST ON POINTS") : TEXT("KNOCKED OUT"), 0.5f, 0.27f, 80.f, Red, 0, EKSFont::Comic, 3.f);
		PopSpin();
		if (G.bOnline)
		{
			// No countdown against a person: the two players agree on a rematch or one of them leaves.
			Label(G.bLastResultDecision ? TEXT("Lost on points, to another player.") : TEXT("Put down by another player."), 0.5f, 0.475f, 1.1f, Lilac, 0, 0.94f);
			Plate(0.27f, 0.585f, 0.46f, 0.085f);
			Label(Rematch, 0.5f, 0.626f, 1.05f, Yellow, 0, 0.38f);
			if (FMath::Frac(Now * 0.9f) < 0.65f) Label(RematchKeys, 0.5f, 0.775f, 1.0f, Lilac, 0, 0.94f);
			DrawTracker(G, true);
			return;
		}
		Label(G.bLastResultDecision ? TEXT("Lost on points. The cabinet keeps you for another round.") : TEXT("The cabinet keeps you for another round."), 0.5f, 0.475f, 1.1f, Lilac, 0, 0.94f);
		const int32 Count = FMath::Max(0, FMath::FloorToInt(G.ContinueLeft));
		if (Count != SeenCount) { SeenCount = Count; CountAt = Now; }
		if (!Pic(TEXT("t_continue"), 0.43f, 0.63f, 0.17f)) Text(TEXT("CONTINUE?"), 0.43f, 0.63f, 70.f, Yellow, 0, EKSFont::Comic, 3.f);
		// The count jumps with every second.
		PushSpin(0.74f, 0.63f, PopScale(Now - CountAt) * 1.05f, 8.f);
		Pic(TEXT("burst_hurt"), 0.74f, 0.63f, 0.25f);
		Text(FString::FromInt(Count), 0.74f, 0.625f, 78.f, FLinearColor(0.85f, 0.f, 0.25f), 0, EKSFont::Comic, 3.f, FLinearColor::Black);
		PopSpin();
		if (FMath::Frac(Now * 0.9f) < 0.65f) Label(TEXT("Press ENTER or jab left + right"), 0.5f, 0.775f, 1.0f, Lilac);
	}
	if (G.bPaused)
	{
		Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.02f, 0.f, 0.06f, 0.65f));
		if (!Pic(TEXT("t_paused"), 0.5f, 0.44f + 0.006f * FMath::Sin(Now * 2.f), 0.26f)) Text(TEXT("PAUSED"), 0.5f, 0.45f, 80.f, Lilac, 0, EKSFont::Comic, 3.f);
		Label(TEXT("Press P to carry on"), 0.5f, 0.62f, 1.0f, Lilac);
	}
	DrawTracker(G, true);
}

void AKSHUD::DrawCalibrate(const AKSGameMode& G)
{
	const UKSTrackingSubsystem* T = G.Tracking();
	Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.02f, 0.f, 0.05f, 0.72f));
	Plate(0.30f, 0.02f, 0.40f, 0.085f);
	Label(FString::Printf(TEXT("CALIBRATION   STEP %d OF %d"), G.CalStep + 1, AKSGameMode::CalSteps), 0.5f, 0.061f, 1.15f, FLinearColor::White, 0, 0.30f);
	// How far a panel's chrome rim reaches in from its edge. Everything written or drawn in a panel stays inside it.
	const float RimX = 42.f * Unit / SW, RimY = 42.f * Unit / SH;
	bool bFits = true;

	/* ---- the mirror: what the camera sees of you, flipped like a mirror ---- */
	const float PX = 0.03f, PY = 0.13f, PW = 0.43f, PH = 0.70f;
	Panel(PX, PY, PW, PH);
	const float BX = PX + RimX, BY = PY + RimY, BW = PW - 2.f * RimX, BH = PH - 2.f * RimY;
	// Where the shoulders must be above, and the elbows inside.
	Fill(BX, BY + BH * 0.52f, BW, 0.003f, Faded(Lilac, 0.5f));
	Label(TEXT("shoulders above this line"), BX + 0.012f, BY + BH * 0.52f - 0.020f, 0.7f, Lilac, -1, BW - 0.024f);
	Fill(BX, BY + BH * 0.95f, BW, 0.003f, Faded(Lilac, 0.5f));
	Label(TEXT("elbows above this line"), BX + 0.012f, BY + BH * 0.95f - 0.020f, 0.7f, Lilac, -1, BW - 0.024f);
	if (T && T->IsBodySeen() && T->GetImageLandmarks().Num() >= 33)
	{
		const TArray<FVector2f>& L = T->GetImageLandmarks();
		auto Pt = [&](int32 i) { return FVector2D((BX + FMath::Clamp(1.f - L[i].X, 0.f, 1.f) * BW) * SW, (BY + FMath::Clamp(L[i].Y, 0.f, 1.f) * BH) * SH); };
		auto Seg = [&](int32 A, int32 B, const FLinearColor& C)
		{
			// A limb that leaves the picture is drawn in red, pinned to the edge it left through.
			const bool bOut = L[A].Y > 1.f || L[B].Y > 1.f || L[A].X < 0.f || L[A].X > 1.f || L[B].X < 0.f || L[B].X > 1.f;
			const FVector2D P = Pt(A), Q = Pt(B);
			DrawLine(P.X, P.Y, Q.X, Q.Y, bOut ? Red : C, 5.f * Unit);
		};
		Seg(11, 12, Lilac); Seg(11, 23, Muted); Seg(12, 24, Muted);
		Seg(11, 13, Cyan); Seg(13, 15, Cyan); Seg(12, 14, Magenta); Seg(14, 16, Magenta);
		const FVector2D Head = Pt(0);
		DrawRect(Yellow, Head.X - 9.f * Unit, Head.Y - 9.f * Unit, 18.f * Unit, 18.f * Unit);
		for (int32 Wrist : { 15, 16 }) { const FVector2D W = Pt(Wrist); DrawRect(Wrist == 15 ? Cyan : Magenta, W.X - 7.f * Unit, W.Y - 7.f * Unit, 14.f * Unit, 14.f * Unit); }
	}
	else if (!G.bCalSteady[0]) Label(TEXT("no body in view"), BX + BW * 0.5f, BY + BH * 0.3f, 1.0f, Muted, 0, BW - 0.03f);   // not for a single lost frame: that made the words blink
	Label(TEXT("YOU, AS THE CAMERA SEES YOU"), PX + PW * 0.5f, PY + PH + 0.028f, 0.7f, Lilac, 0, PW);

	/* ---- what to do: the move, the advice (two lines at most) and how far along, all inside the panel ---- */
	const float QX = 0.49f, QY = 0.13f, QW = 0.48f, QH = 0.32f, RX = QX + QW * 0.5f, Room = QW - 2.f * RimX - 0.02f;
	Panel(QX, QY, QW, QH);
	Text(G.CalTitle, RX, QY + RimY + 0.040f, 40.f, Yellow, 0, EKSFont::Comic, 2.6f, FLinearColor::Black, Room);
	const bool bWarn = G.CalHint.Contains(TEXT("STEP")) || G.CalHint.Contains(TEXT("MOVE")) || G.CalHint.Contains(TEXT("COME"));
	bFits &= Lines(G.CalHint, RX, QY + RimY + 0.112f, Room, 2, 19.f, bWarn ? Red : Lilac);
	bFits &= TextSize(G.CalTitle, Fit(G.CalTitle, 40.f, EKSFont::Comic, Room), EKSFont::Comic).X <= Room * SW;
	const float BarW = Room * 0.92f, BarY = QY + QH - RimY - 0.036f;
	Fill(RX - BarW * 0.5f, BarY, BarW, 0.022f, FLinearColor(0.15f, 0.08f, 0.25f, 0.9f));
	Fill(RX - BarW * 0.5f, BarY, BarW * FMath::Clamp(G.CalProgress, 0.f, 1.f), 0.022f, Green);
	Fill(RX - BarW * 0.5f, BarY, BarW * FMath::Clamp(G.CalProgress, 0.f, 1.f), 0.007f, FLinearColor(1.f, 1.f, 1.f, 0.4f));

	/* ---- below it: the count of a move being practised, or the list of what the camera still needs ---- */
	if (G.CalChecks.Num())
	{
		const float Row = 0.045f, CY = 0.50f;
		Panel(QX, CY, QW, 2.f * RimY + Row * G.CalChecks.Num());
		float Y = CY + RimY + Row * 0.5f;
		for (const TPair<FString, bool>& Check : G.CalChecks)
		{
			Label(Check.Value ? TEXT("OK") : TEXT("NO"), QX + RimX + 0.012f, Y, 0.9f, Check.Value ? Green : Red, -1);
			Label(Check.Key, QX + RimX + 0.062f, Y, 0.9f, Check.Value ? Green : Red, -1, Room - 0.06f);
			Y += Row;
		}
	}
	else if (G.CalCountNeed > 0)
	{
		Pic(TEXT("burst_hit"), RX, 0.635f, 0.26f);
		Text(FString::Printf(TEXT("%d / %d"), G.CalCountNow, G.CalCountNeed), RX, 0.630f, 54.f, G.CalCountNow >= G.CalCountNeed ? FLinearColor(0.f, 0.5f, 0.1f) : FLinearColor(0.95f, 0.08f, 0.05f), 0, EKSFont::Comic, 2.6f, FLinearColor::Black, 0.10f);
	}
	Label(TEXT("SPACE  skip this step          K  play with the keyboard instead"), 0.5f, 0.945f, 0.85f, Lilac, 0, 0.94f);

	// For tools/check.sh: writing that does not fit its panel even at the smallest size.
	bCalTextOutside |= !bFits;
	if (!bLoggedCal && G.CalStep >= 3) { bLoggedCal = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui calibration text_outside_box=%d"), bCalTextOutside ? 1 : 0); }
}

void AKSHUD::DrawHUD()
{
	Super::DrawHUD();
	const AKSGameMode* G = GetWorld() ? Cast<AKSGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	if (!G || !Canvas) return;
	SW = Canvas->SizeX; SH = Canvas->SizeY; Unit = SH / 720.f; Now = GetWorld()->GetTimeSeconds();
	if (!bArtLoaded) LoadArt();

	switch (G->Phase)
	{
	case EKSPhase::Menu: DrawMenu(*G); break;
	case EKSPhase::Intro: DrawIntro(*G); break;
	case EKSPhase::Select: DrawSelect(*G); break;
	case EKSPhase::Online: DrawOnline(*G); break;
	case EKSPhase::Street: case EKSPhase::PullIn: case EKSPhase::Ending: DrawStreet(*G); break;
	case EKSPhase::Calibrate: DrawCalibrate(*G); break;
	default: DrawFight(*G); break;
	}

	// Whoever is speaking gets a comic speech bubble by the head; over the result and continue panels, a plate along the bottom.
	if (G->Phase == EKSPhase::Card || G->Phase == EKSPhase::Fight || G->Phase == EKSPhase::FightEnd) SpeechBubble(*G);
	else if (G->Phase == EKSPhase::Result || G->Phase == EKSPhase::Continue) SpeechCaption(*G);
	else { BubbleLine.Reset(); bBubbleSet = false; }

	if (G->RedFlash > 0.f) Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(1.f, 0.f, 0.f, 0.35f * G->RedFlash));
	if (G->WhiteFade > 0.f) Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(1.f, 1.f, 1.f, G->WhiteFade));
}
