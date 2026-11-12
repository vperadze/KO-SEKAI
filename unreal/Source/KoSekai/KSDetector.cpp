#include "KSDetector.h"

namespace
{
	// Tuning knobs, the same as TUNE in tracker.js. Distances are in shoulder widths, speeds in shoulder widths per second.
	// StrokeEnd is 0.5 here (0.35 in tracker.js): a punch is called as soon as the arm has clearly slowed, not when it has stopped.
	const float StrokeStart = 2.0f, StrokePeak = 3.5f, StrokeEnd = 0.5f, MinScore = 0.55f, TwoHand = 0.6f;
	const double StrokeMaxMs = 450, RefractoryMs = 120, HoldMs = 60, CalMs = 800;
	// A punch is called while the arm is still on its way out, as soon as the kind of punch is clear, not after it stops.
	const double EarlyMs = 60;
	const float EarlyScore = 0.38f, EarlyLead = 0.12f;
	const float EvadeOn = 0.45f, EvadeOff = 0.3f, DuckOn = 0.45f, DuckOff = 0.3f, DriftTau = 5.f;

	float Clamp01(float V) { return FMath::Clamp(V, 0.f, 1.f); }
	float Dist(const FVector2f& A, const FVector2f& B) { return FVector2f::Distance(A, B); }
}

void FKSDetector::Reset()
{
	Arms[0] = FArm(); Arms[0].Sh = 11; Arms[0].El = 13; Arms[0].Wr = 15; Arms[0].Sign = -1.f;   // the player's own left
	Arms[1] = FArm(); Arms[1].Sh = 12; Arms[1].El = 14; Arms[1].Wr = 16; Arms[1].Sign = 1.f;
	for (bool& H : Held) H = false;
	bHasBase = bCalibrating = bWantCal = false;
	CalSamples.Reset();
	Ss = 0; LastT = 0; LastFireT = -1e9; CoverSince = 0;
}

float FKSDetector::CalibrationProgress() const
{
	return bCalibrating ? Clamp01((float)((LastT - CalT0) / CalMs)) : -1.f;
}

/* ---------- stroke analysis ---------- */

TArray<FKSDetector::FSample> FKSDetector::WindowOf(const FArm& Arm, double T0)
{
	TArray<FSample> Win;
	for (const FSample& S : Arm.Buf) if (S.T >= T0 - 50) Win.Add(S);
	return Win;
}

FKSDetector::FMotion FKSDetector::Motion(const TArray<FSample>& Win)
{
	FMotion M;
	for (int32 i = 1; i < Win.Num(); i++)
	{
		const float D = FMath::Sqrt(FMath::Square(Win[i].Ax - Win[i - 1].Ax) + FMath::Square(Win[i].Ay - Win[i - 1].Ay));
		M.Path += D; M.MaxStep = FMath::Max(M.MaxStep, D);
	}
	const FSample& A = Win[0]; const FSample& B = Win.Last();
	M.bJumped = M.MaxStep > 0.45f && M.MaxStep > 0.6f * M.Path;
	M.Wrist = FMath::Sqrt(FMath::Square(B.Ax - A.Ax) + FMath::Square(B.Ay - A.Ay));
	M.Head = FMath::Sqrt(FMath::Square(B.Hx - A.Hx) + FMath::Square(B.Hy - A.Hy));
	return M;
}

FKSDetector::FFeats FKSDetector::Feats(const TArray<FSample>& Win)
{
	// Three-frame average, so single-frame jitter does not look like movement. Hand size ignores frames without a hand.
	const int32 N = Win.Num();
	TArray<FSample> Sm; Sm.SetNum(N);
	for (int32 i = 0; i < N; i++)
	{
		const int32 A = FMath::Max(0, i - 1), B = FMath::Min(N - 1, i + 1);
		FSample O; int32 HsN = 0; const float Count = (float)(B - A + 1);
		for (int32 k = A; k <= B; k++)
		{
			const FSample& S = Win[k];
			O.Inn += S.Inn; O.Up += S.Up; O.ElUp += S.ElUp; O.Fx += S.Fx; O.Fy += S.Fy; O.Fl += S.Fl; O.Depth += S.Depth; O.Ang += S.Ang;
			if (S.Hs > 0.f) { O.Hs += S.Hs; HsN++; }
		}
		O.Inn /= Count; O.Up /= Count; O.ElUp /= Count; O.Fx /= Count; O.Fy /= Count; O.Fl /= Count; O.Depth /= Count; O.Ang /= Count;
		O.Hs = HsN ? O.Hs / HsN : 0.f;
		Sm[i] = O;
	}
	const FSample& S1 = Sm.Last();
	// Towards-the-camera signals are noisy, so they are measured from the average of the stroke's first frames.
	float Depth0 = 0, Ang0 = 0, Hs0 = 0; int32 Hs0N = 0; const int32 First = FMath::Min(3, N);
	for (int32 i = 0; i < First; i++) { Depth0 += Sm[i].Depth; Ang0 += Sm[i].Ang; if (Sm[i].Hs > 0.f) { Hs0 += Sm[i].Hs; Hs0N++; } }
	Depth0 /= First; Ang0 /= First; Hs0 = Hs0N ? Hs0 / Hs0N : 0.f;
	// Sideways and upward travel are measured from the least-extended point inside the stroke, so a wind-up does not hide the punch.
	float MinIn = FLT_MAX, MinUp = FLT_MAX, MaxFl = 0.f;
	for (const FSample& S : Sm) { MinIn = FMath::Min(MinIn, S.Inn); MinUp = FMath::Min(MinUp, S.Up); MaxFl = FMath::Max(MaxFl, S.Fl); }
	const float FSum = S1.Fx + FMath::Abs(S1.Fy) + 1e-6f;
	FFeats F;
	F.DIn = S1.Inn - MinIn; F.DUp = S1.Up - MinUp; F.DDepth = S1.Depth - Depth0; F.DAng = S1.Ang - Ang0;
	F.Grow = S1.Hs > 0.f && Hs0 > 0.f ? S1.Hs / Hs0 : 1.f;
	F.FlRatio = MaxFl > 0.f ? S1.Fl / MaxFl : 1.f;
	F.Horiz = S1.Fx / FSum; F.Vert = FMath::Max(0.f, S1.Fy) / FSum; F.ElUp = S1.ElUp;
	return F;
}

FKSDetector::FScores FKSDetector::Scores(const FFeats& F)
{
	// straight: the fist comes at the camera (it grows, gets nearer, the elbow opens, the forearm looks shorter)
	float Ev[4] = { Clamp01(F.DDepth / 0.25f), Clamp01((F.Grow - 1.f) / 0.35f), Clamp01((F.DAng - 10.f) / 50.f), Clamp01((0.85f - F.FlRatio) / 0.35f) };
	Algo::Sort(Ev, [](float A, float B) { return A > B; });
	float Straight = (Ev[0] + Ev[1]) / 2.f;
	const float FlatArm = Clamp01((F.FlRatio - 0.6f) / 0.3f);   // forearm still looks full length: it swung sideways or upward
	Straight = FMath::Max(0.f, Straight - 0.4f * FlatArm * FMath::Max(Clamp01((F.DIn - 0.5f) / 0.5f), Clamp01((F.DUp - 0.5f) / 0.5f)));
	// hook: the wrist sweeps across the body with the forearm flat and the elbow raised
	const float HookGeo = (Clamp01((F.Horiz - 0.4f) / 0.35f) + Clamp01((F.FlRatio - 0.5f) / 0.3f) + Clamp01((F.ElUp + 0.75f) / 0.45f)) / 3.f;
	const float Hook = Clamp01((F.DIn - 0.15f) / 0.6f) * (0.35f + 0.65f * HookGeo);
	// uppercut: the wrist rises with the forearm upright, mostly upward rather than across
	const float Dom = F.DUp / (F.DUp + FMath::Max(0.f, F.DIn) + 1e-6f);
	const float UpGeo = (Clamp01((F.Vert - 0.45f) / 0.35f) + Clamp01((F.FlRatio - 0.45f) / 0.3f) + Clamp01((Dom - 0.45f) / 0.3f)) / 3.f;
	const float Uppercut = Clamp01((F.DUp - 0.15f) / 0.6f) * (0.35f + 0.65f * UpGeo);
	FScores S; S.Straight = Straight; S.Hook = Hook; S.Uppercut = Uppercut;
	return S;
}

float FKSDetector::MainAxis(EKSPunch Type, const FFeats& F)
{
	return Type == EKSPunch::Hook ? F.DIn : Type == EKSPunch::Uppercut ? F.DUp : FMath::Max(F.DDepth / 0.25f, (F.Grow - 1.f) / 0.35f);
}

/* ---------- one frame ---------- */

bool FKSDetector::Process(const FKSPoseFrame& Frame, TArray<FKSDetectedPunch>& OutPunches, float Sensitivity)
{
	if (!Frame.bSeen)
	{
		for (FArm& A : Arms) { A.Buf.Reset(); A.bStroke = false; }
		for (bool& H : Held) H = false;
		CoverSince = 0; Ss = 0; LastT = 0;
		return false;
	}
	// image-height units, aspect corrected
	FVector2f P[33];
	for (int32 i = 0; i < 33; i++) P[i] = FVector2f(Frame.Image[i].X * Frame.Aspect, Frame.Image[i].Y);
	const FVector2f SL = P[11], SR = P[12], Nose = P[0];
	const float S = Dist(SL, SR);
	if (S < 0.04f) return false;
	const double T = Frame.TimeMs;
	const float Dt = LastT > 0 ? FMath::Min(0.2f, (float)((T - LastT) / 1000.0)) : 0.f;
	LastT = T;
	// Shoulder width shrinks and grows as the torso turns during punches, so a slow average is the yardstick.
	Ss = (Ss > 0.f && Dt > 0.f) ? Ss + (S - Ss) * (1.f - FMath::Exp(-Dt / 0.5f)) : S;
	const FVector2f Mid = (SL + SR) * 0.5f;

	if ((!bHasBase && !bCalibrating) || bWantCal) { bCalibrating = true; CalT0 = T; CalSamples.Reset(); bWantCal = false; }
	if (bCalibrating) { FCalSnap Snap; Snap.Nose = Nose; Snap.Mid = Mid; Snap.S = S; Snap.WL = P[15]; Snap.WR = P[16]; CalStep(T, Snap); }

	FVerdict Results[2]; int32 NumResults = 0;
	for (int32 i = 0; i < 2; i++)
	{
		FVerdict V;
		if (StrokeStep(i, ArmSample(Arms[i], Frame, P, Mid, Nose), T, Sensitivity, V)) Results[NumResults++] = V;
	}
	// If both arms claim a punch on the same frame, the clearer one wins.
	if (NumResults == 2 && Results[0].bPunch && Results[1].bPunch) Results[Results[0].Score >= Results[1].Score ? 1 : 0].bPunch = false;
	for (int32 i = 0; i < NumResults; i++)
	{
		if (!Results[i].bPunch) continue;
		Arms[Results[i].Arm].Cool = T + RefractoryMs; LastFireT = T;
		OutPunches.Add({ Results[i].Arm == 0 ? EKSArm::Left : EKSArm::Right, Results[i].Type });
	}

	if (bHasBase && !bCalibrating) Stances(T, P, Nose, Mid);

	// While standing neutral, let the calibration follow slow drift (stepping a little, leaning back).
	const bool bNeutral = bHasBase && !bCalibrating && !Arms[0].bStroke && !Arms[1].bStroke
		&& !Held[1] && !Held[2] && !Held[3] && !Held[4] && T - LastFireT > 600;
	if (bNeutral && Dt > 0.f)
	{
		const float Al = 1.f - FMath::Exp(-Dt / DriftTau);
		BaseNose += (Nose - BaseNose) * Al; BaseMid += (Mid - BaseMid) * Al; BaseS += (S - BaseS) * Al;
	}
	return true;
}

// Calibration averages a still guard and restarts if you move. Returns true when it completes.
bool FKSDetector::CalStep(double T, const FCalSnap& Snap)
{
	if (CalSamples.Num())
	{
		const FCalSnap& First = CalSamples[0];
		if (Dist(Snap.Nose, First.Nose) / First.S > 0.12f || Dist(Snap.WL, First.WL) / First.S > 0.3f || Dist(Snap.WR, First.WR) / First.S > 0.3f)
		{
			CalSamples.Reset(); CalT0 = T;
		}
	}
	CalSamples.Add(Snap);
	if (T - CalT0 < CalMs) return false;
	const float N = (float)CalSamples.Num();
	BaseNose = BaseMid = FVector2f::ZeroVector; BaseS = BaseWristSep = BaseWristBelowNose = 0.f;
	for (const FCalSnap& C : CalSamples)
	{
		BaseNose += C.Nose / N; BaseMid += C.Mid / N; BaseS += C.S / N;
		BaseWristSep += Dist(C.WL, C.WR) / C.S / N;
		BaseWristBelowNose += ((C.WL.Y + C.WR.Y) / 2.f - C.Nose.Y) / C.S / N;
	}
	bHasBase = true; bCalibrating = false; CalSamples.Reset();
	return true;
}

FKSDetector::FSample FKSDetector::ArmSample(const FArm& A, const FKSPoseFrame& Frame, const FVector2f* P, const FVector2f& Mid, const FVector2f& Head) const
{
	const FVector2f El = P[A.El], Wr = P[A.Wr], Fi = P[A.Wr + 4], Pk = P[A.Wr + 2];
	// elbow angle in 3D: about 50 degrees in guard, 150+ when the arm is straight
	const FVector3f U = Frame.World[A.Sh] - Frame.World[A.El], W = Frame.World[A.Wr] - Frame.World[A.El];
	const float Len = U.Size() * W.Size();
	const float Cos = Len > 1e-9f ? FVector3f::DotProduct(U, W) / Len : 1.f;
	// apparent fist size: grows when the fist comes towards the camera
	const bool bHandOk = Frame.Visibility[A.Wr + 4] > 0.5f && Frame.Visibility[A.Wr + 2] > 0.5f;
	FSample S;
	S.T = Frame.TimeMs;
	S.Inn = (Wr.X - Mid.X) * A.Sign / Ss;      // sideways position, grows as the wrist moves across the body
	S.Up = (Mid.Y - Wr.Y) / Ss;                // height above the shoulder line
	S.ElUp = (Mid.Y - El.Y) / Ss;
	S.Fx = FMath::Abs(Wr.X - El.X) / Ss; S.Fy = (El.Y - Wr.Y) / Ss; S.Fl = Dist(Wr, El) / Ss;   // forearm as seen on screen
	S.Depth = Frame.World[A.Sh].Z - Frame.World[A.Wr].Z;   // metres, positive = fist in front of the shoulder
	S.Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.f, 1.f)));
	S.Hs = bHandOk ? (Dist(Wr, Fi) + Dist(Wr, Pk)) / 2.f / Ss : 0.f;
	S.Ax = Wr.X / Ss; S.Ay = Wr.Y / Ss;        // wrist on screen, for spotting tracking jumps
	S.Hx = Head.X / Ss; S.Hy = Head.Y / Ss;    // head on screen, for spotting whole-body moves
	S.Vis = FMath::Min(Frame.Visibility[A.Wr], Frame.Visibility[A.El]);
	return S;
}

bool FKSDetector::StrokeStep(int32 ArmIndex, const FSample& S, double T, float K, FVerdict& Out)
{
	FArm& A = Arms[ArmIndex];
	A.Buf.Add(S);
	while (A.Buf.Num() && A.Buf[0].T < T - 700) A.Buf.RemoveAt(0, EAllowShrinking::No);
	const FSample* Ref = nullptr;
	for (int32 i = A.Buf.Num() - 2; i >= 0; i--) if (A.Buf[i].T <= T - 45) { Ref = &A.Buf[i]; break; }
	if (!Ref) return false;
	const float Dt = (float)((T - Ref->T) / 1000.0);
	const float GrowRate = S.Hs > 0.f && Ref->Hs > 0.f ? (S.Hs / Ref->Hs - 1.f) / Dt : 0.f;
	A.Act = FMath::Sqrt(FMath::Square(S.Inn - Ref->Inn) + FMath::Square(S.Up - Ref->Up)) / Dt
		+ FMath::Max(0.f, (S.Depth - Ref->Depth) / Dt) / 0.5f + FMath::Max(0.f, GrowRate) + FMath::Max(0.f, (S.Ang - Ref->Ang) / Dt) / 250.f;
	if (!A.bStroke)
	{
		if (T >= A.Cool && A.Act > StrokeStart / K) { A.bStroke = true; A.StrokeT0 = Ref->T; A.StrokePeak = A.Act; }
		return false;
	}
	A.StrokePeak = FMath::Max(A.StrokePeak, A.Act);
	const double Dur = T - A.StrokeT0;
	if (!((A.Act < StrokeEnd * A.StrokePeak && Dur >= 80) || Dur > StrokeMaxMs))
	{
		if (!A.bFired && Dur >= EarlyMs && A.StrokePeak >= StrokePeak / K)
		{
			FVerdict Early;
			if (Judge(ArmIndex, A.StrokeT0, A.StrokePeak, T, K, Early, true) && Early.bPunch) { A.bFired = true; Out = Early; return true; }
		}
		return false;
	}
	A.bStroke = false;
	if (EndedPeaks.Num() < 64) EndedPeaks.Add(A.StrokePeak);
	if (A.bFired) { A.bFired = false; return false; }      // already called on the way out
	return Judge(ArmIndex, A.StrokeT0, A.StrokePeak, T, K, Out, false);
}

// Returns true when the stroke was worth a verdict (Out.bPunch says whether it was a punch).
bool FKSDetector::Judge(int32 ArmIndex, double StrokeT0, float Peak, double T, float K, FVerdict& Out, bool bEarly)
{
	auto Say = [&](const TCHAR* What) { if (Trace && !bEarly) Trace(FString::Printf(TEXT("%.0f arm %d stroke %.0f ms peak %.1f: %s"), T, ArmIndex, T - StrokeT0, Peak, What)); };
	FArm& A = Arms[ArmIndex];
	Out = FVerdict(); Out.Arm = ArmIndex;
	const TArray<FSample> Win = WindowOf(A, StrokeT0);
	if (Win.Num() < 3) { Say(TEXT("rejected, too short")); return false; }
	if (Peak < StrokePeak / K) { Say(TEXT("rejected, too slow")); return false; }
	const FMotion M = Motion(Win);
	if (M.bJumped) { Say(TEXT("rejected, tracking jump")); return true; }
	float MinVis = 1.f;
	for (int32 i = FMath::Max(0, Win.Num() - 3); i < Win.Num(); i++) MinVis = FMath::Min(MinVis, Win[i].Vis);
	if (MinVis < 0.45f) { Say(TEXT("rejected, hand hidden")); return true; }
	// A wrist below the bottom edge of the picture is the model's guess, not a measurement.
	if (Win.Last().Ay * Ss > 1.0f) { Say(TEXT("rejected, fist below the picture")); return true; }
	if (M.Head > 0.3f && M.Head > 0.5f * M.Wrist) { Say(TEXT("rejected, body moved")); return true; }
	const FFeats F = Feats(Win);
	const FScores Sc = Scores(F);
	EKSPunch Best = EKSPunch::Straight; float BestScore = Sc.Straight;
	if (Sc.Hook > BestScore) { Best = EKSPunch::Hook; BestScore = Sc.Hook; }
	if (Sc.Uppercut > BestScore) { Best = EKSPunch::Uppercut; BestScore = Sc.Uppercut; }
	if (bEarly)
	{
		// Part-way through the stroke the scores are still small: call it only when one kind clearly leads.
		const float Second = Best == EKSPunch::Straight ? FMath::Max(Sc.Hook, Sc.Uppercut) : Best == EKSPunch::Hook ? FMath::Max(Sc.Straight, Sc.Uppercut) : FMath::Max(Sc.Straight, Sc.Hook);
		if (BestScore < EarlyScore / K || BestScore < Second + EarlyLead) return true;
	}
	else if (BestScore < FMath::Max(0.25f, FMath::Min(0.9f, MinScore / K)))
	{
		if (Trace) Trace(FString::Printf(TEXT("%.0f arm %d stroke %.0f ms peak %.1f: rejected, unclear (straight %.2f hook %.2f uppercut %.2f)"), T, ArmIndex, T - StrokeT0, Peak, Sc.Straight, Sc.Hook, Sc.Uppercut));
		return true;
	}
	// Raising both hands into a cover-up, or the whole upper body moving, shows up on both arms at once.
	FArm& O = Arms[1 - ArmIndex];
	const TArray<FSample> OW = WindowOf(O, StrokeT0);
	if (OW.Num() >= 2)
	{
		const FMotion OM = Motion(OW);
		const bool bSmoothMove = !OM.bJumped && OW.Last().Vis > 0.5f;
		const float Mine = MainAxis(Best, F), Theirs = MainAxis(Best, Feats(OW));
		if (bSmoothMove && Mine > 0.f && Theirs >= TwoHand * Mine)
		{
			if (bEarly) return true;
			Say(TEXT("rejected, both hands moved"));
			O.bStroke = false; O.bFired = false; O.Cool = T + 150; return true;
		}   // both hands moved
	}
	Out.bPunch = true; Out.Type = Best; Out.Score = BestScore;
	if (Trace) Trace(FString::Printf(TEXT("%.0f arm %d PUNCH %s %s after %.0f ms, peak %.1f (straight %.2f hook %.2f uppercut %.2f)"), T, ArmIndex,
		Best == EKSPunch::Straight ? TEXT("straight") : Best == EKSPunch::Hook ? TEXT("hook") : TEXT("uppercut"), bEarly ? TEXT("early") : TEXT("at end"), T - StrokeT0, Peak, Sc.Straight, Sc.Hook, Sc.Uppercut));
	return true;
}

void FKSDetector::Stances(double T, const FVector2f* P, const FVector2f& Nose, const FVector2f& Mid)
{
	const FVector2f WL = P[15], WR = P[16];
	const float B = BaseS, S = Ss;
	const float Lean = 0.6f * (Nose.X - BaseNose.X) / B + 0.4f * (Mid.X - BaseMid.X) / B;   // + = the player's own left
	const float Drop = 0.6f * (Nose.Y - BaseNose.Y) / B + 0.4f * (Mid.Y - BaseMid.Y) / B;
	const bool bOn = Held[(int32)EKSDefense::Block];
	// Block = both fists held up at face height in front of the head, forearms upright. This is judged on its own,
	// not against the calibration pose: a player who calibrated with the hands already up could otherwise never block.
	auto FistUp = [&](const FVector2f& W, const FVector2f& El)
	{
		return FMath::Abs(W.X - Nose.X) / S < (bOn ? 1.1f : 0.9f) && (W.Y - Nose.Y) / S < (bOn ? 0.62f : 0.45f) && W.Y < El.Y;
	};
	const float Sep = Dist(WL, WR) / S;
	const bool bPunching = Arms[0].bStroke || Arms[1].bStroke;
	const bool bCover = FistUp(WL, P[13]) && FistUp(WR, P[14]) && Sep < (bOn ? 1.6f : 1.3f) && (bOn || !bPunching);
	if (bCover) { if (CoverSince == 0) CoverSince = T; } else CoverSince = 0;
	const bool State[5] = {
		false,
		bCover && (bOn || T - CoverSince >= HoldMs),
		Lean > (Held[(int32)EKSDefense::EvadeLeft] ? EvadeOff : EvadeOn),
		-Lean > (Held[(int32)EKSDefense::EvadeRight] ? EvadeOff : EvadeOn),
		Drop > (Held[(int32)EKSDefense::Duck] ? DuckOff : DuckOn)
	};
	for (int32 i = 1; i < 5; i++)
	{
		if (State[i] && !Held[i]) LastFireT = T;
		if (State[i] != Held[i] && Trace) Trace(FString::Printf(TEXT("%.0f stance %d %s"), T, i, State[i] ? TEXT("ON") : TEXT("off")));
		Held[i] = State[i];
	}
}
