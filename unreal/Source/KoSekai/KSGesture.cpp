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

/* ================= both hands up or down: the special ability ================= */

namespace
{
	const double HandsWindow = 0.40;        // the move is made within this long
	const float HandsTravel = 0.9f;         // and takes each wrist this many shoulder widths up or down
	const double HandsQuiet = 1.0;          // nothing more for this long after one
}

void FKSBothHands::Reset()
{
	Trail.Reset(); LastRaw[0] = LastRaw[1] = FVector2f::ZeroVector; Shoulders = 0.f; QuietUntil = 0.0; Seen = 0;
}

bool FKSBothHands::Update(const TArray<FVector2f>& Image, float Aspect, double Seconds)
{
	if (Image.Num() < 17) { Trail.Reset(); return false; }
	const float Width = FVector2f::Distance(FVector2f(Image[11].X * Aspect, Image[11].Y), FVector2f(Image[12].X * Aspect, Image[12].Y));
	if (Width < 0.02f) return false;
	const FVector2f RawL = Image[15], RawR = Image[16];
	if (RawL == LastRaw[0] && RawR == LastRaw[1]) return false;      // the camera has no new picture yet
	LastRaw[0] = RawL; LastRaw[1] = RawR;
	Shoulders = Shoulders > 0.f ? FMath::Lerp(Shoulders, Width, 0.05f) : Width;
	// A hand at the edge of the picture is a guess of the pose model's: not followed.
	for (const FVector2f& Raw : { RawL, RawR }) if (Raw.X < 0.02f || Raw.X > 0.98f || Raw.Y < 0.02f || Raw.Y > 0.98f) { Trail.Reset(); return false; }
	const float ShoulderY = (Image[11].Y + Image[12].Y) * 0.5f;
	const FPoint Now{ Seconds, (RawL.Y - ShoulderY) / Shoulders, (RawR.Y - ShoulderY) / Shoulders };
	// A jump of the tracking is not a move of the hands.
	if (Trail.Num() && (FMath::Abs(Trail.Last().L - Now.L) > 0.7f || FMath::Abs(Trail.Last().R - Now.R) > 0.7f || Seconds - Trail.Last().T > 0.15)) Trail.Reset();
	Trail.Add(Now);
	while (Trail.Num() && Trail[0].T < Seconds - HandsWindow) Trail.RemoveAt(0, EAllowShrinking::No);
	if (Trail.Num() < 4 || Seconds < QuietUntil) return false;
	for (const FPoint& Then : Trail)
	{
		const float L = Now.L - Then.L, R = Now.R - Then.R;
		if (L * R > 0.f && FMath::Min(FMath::Abs(L), FMath::Abs(R)) >= HandsTravel)
		{
			Seen++; QuietUntil = Seconds + HandsQuiet; Trail.Reset();
			return true;
		}
	}
	return false;
}

FString FKSBothHands::SelfTest()
{
	// A body in the middle of the picture, shoulders a fifth of it apart, fists in guard just under the shoulders'
	// height. Each case holds still for a third of a second, then moves for a quarter. Distances are parts of the
	// picture's height: a shoulder width is 0.27 of it here.
	auto Run = [](float LeftBy, float RightBy, float BodyBy)
	{
		FKSBothHands Hands;
		TArray<FVector2f> Image; Image.SetNumZeroed(33);
		int32 Fired = 0;
		for (int32 Frame = 0; Frame < 30; Frame++)
		{
			const float Along = FMath::Clamp((Frame - 10) / 8.f, 0.f, 1.f), Shake = (Frame % 2) * 0.001f;
			const float Body = BodyBy * Along;
			Image[11] = FVector2f(0.6f, 0.40f + Body); Image[12] = FVector2f(0.4f, 0.40f + Body);
			Image[15] = FVector2f(0.57f + Shake, 0.45f + Body + LeftBy * Along); Image[16] = FVector2f(0.43f + Shake, 0.45f + Body + RightBy * Along);
			Fired += Hands.Update(Image, 4.f / 3.f, 100.0 + Frame / 30.0) ? 1 : 0;
		}
		return Fired;
	};
	return FString::Printf(TEXT("up=%d down=%d duck=%d one_hand=%d guard_to_block=%d opposite=%d"),
		Run(-0.30f, -0.30f, 0.f), Run(0.30f, 0.30f, 0.f), Run(0.f, 0.f, 0.30f), Run(-0.30f, 0.f, 0.f), Run(-0.12f, -0.12f, 0.f), Run(-0.30f, 0.30f, 0.f));
}
