#include "KSHUD.h"
#include "KSGameMode.h"
#include "KSTracking.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

using namespace KSColors;

void AKSHUD::Label(const FString& Text, float X, float Y, float Size, const FLinearColor& Color, int32 Align)
{
	if (Text.IsEmpty()) return;
	UFont* Font = GEngine->GetLargeFont();
	const float Scale = Size * Unit;
	float TW = 0.f, TH = 0.f;
	GetTextSize(Text, TW, TH, Font, Scale);
	const float PX = X * SW - (Align == 0 ? TW * 0.5f : Align > 0 ? TW : 0.f), PY = Y * SH - TH * 0.5f;
	const float Off = FMath::Max(1.f, 2.f * Unit * FMath::Min(Size, 2.f));
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.85f), PX + Off, PY + Off, Font, Scale);
	DrawText(Text, Color, PX, PY, Font, Scale);
}

void AKSHUD::Fill(float X, float Y, float W, float H, const FLinearColor& Color)
{
	DrawRect(Color, X * SW, Y * SH, W * SW, H * SH);
}

void AKSHUD::HealthBar(float X, float W, float Fraction, float Trail, const FLinearColor& Color, bool bRightToLeft)
{
	const float Y = 0.062f, H = 0.03f;
	Fill(X - 0.003f, Y - 0.005f, W + 0.006f, H + 0.01f, FLinearColor(0.f, 0.f, 0.f, 0.75f));
	Fill(X, Y, W, H, FLinearColor(0.13f, 0.06f, 0.24f, 0.9f));
	auto Part = [&](float F, const FLinearColor& C) { F = FMath::Clamp(F, 0.f, 1.f); Fill(bRightToLeft ? X + W * (1.f - F) : X, Y, W * F, H, C); };
	Part(Trail, FLinearColor(1.f, 1.f, 1.f, 0.85f));
	Part(Fraction, Fraction < 0.25f ? Red : Color);
}

void AKSHUD::Subtitles(const AKSGameMode& G)
{
	if (!G.Subtitle.IsEmpty())
	{
		Fill(0.f, 0.775f, 1.f, 0.09f, FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Label(G.Subtitle, 0.5f, 0.82f, 1.25f, Lilac);
	}
	if (!G.Prompt.IsEmpty())
	{
		const float Pulse = 0.65f + 0.35f * FMath::Sin(GetWorld()->GetTimeSeconds() * 5.f);
		Label(G.Prompt, 0.5f, 0.915f, 1.0f, FLinearColor(Yellow.R, Yellow.G, Yellow.B, Pulse));
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
	Label(Status, 0.985f, 0.975f, 0.8f, Color, 1);
	// Too close to the camera is the commonest reason punches are missed, so say it where it cannot be overlooked.
	if (T->AreHandsOutOfView())
	{
		Label(TEXT("STEP BACK"), 0.5f, 0.30f, 2.2f, Yellow, 0);
		Label(TEXT("the camera must see your elbows and fists"), 0.5f, 0.36f, 0.9f, Yellow, 0);
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

void AKSHUD::DrawStreet(const AKSGameMode& G)
{
	if (G.Phase == EKSPhase::Street && G.PhaseTime < 7.f)
	{
		const float A = FMath::Clamp(FMath::Min(G.PhaseTime / 1.2f, (7.f - G.PhaseTime) / 1.5f), 0.f, 1.f);
		Label(TEXT("KO-SEKAI"), 0.503f, 0.283f, 6.0f, FLinearColor(Cyan.R, Cyan.G, Cyan.B, A));
		Label(TEXT("KO-SEKAI"), 0.5f, 0.28f, 6.0f, FLinearColor(Magenta.R, Magenta.G, Magenta.B, A));
		Label(TEXT("THE GAME THAT WAS NEVER RELEASED"), 0.5f, 0.40f, 1.2f, FLinearColor(Lilac.R, Lilac.G, Lilac.B, A));
	}
	Subtitles(G);
	DrawTracker(G, false);
}

void AKSHUD::DrawFight(const AKSGameMode& G)
{
	const FKSFighterDef& F = G.Fighter();
	const float Now = G.Clock;

	if (G.Phase == EKSPhase::Card && G.PhaseTime < 2.6f)
	{
		const float A = FMath::Clamp(FMath::Min(G.PhaseTime / 0.3f, (2.6f - G.PhaseTime) / 0.3f), 0.f, 1.f);
		Fill(0.f, 0.f, 1.f, 0.16f, FLinearColor(0.f, 0.f, 0.f, 0.8f * A));
		Fill(0.f, 0.60f, 1.f, 0.40f, FLinearColor(0.f, 0.f, 0.f, 0.8f * A));
		Label(FString::Printf(TEXT("CUT FIGHTER %d OF %d"), G.FighterIndex + 1, G.NumFights()), 0.5f, 0.08f, 1.3f, FLinearColor(Cyan.R, Cyan.G, Cyan.B, A));
		Label(F.Name, 0.5f, 0.70f, 4.2f, FLinearColor(Yellow.R, Yellow.G, Yellow.B, A));
		Label(F.Tagline, 0.5f, 0.81f, 1.3f, FLinearColor(Lilac.R, Lilac.G, Lilac.B, A));
		Label(FString::Printf(TEXT("YOU FIGHT AS %s"), G.PlayerFighter().Name), 0.5f, 0.88f, 1.1f, FLinearColor(Cyan.R, Cyan.G, Cyan.B, A));
		const bool bTracked = G.Tracking() && G.Tracking()->IsConnected();
		Label(bTracked ? TEXT("PUNCH, BLOCK, LEAN AND DUCK FOR REAL - THE CABINET IS WATCHING")
			: TEXT("A/Q/Z left straight/hook/uppercut   D/E/C right   SPACE block   ARROWS evade + duck   P pause"), 0.5f, 0.91f, 0.9f, FLinearColor(Muted.R, Muted.G, Muted.B, A));
		DrawTracker(G, true);
		return;
	}

	/* health, names, clock */
	HealthBar(0.04f, 0.38f, G.PlayerHP / 100.f, G.PlayerTrail / 100.f, Cyan, false);
	HealthBar(0.58f, 0.38f, G.OppHP / G.OppMaxHP, G.OppTrail / G.OppMaxHP, Magenta, true);
	Label(TEXT("YOU"), 0.04f, 0.035f, 1.0f, Cyan, -1);
	Label(F.Name, 0.96f, 0.035f, 1.0f, Magenta, 1);
	const int32 Secs = FMath::CeilToInt(FMath::Max(0.f, G.TimeLeft));
	Label(FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60), 0.5f, 0.075f, 1.6f, Secs <= 10 ? Red : Lilac);

	if (G.Phase == EKSPhase::Fight)
	{
		if (!G.CueMain.IsEmpty())
		{
			Label(G.CueMain, 0.5f, 0.19f, 2.4f, G.bCueGood ? Green : Yellow);
			Label(G.CueHint, 0.5f, 0.25f, 1.0f, Lilac);
		}
		if (G.Combo > 1) Label(FString::Printf(TEXT("COMBO x%d"), G.Combo), 0.96f, 0.13f, 1.1f, Cyan, 1);
	}
	for (const FKSPopup& P : G.Popups)
	{
		const float U = (Now - P.Start) / P.Life;
		Label(P.Text, P.Pos.X, P.Pos.Y - 0.06f * U, P.bBig ? 2.6f : 1.5f, FLinearColor(P.Color.R, P.Color.G, P.Color.B, 1.f - U * U));
	}
	if (Now < G.AnnounceUntil) Label(G.Announcement, 0.5f, 0.42f, 5.0f, Yellow);

	if (G.Phase == EKSPhase::Result)
	{
		Fill(0.2f, 0.22f, 0.6f, 0.56f, FLinearColor(0.03f, 0.01f, 0.07f, 0.88f));
		Label(G.bLastResultDecision ? TEXT("YOU WIN ON POINTS") : TEXT("YOU WIN"), 0.5f, 0.29f, 3.0f, Green);
		Label(F.DefeatLine, 0.5f, 0.38f, 1.05f, Lilac);
		const FKSStats& S = G.Stats;
		Label(FString::Printf(TEXT("LANDED %d     COUNTERS %d     DODGES %d     BLOCKS %d"), S.Landed, S.Counters, S.Dodges, S.Blocks), 0.5f, 0.48f, 1.0f, Cyan);
		Label(FString::Printf(TEXT("HITS TAKEN %d     BEST COMBO %d     HEALTH LEFT %d"), S.Taken, S.BestCombo, FMath::RoundToInt(G.PlayerHP)), 0.5f, 0.54f, 1.0f, Cyan);
		const bool bLast = G.FighterIndex + 1 >= G.NumFights();
		Label(bLast ? TEXT("The screen cracks. There is a way out.") : FString::Printf(TEXT("NEXT: %s"), G.LadderFighter(G.FighterIndex + 1).Name), 0.5f, 0.63f, 1.3f, Yellow);
		Label(TEXT("Press ENTER or throw a punch"), 0.5f, 0.72f, 1.0f, Lilac);
	}
	if (G.Phase == EKSPhase::Continue)
	{
		Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.f, 0.f, 0.f, G.BlackFade));
		Label(G.bLastResultDecision ? TEXT("LOST ON POINTS") : TEXT("KNOCKED OUT"), 0.5f, 0.30f, 3.4f, Red);
		Label(TEXT("The cabinet keeps you for another round."), 0.5f, 0.40f, 1.2f, Lilac);
		Label(FString::Printf(TEXT("CONTINUE?  %d"), FMath::Max(0, FMath::FloorToInt(G.ContinueLeft))), 0.5f, 0.54f, 4.0f, Yellow);
		Label(TEXT("Press ENTER or throw a punch"), 0.5f, 0.66f, 1.0f, Lilac);
	}
	if (G.bPaused)
	{
		Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Label(TEXT("PAUSED"), 0.5f, 0.45f, 4.0f, Lilac);
		Label(TEXT("Press P to carry on"), 0.5f, 0.55f, 1.0f, Muted);
	}
	DrawTracker(G, true);
}

void AKSHUD::DrawCalibrate(const AKSGameMode& G)
{
	const UKSTrackingSubsystem* T = G.Tracking();
	Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(0.02f, 0.f, 0.05f, 0.55f));
	Fill(0.f, 0.f, 1.f, 0.125f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Label(FString::Printf(TEXT("CALIBRATION   STEP %d OF %d"), G.CalStep + 1, AKSGameMode::CalSteps), 0.5f, 0.06f, 1.4f, Cyan);

	/* ---- the mirror: what the camera sees of you, flipped like a mirror ---- */
	const float BX = 0.04f, BY = 0.15f, BW = 0.40f, BH = 0.66f;
	Fill(BX, BY, BW, BH, FLinearColor(0.05f, 0.02f, 0.10f, 0.9f));
	// Where the shoulders must be above, and the elbows inside.
	Fill(BX, BY + BH * 0.52f, BW, 0.003f, FLinearColor(Lilac.R, Lilac.G, Lilac.B, 0.5f));
	Label(TEXT("shoulders above this line"), BX + 0.008f, BY + BH * 0.52f - 0.018f, 0.8f, Lilac, -1);
	Fill(BX, BY + BH * 0.95f, BW, 0.003f, FLinearColor(Lilac.R, Lilac.G, Lilac.B, 0.5f));
	Label(TEXT("elbows above this line"), BX + 0.008f, BY + BH * 0.95f - 0.018f, 0.8f, Lilac, -1);
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
	else Label(TEXT("no body in view"), BX + BW * 0.5f, BY + BH * 0.4f, 1.0f, Muted);
	Label(TEXT("YOU, AS THE CAMERA SEES YOU"), BX + BW * 0.5f, BY + BH + 0.025f, 0.7f, Muted);

	/* ---- what to do ---- */
	const float RX = 0.72f;
	Fill(0.47f, 0.15f, 0.50f, 0.20f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Label(G.CalTitle, RX, 0.20f, 1.9f, Yellow);
	Label(G.CalHint, RX, 0.265f, 1.25f, G.CalHint.Contains(TEXT("STEP")) || G.CalHint.Contains(TEXT("MOVE")) || G.CalHint.Contains(TEXT("COME")) ? Red : Lilac);
	Fill(RX - 0.2f, 0.31f, 0.40f, 0.022f, FLinearColor(0.15f, 0.08f, 0.25f, 0.9f));
	Fill(RX - 0.2f, 0.31f, 0.40f * FMath::Clamp(G.CalProgress, 0.f, 1.f), 0.022f, Green);
	if (G.CalCountNeed > 0) Label(FString::Printf(TEXT("%d / %d"), G.CalCountNow, G.CalCountNeed), RX, 0.43f, 3.2f, G.CalCountNow >= G.CalCountNeed ? Green : Cyan);
	if (G.CalChecks.Num()) Fill(0.47f, 0.66f, 0.50f, 0.045f * G.CalChecks.Num() + 0.02f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	float Y = 0.69f;
	for (const TPair<FString, bool>& Check : G.CalChecks)
	{
		Label((Check.Value ? TEXT("OK    ") : TEXT("NO    ")) + Check.Key, 0.49f, Y, 1.0f, Check.Value ? Green : Red, -1);
		Y += 0.045f;
	}
	Label(TEXT("SPACE  skip this step          K  play with the keyboard instead"), 0.5f, 0.94f, 0.95f, Lilac);
}

void AKSHUD::DrawHUD()
{
	Super::DrawHUD();
	const AKSGameMode* G = GetWorld() ? Cast<AKSGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	if (!G || !Canvas) return;
	SW = Canvas->SizeX; SH = Canvas->SizeY; Unit = SH / 720.f;

	switch (G->Phase)
	{
	case EKSPhase::Street: case EKSPhase::PullIn: case EKSPhase::Ending: DrawStreet(*G); break;
	case EKSPhase::Calibrate: DrawCalibrate(*G); break;
	default: DrawFight(*G); break;
	}

	// The subtitle of whoever is speaking.
	if (!G->SpokenLine.IsEmpty() && G->Phase != EKSPhase::Street && G->Phase != EKSPhase::PullIn && G->Phase != EKSPhase::Ending && G->Phase != EKSPhase::Calibrate)
	{
		const float Y = G->Phase == EKSPhase::Card && G->PhaseTime < 2.6f ? 0.955f : 0.86f;
		Fill(0.12f, Y - 0.032f, 0.76f, 0.064f, FLinearColor(0.f, 0.f, 0.f, 0.62f));
		Label(G->SpokenBy + TEXT(":  \"") + G->SpokenLine + TEXT("\""), 0.5f, Y, 1.05f, FLinearColor::White);
	}

	if (G->RedFlash > 0.f) Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(1.f, 0.f, 0.f, 0.35f * G->RedFlash));
	if (G->WhiteFade > 0.f) Fill(0.f, 0.f, 1.f, 1.f, FLinearColor(1.f, 1.f, 1.f, G->WhiteFade));
}
