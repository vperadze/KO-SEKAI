// Receives webcam tracking from the browser (tracking-test/unreal-bridge.html, which runs tracker.js)
// over local HTTP and exposes it to the game: punch events, held stances and the 33 body landmarks.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HttpRouteHandle.h"
#include "HttpResultCallback.h"
#include "KSTypes.h"
#include "KSTracking.generated.h"

struct FHttpServerRequest;
class IHttpRouter;

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
	/** Calibration progress 0..1, or -1 when not calibrating. */
	float GetCalibrationProgress() const { return IsConnected() ? CalProgress : -1.f; }

	bool IsHeld(EKSDefense Stance) const;
	/** Returns the punches that arrived since the last call. */
	TArray<FKSPunchEvent> TakePunches();

	/** MediaPipe world landmarks in metres (origin between the hips, x = image right, y = down, z = away from camera). */
	const TArray<FVector>& GetWorldLandmarks() const { return World; }
	bool HasLandmarks() const { return IsBodySeen() && World.Num() == NumLandmarks; }

private:
	bool HandlePose(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle Route;
	double LastPacketTime = -1000.0;
	bool bSeen = false;
	bool bCalibrated = false;
	float CalProgress = -1.f;
	bool Held[5] = { false, false, false, false, false };
	TArray<FKSPunchEvent> Pending;
	TArray<FVector> World;
};
