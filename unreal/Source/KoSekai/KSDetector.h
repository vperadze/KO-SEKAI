// Turns a stream of body poses into boxing moves. A C++ port of the Detector in tracking-test/tracker.js,
// kept line for line where possible so both versions behave the same and can be tuned together.
#pragma once

#include "CoreMinimal.h"
#include "KSTypes.h"

/** One camera frame's worth of pose: 33 MediaPipe landmarks. */
struct FKSPoseFrame
{
	bool bSeen = false;
	double TimeMs = 0.0;          // capture time
	float Aspect = 4.f / 3.f;     // image width / height
	FVector2f Image[33];          // normalised image position (0..1, origin top left)
	float Visibility[33];
	FVector3f World[33];          // metres, origin between the hips, x = image right, y = down, z = away from the camera
};

struct FKSDetectedPunch { EKSArm Arm; EKSPunch Type; };

class FKSDetector
{
public:
	FKSDetector() { Reset(); }
	void Reset();
	void Calibrate() { bWantCal = true; }
	/** Feed one frame. Punches thrown on this frame are appended to OutPunches. Returns false when no body is usable. */
	bool Process(const FKSPoseFrame& Frame, TArray<FKSDetectedPunch>& OutPunches, float Sensitivity = 1.f);

	bool IsCalibrated() const { return bHasBase && !bCalibrating; }
	/** 0..1 while calibrating, -1 otherwise. */
	float CalibrationProgress() const;
	bool IsHeld(EKSDefense Stance) const { return Held[(int32)Stance]; }
	/** One line per decision (punch called, stroke rejected and why, stance on/off), for tuning from recordings. */
	TFunction<void(const FString&)> Trace;
	/** Peak activity of every stroke that has ended since this was last emptied, punch or not. Calibration uses it to learn how hard this player punches. */
	TArray<float> EndedPeaks;

private:
	struct FSample
	{
		double T = 0;
		float Inn = 0, Up = 0, ElUp = 0, Fx = 0, Fy = 0, Fl = 0, Depth = 0, Ang = 0, Hs = 0;
		float Ax = 0, Ay = 0, Hx = 0, Hy = 0, Vis = 1;
	};
	struct FFeats { float DIn = 0, DUp = 0, DDepth = 0, DAng = 0, Grow = 1, FlRatio = 1, Horiz = 0, Vert = 0, ElUp = 0; };
	struct FScores { float Straight = 0, Hook = 0, Uppercut = 0; };
	struct FMotion { float Path = 0, MaxStep = 0, Wrist = 0, Head = 0; bool bJumped = false; };
	struct FArm
	{
		int32 Sh = 0, El = 0, Wr = 0;
		float Sign = 1.f;
		TArray<FSample> Buf;
		bool bStroke = false, bFired = false;
		double StrokeT0 = 0, Cool = 0;
		float StrokePeak = 0, Act = 0;
	};
	struct FVerdict { int32 Arm = 0; bool bPunch = false; EKSPunch Type = EKSPunch::Straight; float Score = 0; };
	struct FCalSnap { FVector2f Nose, Mid, WL, WR; float S = 0; };

	static TArray<FSample> WindowOf(const FArm& Arm, double T0);
	static FMotion Motion(const TArray<FSample>& Win);
	static FFeats Feats(const TArray<FSample>& Win);
	static FScores Scores(const FFeats& F);
	static float MainAxis(EKSPunch Type, const FFeats& F);

	bool CalStep(double T, const FCalSnap& Snap);
	FSample ArmSample(const FArm& Arm, const FKSPoseFrame& Frame, const FVector2f* P, const FVector2f& Mid, const FVector2f& Head) const;
	bool StrokeStep(int32 ArmIndex, const FSample& S, double T, float K, FVerdict& Out);
	bool Judge(int32 ArmIndex, double StrokeT0, float StrokePeak, double T, float K, FVerdict& Out, bool bEarly);
	void Stances(double T, const FVector2f* P, const FVector2f& Nose, const FVector2f& Mid);

	FArm Arms[2];
	bool Held[5];
	// calibration
	bool bHasBase = false, bCalibrating = false, bWantCal = false;
	double CalT0 = 0;
	TArray<FCalSnap> CalSamples;
	FVector2f BaseNose, BaseMid;
	float BaseS = 0, BaseWristSep = 0, BaseWristBelowNose = 0;
	// running state
	float Ss = 0;                 // slow average of the shoulder width, the yardstick for everything else
	double LastT = 0, LastFireT = -1e9, CoverSince = 0;
};
