#pragma once

#include "CoreMinimal.h"

/** Deliberate hand moves read off the webcam, for the screens that are not a fight (menu, choosing a fighter).
 *
 *  A swipe: a hand travels at least a shoulder's width, mostly along one line, within about a quarter of a second.
 *  Nothing smaller counts: these screens used to take any punch the detector called as a button press, and somebody
 *  settling in front of the camera sets off several a minute (twelve in the first half minute of a recorded session;
 *  this rule fired once on the same recording). The way back does not count either: after a swipe up, the same hand
 *  coming down again within a second is the hand going home. */
class FKSGesture
{
public:
	/** Image: the 33 joints in the camera picture, 0..1 from the top left (empty when nobody is seen). */
	void Update(const TArray<FVector2f>& Image, float Aspect, double Seconds);
	void Reset();
	/** This frame only. StepX: +1 towards the player's own right, -1 left. StepY: +1 down, -1 up. */
	int32 StepX = 0, StepY = 0;
	int32 Swipes = 0;                 // since Reset, for the log

private:
	struct FPoint { double T = 0.0; FVector2f P = FVector2f::ZeroVector; };
	TArray<FPoint> Trail[2];          // each wrist's last half second, in shoulder widths
	FVector2f LastRaw[2] = { FVector2f::ZeroVector, FVector2f::ZeroVector };
	float Shoulders = 0.f;
	double QuietUntil = 0.0;
	double NoUntil[2][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };      // per hand and direction: right, left, up, down
};
