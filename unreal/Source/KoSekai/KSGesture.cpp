#include "KSGesture.h"

namespace
{
	const double WindowSeconds = 0.28;      // a swipe is made within this long
	const float SwipeWidths = 1.0f;         // and covers this many shoulder widths
	const float OneWay = 1.6f;              // along one line: this many times further than across it
	const double QuietSeconds = 0.45;       // nothing more for this long after a swipe
	const double HomeSeconds = 0.9;         // and the opposite way with the same hand not for this long: that is the hand going back
}

void FKSGesture::Reset()
{
	for (int32 h = 0; h < 2; h++) { Trail[h].Reset(); for (double& Until : NoUntil[h]) Until = 0.0; LastRaw[h] = FVector2f::ZeroVector; }
	Shoulders = 0.f; QuietUntil = 0.0; StepX = StepY = 0; Swipes = 0;
}

void FKSGesture::Update(const TArray<FVector2f>& Image, float Aspect, double Seconds)
{
	StepX = StepY = 0;
	if (Image.Num() < 17) { Trail[0].Reset(); Trail[1].Reset(); return; }
	auto Point = [&](int32 Joint) { return FVector2f(Image[Joint].X * Aspect, Image[Joint].Y); };
	const float Width = FVector2f::Distance(Point(11), Point(12));
	if (Width < 0.02f) return;
	Shoulders = Shoulders > 0.f ? FMath::Lerp(Shoulders, Width, 0.05f) : Width;
	static const int32 Wrists[2] = { 15, 16 };      // the player's own left, right
	for (int32 h = 0; h < 2; h++)
	{
		const FVector2f Raw = Image[Wrists[h]];
		if (Raw == LastRaw[h]) continue;               // the camera has no new picture yet
		LastRaw[h] = Raw;
		TArray<FPoint>& T = Trail[h];
		// A hand at the edge of the picture is a guess of the pose model's: not followed.
		if (Raw.X < 0.02f || Raw.X > 0.98f || Raw.Y < 0.02f || Raw.Y > 0.98f) { T.Reset(); continue; }
		const FVector2f Now = Point(Wrists[h]) / Shoulders;
		// A jump of the tracking is not a move of the hand.
		if (T.Num() && (FVector2f::Distance(T.Last().P, Now) > 0.7f || Seconds - T.Last().T > 0.15)) T.Reset();
		T.Add({ Seconds, Now });
		while (T.Num() && T[0].T < Seconds - 0.6) T.RemoveAt(0, EAllowShrinking::No);
		const FPoint* Then = nullptr;
		for (int32 i = T.Num() - 1; i >= 0; i--) if (T[i].T <= Seconds - WindowSeconds) { Then = &T[i]; break; }
		if (!Then || T.Num() < 5 || Seconds < QuietUntil) continue;
		const FVector2f Moved = Now - Then->P;
		const float Across = FMath::Abs(Moved.X), Up = FMath::Abs(Moved.Y);
		if (FMath::Max(Across, Up) < SwipeWidths) continue;
		// A small x in the picture is the player's own right-hand side (the camera is not a mirror).
		int32 Way = -1;
		if (Across > Up * OneWay) Way = Moved.X < 0.f ? 0 : 1;
		else if (Up > Across * OneWay) Way = Moved.Y < 0.f ? 2 : 3;
		if (Way < 0 || Seconds < NoUntil[h][Way]) continue;
		if (Way == 0) StepX = 1; else if (Way == 1) StepX = -1; else if (Way == 2) StepY = -1; else StepY = 1;
		Swipes++;
		QuietUntil = Seconds + QuietSeconds;
		NoUntil[h][Way ^ 1] = Seconds + HomeSeconds;
		T.Reset();
		break;
	}
}
