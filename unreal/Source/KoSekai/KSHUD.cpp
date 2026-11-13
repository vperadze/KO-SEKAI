#include "KSHUD.h"
#include "KSGameMode.h"
#include "KSTracking.h"
#include "KSAudio.h"
#include "KSUIData.h"
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
		TEXT("t_fight"), TEXT("t_ko"), TEXT("t_youwin"), TEXT("t_youlose"), TEXT("t_knockedout"), TEXT("t_time"), TEXT("t_down"), TEXT("t_continue"), TEXT("t_paused") };

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

void AKSHUD::Text(const FString& Str, float X, float Y, float Px, const FLinearColor& Color, int32 Align, EKSFont Which, float Edge, const FLinearColor& EdgeColor)
{
	if (Str.IsEmpty() || Color.A <= 0.004f) return;
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
		Label(G.Subtitle, 0.5f, 0.82f, 1.2f, Lilac);
	}
	if (!G.Prompt.IsEmpty())
	{
		const float Pulse = 0.65f + 0.35f * FMath::Sin(Now * 5.f);
		Label(G.Prompt, 0.5f, 0.915f, 1.0f, Faded(Yellow, Pulse));
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
	if (T->AreHandsOutOfView())
	{
		Text(TEXT("STEP BACK"), 0.5f, 0.30f, 64.f, Yellow, 0, EKSFont::Comic, 3.f);
		Label(TEXT("the camera must see your elbows and fists"), 0.5f, 0.37f, 0.9f, Yellow, 0);
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
	float X = P.Pos.X, Y = P.Pos.Y;
	if (P.Kind == 1 || P.Kind == 3)
	{
		// Pinned to the opponent's head; Pos is how far off it sits.
		const FVector Head = Project(G.OppHead);
		if (Head.Z > 0.f) { X = FMath::Clamp(Head.X / SW + P.Pos.X, 0.1f, 0.9f); Y = FMath::Clamp(Head.Y / SH + P.Pos.Y, 0.2f, 0.85f); }
		else { X = 0.6f + P.Pos.X; Y = 0.38f + P.Pos.Y; }
	}
	Y -= 0.04f * U;
	const float Tilt = (Hash(P.Start * 7.3f + P.Pos.X) - 0.5f) * 26.f;
	PushSpin(X, Y, PopScale(T) * (P.bBig ? 1.3f : 1.f) * (1.f + 0.03f * FMath::Sin(T * 40.f)), Tilt);
	const TCHAR* Art = ArtFor(P.Text);
	if (P.Kind == 1 || P.Kind == 2)
	{
		// A damage number on its burst.
		const bool bDealt = P.Kind == 1;
		Pic(bDealt ? TEXT("burst_hit") : TEXT("burst_hurt"), X, Y, 0.19f, Alpha);
		Text(P.Text, X, Y - 0.004f, 56.f, Faded(bDealt ? FLinearColor(0.95f, 0.08f, 0.05f) : FLinearColor(0.75f, 0.f, 0.3f), Alpha), 0, EKSFont::Comic, 2.6f, FLinearColor(0.f, 0.f, 0.f, 1.f));
		if (!bLoggedDamage) { bLoggedDamage = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui damage badge shown text=%s"), *P.Text); }
	}
	else if (Art && Pic(Art, X, Y, 0.20f, Alpha)) {}
	else if (P.Text.EndsWith(TEXT(" HITS")))
	{
		Pic(TEXT("burst_hit"), X, Y, 0.20f, Alpha);
		Text(P.Text.LeftChop(5), X, Y - 0.018f, 52.f, Faded(FLinearColor(0.95f, 0.08f, 0.05f), Alpha), 0, EKSFont::Comic, 2.6f, FLinearColor::Black);
		Text(TEXT("HITS"), X, Y + 0.034f, 24.f, Faded(FLinearColor::Black, Alpha), 0, EKSFont::Comic, 0.f);
	}
	else Text(P.Text, X, Y, P.bBig ? 60.f : 40.f, Faded(P.Color, Alpha), 0, EKSFont::Comic, 3.f, FLinearColor::Black);
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
	if (!Art || !Pic(Art, X, Y, 0.36f, Alpha))
	{
		const float W = TextSize(G.Announcement, 72.f, EKSFont::Comic).X / SW + 0.16f;
		Plate(X - W * 0.5f, Y - 0.075f, W, 0.15f, Alpha);
		Text(G.Announcement, X, Y, 72.f, Faded(Yellow, Alpha), 0, EKSFont::Comic, 3.f, FLinearColor::Black);
	}
	PopSpin();
}

void AKSHUD::SpeechBubble(const AKSGameMode& G)
{
	if (G.SpokenLine.IsEmpty()) { BubbleLine.Reset(); return; }
	if (G.SpokenLine != BubbleLine)
	{
		BubbleLine = G.SpokenLine; BubbleStart = Now;
		KSAudio::Play(this, EKSSfx::Pop, 0.6f);
		if (!bLoggedBubble) { bLoggedBubble = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui speech bubble shown picture=%d"), Tex(TEXT("bubble")) ? 1 : 0); }
	}
	const float T = Now - BubbleStart;
	UTexture2D* B = Tex(TEXT("bubble"));
	const float H = 0.33f, W = H * SH * (B ? (float)B->GetSizeX() / B->GetSizeY() : 1.6f) / SW;

	// The bubble hangs beside the speaker's head with its tail towards it: up and right of the opponent, left of the player's fighter.
	const bool bOpp = G.bSpokenByOpp;
	const FVector Head = Project(bOpp ? G.OppHead : G.PlayerHead);
	FVector2D At = Head.Z > 0.f ? FVector2D(Head.X / SW, Head.Y / SH) : bOpp ? FVector2D(0.5f, 0.40f) : FVector2D(0.5f, 0.78f);
	const bool bFlip = !bOpp;      // the picture's tail points down and left
	// It keeps out from under the health bars and the cue plate (down to 0.37 of the screen) and off the fighters in the middle.
	const float X = FMath::Clamp(At.X + (bFlip ? -0.58f : 0.58f) * W, W * 0.5f + 0.01f, 1.f - W * 0.5f - 0.01f);
	const float Y = FMath::Clamp(At.Y - H * 0.30f, (G.Phase == EKSPhase::Fight ? 0.37f : 0.17f) + H * 0.5f, 0.99f - H * 0.5f);

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
	const float RoomW = (I1 - I0) * W * SW * 1.16f, RoomH = (V1 - V0) * H * SH * 1.25f;

	const FString Line = G.SpokenLine.ToUpper();
	float Px = 46.f; TArray<FString> Lines;
	for (; Px >= 18.f; Px -= 3.f)
	{
		Lines = Wrap(Line, Px, EKSFont::Comic, RoomW);
		bool bFits = Lines.Num() * Px * Unit * 1.02f <= RoomH;
		for (const FString& L : Lines) bFits &= TextSize(L, Px, EKSFont::Comic).X <= RoomW;
		if (bFits) break;
	}
	// The longest word is the one that gets shouted in red.
	TArray<FString> Words; Line.ParseIntoArray(Words, TEXT(" "), true);
	FString Shout;
	for (const FString& Word : Words) if (Word.Len() > Shout.Len()) Shout = Word;
	const float Step = Px * 1.02f / 720.f;
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		const float LY = CY + (i - (Lines.Num() - 1) * 0.5f) * Step;
		Text(Lines[i], CX, LY, Px, FLinearColor(0.04f, 0.02f, 0.06f), 0, EKSFont::Comic, 0.f);
		const int32 Found = Shout.Len() >= 4 ? Lines[i].Find(Shout, ESearchCase::CaseSensitive) : INDEX_NONE;
		if (Found != INDEX_NONE)
		{
			const float Whole = TextSize(Lines[i], Px, EKSFont::Comic).X, Before = Found > 0 ? TextSize(Lines[i].Left(Found), Px, EKSFont::Comic).X : 0.f;
			Text(Shout, CX + (Before - Whole * 0.5f) / SW, LY, Px, FLinearColor(0.9f, 0.03f, 0.08f), -1, EKSFont::Comic, 0.f);
			Shout.Reset();
		}
	}
	// Who is talking, on a name tag at the bubble's top corner.
	const float TagW = TextSize(G.SpokenBy, 17.f, EKSFont::Hud).X / SW + 0.05f, TagX = X + (bFlip ? 0.16f : -0.16f) * W;
	Plate(TagX - TagW * 0.5f, Y - H * 0.47f, TagW, 0.046f, 1.f, bOpp ? FLinearColor::White : FLinearColor(0.45f, 0.9f, 1.f));
	Text(G.SpokenBy, TagX, Y - H * 0.47f + 0.022f, 17.f, FLinearColor::White);
	PopSpin();
}

/* ================= the screens ================= */

void AKSHUD::DrawMenu(const AKSGameMode& G)
{
	const float T = G.PhaseTime;
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
	Label(TEXT("T H E   G A M E   T H A T   W A S   N E V E R   R E L E A S E D"), 0.5f, 0.435f, 0.95f, Faded(Lilac, In));

	if (G.MenuIndex != SeenMenu) { SeenMenu = G.MenuIndex; MenuAt = Now; }
	static const TCHAR* Items[] = { TEXT("START GAME"), TEXT("STRAIGHT TO THE RING"), TEXT("QUIT") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Items); i++)
	{
		const bool bOn = i == G.MenuIndex;
		// Each plate slides in from the side a moment after the one above it.
		const float Slide = 1.f - FMath::Clamp((T - 0.45f - 0.09f * i) / 0.25f, 0.f, 1.f);
		const float Y = 0.555f + 0.105f * i, X = 0.5f + Slide * Slide * (i % 2 ? 0.7f : -0.7f);
		const float Bump = bOn ? PopScale(Now - MenuAt) * (1.06f + 0.012f * FMath::Sin(Now * 6.f)) : 0.92f;
		PushSpin(X, Y, Bump, 0.f);
		Plate(X - 0.21f, Y - 0.043f, 0.42f, 0.086f, bOn ? 1.f : 0.8f, bOn ? FLinearColor::White : FLinearColor(0.42f, 0.36f, 0.6f));
		Text(Items[i], X, Y - 0.002f, 30.f, bOn ? FLinearColor(1.f, 0.95f, 0.45f) : Lilac, 0, EKSFont::Hud, 2.2f);
		if (bOn)
		{
			// The glove punches at the chosen line.
			const float Jab = FMath::Abs(FMath::Sin(Now * 5.f)) * 0.012f;
			if (!Pic(TEXT("glove"), X - 0.255f + Jab, Y, 0.10f)) Text(TEXT(">"), X - 0.24f + Jab, Y, 34.f, Yellow);
		}
		PopSpin();
	}

	const UKSTrackingSubsystem* Tr = G.Tracking();
	const bool bPunches = Tr && Tr->IsTracking();
	Label(bPunches ? TEXT("UP / DOWN  or  LEFT PUNCH  choose          ENTER  or  RIGHT PUNCH  select")
		: TEXT("UP / DOWN  choose          ENTER  select"), 0.5f, 0.905f, 0.8f, Faded(Lilac, In));
	if (FMath::Frac(Now * 0.8f) < 0.62f) Label(TEXT("INSERT YOURSELF"), 0.5f, 0.955f, 0.95f, Faded(Yellow, In));
	Label(TEXT("(C) 1987   CANCELLED BEFORE LAUNCH"), 0.015f, 0.975f, 0.6f, Faded(Muted, In), -1);

	// Scan lines, as on the tube of an old cabinet.
	for (float Y = 0.f; Y < SH; Y += 4.f * Unit) DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.13f), 0.f, Y, SW, 1.5f * Unit);
	if (!bLoggedMenu) { bLoggedMenu = true; UE_LOG(LogTemp, Display, TEXT("KSTEST ui menu shown background=%d logo=%d"), Tex(TEXT("title_bg")) ? 1 : 0, Tex(TEXT("logo")) ? 1 : 0); }
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

	if (G.Phase == EKSPhase::Card && G.PhaseTime < 2.6f)
	{
		const float T = G.PhaseTime, A = FMath::Clamp(FMath::Min(T / 0.3f, (2.6f - T) / 0.3f), 0.f, 1.f);
		Fill(0.f, 0.f, 1.f, 0.15f, FLinearColor(0.02f, 0.f, 0.06f, 0.85f * A));
		Fill(0.f, 0.146f, 1.f, 0.005f, Faded(Magenta, A));
		Fill(0.f, 0.60f, 1.f, 0.40f, FLinearColor(0.02f, 0.f, 0.06f, 0.85f * A));
		Fill(0.f, 0.598f, 1.f, 0.005f, Faded(Cyan, A));
		Plate(0.33f, 0.035f, 0.34f, 0.085f, A);
		Label(FString::Printf(TEXT("CUT FIGHTER %d OF %d"), G.FighterIndex + 1, G.NumFights()), 0.5f, 0.076f, 1.15f, Faded(FLinearColor::White, A));
		// The name slides in from the right, the VS badge bangs in on the left.
		const float Slide = 1.f - FMath::Clamp(T / 0.28f, 0.f, 1.f);
		Text(F.Name, 0.56f + Slide * Slide * 0.8f, 0.705f, 92.f, Faded(Yellow, A), 0, EKSFont::Comic, 3.f, FLinearColor::Black);
		PushSpin(0.115f, 0.71f, SlamScale(FMath::Max(0.f, T - 0.15f)) * (1.f + 0.03f * FMath::Sin(Now * 9.f)), -8.f);
		if (T > 0.15f) Pic(TEXT("vs"), 0.115f, 0.71f, 0.24f, A);
		PopSpin();
		Label(F.Tagline, 0.56f, 0.805f, 1.15f, Faded(Lilac, A));
		Label(FString::Printf(TEXT("YOU FIGHT AS %s"), G.PlayerFighter().Name), 0.56f, 0.862f, 1.05f, Faded(Cyan, A));
		const bool bTracked = G.Tracking() && G.Tracking()->IsConnected();
		Label(bTracked ? TEXT("PUNCH, BLOCK, LEAN AND DUCK FOR REAL - THE CABINET IS WATCHING")
			: TEXT("A/Q/Z left straight/hook/uppercut   D/E/C right   SPACE block   ARROWS evade + duck   P pause"), 0.5f, 0.915f, 0.78f, Faded(Muted, A));
		DrawTracker(G, true);
		return;
	}

	/* health, names, clock */
	if (SeenPlayerHP >= 0.f && G.PlayerHP < SeenPlayerHP - 0.01f) PlayerJoltAt = Now;
	if (SeenOppHP >= 0.f && G.OppHP < SeenOppHP - 0.01f) OppJoltAt = Now;
	SeenPlayerHP = G.PlayerHP; SeenOppHP = G.OppHP;
	auto Jolt = [this](float At) { return FMath::Clamp(1.f - (Now - At) / 0.28f, 0.f, 1.f); };
	HealthBar(0.025f, 0.405f, G.PlayerHP / 100.f, G.PlayerTrail / 100.f, Cyan, false, Jolt(PlayerJoltAt));
	HealthBar(0.57f, 0.405f, G.OppHP / G.OppMaxHP, G.OppTrail / G.OppMaxHP, Magenta, true, Jolt(OppJoltAt));
	Text(FString::Printf(TEXT("YOU  -  %s"), G.PlayerFighter().Name), 0.07f, 0.150f, 19.f, Cyan, -1);
	Text(F.Name, 0.93f, 0.150f, 19.f, Magenta, 1);
	const int32 Secs = FMath::CeilToInt(FMath::Max(0.f, G.TimeLeft));
	const bool bLate = Secs <= 10 && G.Phase == EKSPhase::Fight;
	PushSpin(0.5f, 0.088f, bLate ? 1.f + 0.08f * FMath::Abs(FMath::Sin(Now * PI)) : 1.f, 0.f);
	Pic(TEXT("timer"), 0.5f, 0.088f, 0.165f);
	Text(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60), 0.5f, 0.086f, 34.f, bLate ? Red : FLinearColor(1.f, 0.95f, 0.6f));
	PopSpin();

	if (G.Phase == EKSPhase::Fight)
	{
		if (!G.CueMain.IsEmpty())
		{
			// What is coming at you, on a plate; an opening flashes.
			const float Beat = G.bCueGood ? 1.f + 0.06f * FMath::Sin(Now * 16.f) : 1.f;
			const float W = TextSize(G.CueMain, 50.f, EKSFont::Comic).X / SW + 0.13f;
			PushSpin(0.5f, 0.255f, Beat, 0.f);
			Plate(0.5f - W * 0.5f, 0.255f - 0.052f, W, 0.104f, 0.95f, G.bCueGood ? FLinearColor(0.5f, 1.f, 0.6f) : FLinearColor::White);
			Text(G.CueMain, 0.5f, 0.252f, 50.f, G.bCueGood ? FLinearColor(0.75f, 1.f, 0.4f) : Yellow, 0, EKSFont::Comic, 2.6f, FLinearColor::Black);
			PopSpin();
			Label(G.CueHint, 0.5f, 0.335f, 1.0f, Lilac);
		}
		if (G.Combo != SeenCombo) { SeenCombo = G.Combo; ComboAt = Now; }
		if (G.Combo > 1)
		{
			PushSpin(0.905f, 0.285f, PopScale(Now - ComboAt), 9.f);
			Pic(TEXT("burst_hit"), 0.905f, 0.285f, 0.19f);
			Text(FString::Printf(TEXT("x%d"), G.Combo), 0.905f, 0.270f, 46.f, FLinearColor(0.95f, 0.08f, 0.05f), 0, EKSFont::Comic, 2.4f, FLinearColor::Black);
			Text(TEXT("COMBO"), 0.905f, 0.318f, 19.f, FLinearColor::Black, 0, EKSFont::Comic, 0.f);
			PopSpin();
		}
	}
	for (const FKSPopup& P : G.Popups) DrawPopup(G, P);
	DrawAnnouncement(G);

	if (G.Phase == EKSPhase::Result)
	{
		const float T = G.PhaseTime;
		Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.02f, 0.f, 0.06f, 0.45f));
		Panel(0.16f, 0.27f, 0.68f, 0.55f);
		PushSpin(0.5f, 0.24f, SlamScale(T), -2.f);
		if (!Pic(TEXT("t_youwin"), 0.5f, 0.24f, 0.30f)) Text(TEXT("YOU WIN"), 0.5f, 0.24f, 90.f, Green, 0, EKSFont::Comic, 3.f);
		PopSpin();
		if (G.bLastResultDecision) Text(TEXT("ON POINTS"), 0.5f, 0.405f, 26.f, Yellow, 0, EKSFont::Comic, 2.f, FLinearColor::Black);
		Label(F.DefeatLine, 0.5f, 0.455f, 0.95f, Lilac);
		// The numbers count in one after another.
		const FKSStats& S = G.Stats;
		const TPair<const TCHAR*, int32> Rows[] = { { TEXT("LANDED"), S.Landed }, { TEXT("COUNTERS"), S.Counters }, { TEXT("DODGES"), S.Dodges }, { TEXT("BLOCKS"), S.Blocks },
			{ TEXT("HITS TAKEN"), S.Taken }, { TEXT("BEST COMBO"), S.BestCombo }, { TEXT("HEALTH LEFT"), FMath::RoundToInt(G.PlayerHP) } };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); i++)
		{
			const float Shown = T - 0.35f - 0.11f * i;
			if (Shown < 0.f) continue;
			const float X = 0.235f + 0.0885f * i;
			PushSpin(X, 0.555f, PopScale(Shown), 0.f);
			Text(FString::FromInt(Rows[i].Value), X, 0.545f, 52.f, i == 4 ? Magenta : Cyan, 0, EKSFont::Comic, 2.4f, FLinearColor::Black);
			PopSpin();
			Text(Rows[i].Key, X, 0.613f, 12.5f, Lilac);
		}
		const bool bLast = G.FighterIndex + 1 >= G.NumFights();
		Plate(0.27f, 0.655f, 0.46f, 0.075f);
		Label(bLast ? TEXT("The screen cracks. There is a way out.") : FString::Printf(TEXT("NEXT: %s"), G.LadderFighter(G.FighterIndex + 1).Name), 0.5f, 0.691f, 1.05f, Yellow);
		if (FMath::Frac(Now * 0.9f) < 0.65f) Label(TEXT("Press ENTER or throw a punch"), 0.5f, 0.765f, 0.9f, Lilac);
	}
	if (G.Phase == EKSPhase::Continue)
	{
		const float T = G.PhaseTime;
		Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.f, 0.f, 0.f, G.BlackFade));
		PushSpin(0.5f, 0.27f, SlamScale(T), -2.f);
		const TCHAR* Art = G.bLastResultDecision ? TEXT("t_youlose") : TEXT("t_knockedout");
		if (!Pic(Art, 0.5f, 0.27f, 0.34f)) Text(G.bLastResultDecision ? TEXT("LOST ON POINTS") : TEXT("KNOCKED OUT"), 0.5f, 0.27f, 80.f, Red, 0, EKSFont::Comic, 3.f);
		PopSpin();
		Label(G.bLastResultDecision ? TEXT("Lost on points. The cabinet keeps you for another round.") : TEXT("The cabinet keeps you for another round."), 0.5f, 0.475f, 1.1f, Lilac);
		const int32 Count = FMath::Max(0, FMath::FloorToInt(G.ContinueLeft));
		if (Count != SeenCount) { SeenCount = Count; CountAt = Now; }
		if (!Pic(TEXT("t_continue"), 0.43f, 0.63f, 0.17f)) Text(TEXT("CONTINUE?"), 0.43f, 0.63f, 70.f, Yellow, 0, EKSFont::Comic, 3.f);
		// The count jumps with every second.
		PushSpin(0.74f, 0.63f, PopScale(Now - CountAt) * 1.05f, 8.f);
		Pic(TEXT("burst_hurt"), 0.74f, 0.63f, 0.25f);
		Text(FString::FromInt(Count), 0.74f, 0.625f, 78.f, FLinearColor(0.85f, 0.f, 0.25f), 0, EKSFont::Comic, 3.f, FLinearColor::Black);
		PopSpin();
		if (FMath::Frac(Now * 0.9f) < 0.65f) Label(TEXT("Press ENTER or throw a punch"), 0.5f, 0.80f, 1.0f, Lilac);
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
	Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.02f, 0.f, 0.05f, 0.55f));
	Plate(0.30f, 0.02f, 0.40f, 0.085f);
	Label(FString::Printf(TEXT("CALIBRATION   STEP %d OF %d"), G.CalStep + 1, AKSGameMode::CalSteps), 0.5f, 0.061f, 1.15f, FLinearColor::White);

	/* ---- the mirror: what the camera sees of you, flipped like a mirror ---- */
	const float BX = 0.04f, BY = 0.15f, BW = 0.40f, BH = 0.66f;
	Panel(BX - 0.012f, BY - 0.02f, BW + 0.024f, BH + 0.04f);
	// Where the shoulders must be above, and the elbows inside.
	Fill(BX, BY + BH * 0.52f, BW, 0.003f, Faded(Lilac, 0.5f));
	Label(TEXT("shoulders above this line"), BX + 0.012f, BY + BH * 0.52f - 0.018f, 0.7f, Lilac, -1);
	Fill(BX, BY + BH * 0.95f, BW, 0.003f, Faded(Lilac, 0.5f));
	Label(TEXT("elbows above this line"), BX + 0.012f, BY + BH * 0.95f - 0.018f, 0.7f, Lilac, -1);
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
	else if (!G.bCalSteady[0]) Label(TEXT("no body in view"), BX + BW * 0.5f, BY + BH * 0.4f, 1.0f, Muted);   // not for a single lost frame: that made the words blink
	Label(TEXT("YOU, AS THE CAMERA SEES YOU"), BX + BW * 0.5f, BY + BH + 0.045f, 0.7f, Lilac);

	/* ---- what to do ---- */
	const float RX = 0.72f;
	Panel(0.47f, 0.13f, 0.50f, 0.24f);
	Text(G.CalTitle, RX, 0.195f, 44.f, Yellow, 0, EKSFont::Comic, 2.6f, FLinearColor::Black);
	Label(G.CalHint, RX, 0.262f, 1.05f, G.CalHint.Contains(TEXT("STEP")) || G.CalHint.Contains(TEXT("MOVE")) || G.CalHint.Contains(TEXT("COME")) ? Red : Lilac);
	Fill(RX - 0.2f, 0.305f, 0.40f, 0.024f, FLinearColor(0.15f, 0.08f, 0.25f, 0.9f));
	Fill(RX - 0.2f, 0.305f, 0.40f * FMath::Clamp(G.CalProgress, 0.f, 1.f), 0.024f, Green);
	Fill(RX - 0.2f, 0.305f, 0.40f * FMath::Clamp(G.CalProgress, 0.f, 1.f), 0.008f, FLinearColor(1.f, 1.f, 1.f, 0.4f));
	if (G.CalCountNeed > 0)
	{
		Pic(TEXT("burst_hit"), RX, 0.50f, 0.25f);
		Text(FString::Printf(TEXT("%d / %d"), G.CalCountNow, G.CalCountNeed), RX, 0.495f, 58.f, G.CalCountNow >= G.CalCountNeed ? FLinearColor(0.f, 0.5f, 0.1f) : FLinearColor(0.95f, 0.08f, 0.05f), 0, EKSFont::Comic, 2.6f, FLinearColor::Black);
	}
	if (G.CalChecks.Num()) Panel(0.47f, 0.645f, 0.50f, 0.045f * G.CalChecks.Num() + 0.06f);
	float Y = 0.695f;
	for (const TPair<FString, bool>& Check : G.CalChecks)
	{
		Label((Check.Value ? TEXT("OK    ") : TEXT("NO    ")) + Check.Key, 0.50f, Y, 0.9f, Check.Value ? Green : Red, -1);
		Y += 0.045f;
	}
	Label(TEXT("SPACE  skip this step          K  play with the keyboard instead"), 0.5f, 0.945f, 0.85f, Lilac);
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
	case EKSPhase::Street: case EKSPhase::PullIn: case EKSPhase::Ending: DrawStreet(*G); break;
	case EKSPhase::Calibrate: DrawCalibrate(*G); break;
	default: DrawFight(*G); break;
	}

	// Whoever is speaking gets a comic speech bubble by the head.
	const bool bFighters = G->Phase == EKSPhase::Card || G->Phase == EKSPhase::Fight || G->Phase == EKSPhase::FightEnd || G->Phase == EKSPhase::Result || G->Phase == EKSPhase::Continue;
	if (bFighters) SpeechBubble(*G); else BubbleLine.Reset();

	if (G->RedFlash > 0.f) Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(1.f, 0.f, 0.f, 0.35f * G->RedFlash));
	if (G->WhiteFade > 0.f) Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(1.f, 1.f, 1.f, G->WhiteFade));
}
