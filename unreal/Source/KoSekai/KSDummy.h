// A placeholder fighter built from engine cylinders and spheres. It has no skeleton or animation assets:
// a pose is just 15 joint positions, and the limbs are stretched between them every frame.
// Poses come from code (the opponent, keyboard play) or straight from the webcam tracker (the player).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KSTypes.h"
#include "KSDummy.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

namespace KSJoint
{
	enum Type { Hip, Neck, Head, ShL, ShR, ElL, ElR, WrL, WrR, HpL, HpR, KnL, KnR, AnL, AnR, Num };
}

/** Joint positions in the fighter's own space, in cm: X forward, Y to the fighter's right, Z up, feet at Z = 0. */
struct FKSPose
{
	FVector J[KSJoint::Num];
};

namespace KSPoses
{
	FKSPose Guard();
	FKSPose Block();
	FKSPose Windup(EKSPunch Punch, EKSArm Arm);
	FKSPose Strike(EKSPunch Punch, EKSArm Arm);
	FKSPose Open();      // guard dropped, dizzy
	FKSPose Hurt();
	FKSPose KO();        // flat on the floor
	FKSPose Victory();
	FKSPose Idle();      // arms by the sides, for the watching crowd

	/** Sets one arm. Offsets are from the shoulder and written for the right arm (+Y = outward); the left is mirrored. */
	void SetArm(FKSPose& Pose, EKSArm Arm, const FVector& Elbow, const FVector& Wrist);
	/** Moves the upper body: sideways (Y), down (Crouch) and backwards (Back). */
	void Lean(FKSPose& Pose, float Y, float Crouch, float Back = 0.f);
	FKSPose Blend(const FKSPose& A, const FKSPose& B, float Alpha);

	/** Replaces the upper body with the tracked one. Landmarks are MediaPipe world landmarks in metres. */
	void ApplyTracked(FKSPose& Pose, const TArray<FVector>& Landmarks);
}

UCLASS()
class AKSDummy : public AActor
{
	GENERATED_BODY()
public:
	AKSDummy();
	virtual void Tick(float DeltaSeconds) override;

	/** bGhost draws the fighter as a see-through glowing figure (used for the player, so the opponent stays visible). */
	void Setup(const FLinearColor& Body, const FLinearColor& Gloves, bool bGhost, float Glow = 0.12f);
	/** Speed is how fast the model follows the target pose (per second; 12 is brisk, 30 is near instant). */
	void SetTarget(const FKSPose& Pose, float Speed = 12.f);
	void Snap(const FKSPose& Pose);
	void Flash(const FLinearColor& Color, float Seconds);
	FVector GetJointWorld(KSJoint::Type Joint) const;

private:
	void Layout();
	void ApplyColors(bool bFlashing);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Bones;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HeadMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Fists[2];
	UPROPERTY() TObjectPtr<UMaterialInterface> SolidMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> GhostMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GloveMID[2];

	FKSPose Current, Target;
	float FollowSpeed = 12.f;
	FLinearColor BodyColor, GloveColor, FlashColor;
	float BaseGlow = 0.12f;
	float FlashLeft = 0.f;
	bool bIsGhost = false;
};
