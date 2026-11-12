// The webcam tracker that runs inside the game: a background thread that reads the camera, finds the player's body
// with the MediaPipe pose model (run by Unreal's ONNX Runtime) and turns it into boxing moves with FKSDetector.
// It needs no browser. The engine's OpenCV plugin supplies the camera and the image handling.
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "KSDetector.h"

class FRunnableThread;
namespace UE::NNE { class IModelInstanceCPU; }

/** What the game reads from the tracker each frame. */
struct FKSTrackerState
{
	bool bCameraOpen = false;
	bool bSeen = false;
	bool bCalibrated = false;
	bool bHandsOutOfView = false;   // the player stands too close: elbows and fists are below the picture
	float CalProgress = -1.f;
	bool Held[5] = { false, false, false, false, false };
	TArray<FKSDetectedPunch> Punches;    // only the ones that arrived since the last Take()
	TArray<FVector> World;               // 33 landmarks in metres, or empty
	TArray<FVector2f> Image;             // 33 landmarks in the camera picture (0..1 from the top left), or empty
	TArray<float> StrokePeaks;           // peak speed of the arm strokes that ended since the last Take()
	double LastFrameSeconds = -1000.0;   // FPlatformTime::Seconds() of the newest processed frame
	float FramesPerSecond = 0.f;
	FString Status;                      // short text for the log and the HUD
};

class FKSCameraTracker : public FRunnable
{
public:
	/** CameraIndex < 0 tries cameras 0 to 3 and takes the first that delivers a picture. */
	FKSCameraTracker(TSharedPtr<UE::NNE::IModelInstanceCPU> InModel, int32 InCameraIndex);
	virtual ~FKSCameraTracker() override;

	/** Copies the newest state and hands over the punches thrown since the last call. */
	void Take(FKSTrackerState& Out);
	void Recalibrate() { bWantCalibrate = true; }
	/** 1 is normal; higher accepts slower punches. Set from the player's own punches during calibration. */
	void SetSensitivity(float Value) { Sensitivity = Value; }
	/** Writes a line into the session recording, to label what the player was asked to do. */
	void Mark(const FString& Text);

	virtual uint32 Run() override;
	virtual void Stop() override { bStop = true; }

private:
	void SetStatus(const FString& Text);

	TSharedPtr<UE::NNE::IModelInstanceCPU> Model;
	int32 CameraIndex = -1;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStop { false };
	TAtomic<bool> bWantCalibrate { false };
	TAtomic<float> Sensitivity { 1.f };
	TArray<FString> Marks;               // guarded by Lock
	FCriticalSection Lock;
	FKSTrackerState Shared;
};
