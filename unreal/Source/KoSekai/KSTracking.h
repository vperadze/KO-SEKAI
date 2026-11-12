// Webcam tracking for the game: punch events, held stances and the 33 body landmarks.
// The tracker normally runs inside the game (KSCameraTracker). The older browser page
// (tracking-test/unreal-bridge.html, which runs tracker.js) can still send the same data over local HTTP,
// and is used when the built-in tracker has no camera.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpRouteHandle.h"
#include "HttpResultCallback.h"
#include "KSTypes.h"
#include "KSCameraTracker.h"
#include "Containers/Ticker.h"
#include "KSTracking.generated.h"

struct FHttpServerRequest;
class IHttpRouter;
class UNNEModelData;
namespace UE::NNE { class IModelCPU; }

struct FKSPunchEvent
{
	EKSArm Arm;
	EKSPunch Type;
};

UCLASS()
class UKSTrackingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 Port = 8770;
	static constexpr int32 NumLandmarks = 33;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True while the bridge page is sending frames (last packet under half a second old). */
	bool IsConnected() const;
	/** True when connected, a body is in view and the tracker has finished calibrating. */
	bool IsTracking() const { return IsConnected() && bSeen && bCalibrated; }
	bool IsBodySeen() const { return IsConnected() && bSeen; }
	bool AreHandsOutOfView() const { return IsBodySeen() && bHandsOutOfView; }
	/** Calibration progress 0..1, or -1 when not calibrating. */
	float GetCalibrationProgress() const { return IsConnected() ? CalProgress : -1.f; }

	bool IsHeld(EKSDefense Stance) const;
	/** Returns the punches that arrived since the last call. */
	TArray<FKSPunchEvent> TakePunches();

	/** MediaPipe world landmarks in metres (origin between the hips, x = image right, y = down, z = away from camera). */
	const TArray<FVector>& GetWorldLandmarks() const { return World; }
	bool HasLandmarks() const { return IsBodySeen() && World.Num() == NumLandmarks; }
	/** The same joints in the camera picture: 0..1 from the top left. Empty when no body is seen. */
	const TArray<FVector2f>& GetImageLandmarks() const { return Image; }
	/** Peak speeds of the arm strokes that ended since the last call (see FKSDetector::EndedPeaks). */
	TArray<float> TakeStrokePeaks() { TArray<float> Out = MoveTemp(StrokePeaks); StrokePeaks.Reset(); return Out; }
	/** True when the game has its own tracker thread, whether or not the camera has delivered a frame yet. */
	bool HasBuiltInTracker() const { return CameraTracker.IsValid(); }
	void SetSensitivity(float Value);
	void Mark(const FString& Text);

	/** True when the tracker inside the game has a camera and is delivering frames (no browser involved). */
	bool IsBuiltInCamera() const { return bBuiltInLive; }
	/** A short description of the built-in tracker, for the HUD: camera found or not, frames per second. */
	const FString& GetCameraStatus() const { return CameraStatus; }
	/** Starts the calibration again (stand still in guard). Only affects the built-in tracker. */
	void Recalibrate();

private:
	bool HandlePose(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	void StartBuiltInTracker();
	bool PullBuiltInTracker(float DeltaSeconds);

	UPROPERTY() TObjectPtr<UNNEModelData> PoseModelData;
	TSharedPtr<UE::NNE::IModelCPU> PoseModel;
	TUniquePtr<FKSCameraTracker> CameraTracker;
	FTSTicker::FDelegateHandle TickHandle;
	bool bBuiltInLive = false;
	FString CameraStatus;

	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle Route;
	double LastPacketTime = -1000.0;
	bool bSeen = false;
	bool bCalibrated = false;
	bool bHandsOutOfView = false;
	float CalProgress = -1.f;
	bool Held[5] = { false, false, false, false, false };
	TArray<FKSPunchEvent> Pending;
	TArray<FVector> World;
	TArray<FVector2f> Image;
	TArray<float> StrokePeaks;
};
