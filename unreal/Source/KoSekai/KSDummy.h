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
class USkeletalMeshComponent;
class UAnimSequence;
class UPointLightComponent;
class UPoseableMeshComponent;

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

	/* ---- real fighters ---- */
	/** Swaps the placeholder shapes for the fighter in /Game/KoSekai/Characters/<Folder>. Returns false if it is missing. */
	bool UseCharacter(const FString& Folder);
	bool HasCharacter() const { return bCharacter; }
	/** Plays one of the fighter's clips by name ("Combat_Stance", "Left_Jab_from_Guard", ...). */
	void PlayClip(const FString& Clip, bool bLoop, float Rate = 1.f, float StartSeconds = 0.f);
	void SetClipRate(float Rate);
	/** The fighter's mood: the first of these faces that the fighter has ("hurt", "ko", "dizzy", ...); an empty list is
	 *  no mood. The face on screen is worked out every frame from the mood, speech, attacking and blinking (UpdateFace). */
	void SetFace(const TArray<FString>& Candidates);
	/** True while the fighter is throwing a punch: the face snarls. */
	void SetAttacking(bool bNow) { bAttacking = bNow; }
	/** Says one of the fighter's lines whose key starts with Prefix ("intro", "hit", "hurt", "win", "lose"), and moves
	 *  the mouth with it. Returns the line's length in seconds, 0 if the fighter has no such line. */
	float Say(const TCHAR* Prefix, float Volume = 1.f);
	void StopTalking();
	bool IsTalking() const { return Spoken != nullptr; }
	/** The words being said now, for the subtitle; empty when silent. */
	FString SpokenText() const;
	/** For the self-test: how many different faces were shown, and how many times the mouth opened, since the last call. */
	void TakeFaceStats(int32& OutFaces, int32& OutMouthOpens);
	/** Sideways lean (cm, +Y is the fighter's right) and vertical squash (1 = standing), eased over a few frames. */
	void SetBodyShift(float SideCm, float Squash);
	/** Makes the fighter's arms copy the player's real arms (33 MediaPipe world landmarks, metres); the rest of the
	 *  body keeps playing its clip. Pass nullptr to go back to the clip's own arms. */
	void SetTrackedArms(const TArray<FVector>* Landmarks);
	bool HasTrackedArms() const { return bPuppet; }

	/* ---- contact ---- */
	/** While a punch clip plays: steps and crouches the fighter so that the glove is at Point (world space) on the impact frame. */
	void AimPunch(const FString& Clip, const FVector& Point, float Weight);
	void StopAim() { bAiming = false; }
	/** Centre of a glove in world space. */
	FVector GloveWorld(EKSArm Arm) const;
	/** Front of the face in world space. bHome ignores the fighter's own dodge and duck, so a dodged punch really misses. */
	FVector FaceWorld(bool bHome) const;
	/** Points all over the body in world space (every bone of a real fighter), for the HUD to know where on screen the fighter is. */
	void BodyPoints(TArray<FVector>& Out) const;
	/** Where the left hand is DRAWN, measured from the head. For the self-test: a body frozen in one pose gives a constant. */
	FVector DrawnHandFromHead() const;
	/** A shove backwards (cm) that springs back, used when a punch is blocked. */
	void Knock(float Cm) { KnockNow += Cm; }
	/** A punch landed on this fighter: the body is thrown by it and wobbles back like a spring. A straight snaps the
	 *  head back, a hook throws the fighter sideways (away from the attacker's punching arm), an uppercut lifts them. */
	void TakeHit(EKSPunch Type, EKSArm AttackerArm, float Strength);
	/** Freezes the pose for a moment, the way arcade fighters do on impact. */
	void HitStop(float Seconds);

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

	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Skel;
	UPROPERTY() TMap<FString, TObjectPtr<UAnimSequence>> Clips;
	UPROPERTY() TMap<FString, TObjectPtr<UMaterialInterface>> Faces;
	UPROPERTY() TObjectPtr<UMaterialInterface> NormalFace;
	bool bCharacter = false;
	FString FaceShown;
	FString Folder;                        // the fighter's content folder, which is also the name its lines are filed under
	FString Mood;                          // the mood face asked for by the game ("" = none)
	bool bMoodFixed = false;               // knocked out: nothing else moves the face
	bool bAttacking = false;
	float BlinkAt = 2.f, BlinkLeft = 0.f, FaceClock = 0.f;
	const struct FKSVoiceLine* Spoken = nullptr;
	const struct FKSVoiceLine* LastSpoken = nullptr;
	float SpokenT = 0.f;
	bool bMouthOpen = false;
	UPROPERTY() TObjectPtr<class UAudioComponent> VoicePlayer;
	TSet<FString> TestFaces;
	int32 TestMouthOpens = 0;
	UMaterialInterface* FaceMaterial(const TCHAR* Name) const;
	void UpdateFace(float DeltaSeconds);
	void ShowFace(const FString& Name, UMaterialInterface* Material);
	float ShiftTarget = 0.f, ShiftNow = 0.f, SquashTarget = 1.f, SquashNow = 1.f;
	UPROPERTY() TObjectPtr<UPointLightComponent> HitLight;
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> Puppet;         // shown instead of Skel while the arms are tracked
	void PoseArm(bool bLeft);
	TArray<FVector> Tracked;
	bool bPuppet = false;
	TArray<FTransform> ShownPose, BlendFrom;   // the pose on screen last frame, and the pose a new clip is eased in from
	float BlendLeft = 0.f, BlendTime = 0.12f;
	// Footwork (see Footwork()): the clips are standing punches, so the legs are given their own life.
	struct FFoot { FVector Planted = FVector::ZeroVector, From = FVector::ZeroVector, To = FVector::ZeroVector; float StepT = -1.f; bool bPlaced = false; };
	FFoot Feet[2];
	float FootClock = 0.f, LegWeight = 0.f, NextShuffle = 0.f;
	int32 ShuffleFoot = 0;
	FString CurrentClip;
	void Footwork(float DeltaSeconds);
	void SolveLeg(bool bLeft, const FVector& Target, const FQuat& FootRotation);
	TArray<FTransform> PrevShownPose, BlendPrev;   // one frame earlier: gives each bone's speed when a clip changes
	float ShownDt = 0.f, BlendDt = 0.f;
	UPROPERTY() TObjectPtr<USceneComponent> Pivot;                  // at the feet; the hit recoil tilts and moves it
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Burst;             // the flash of impact at the point of contact
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BurstMID;
	FVector RecoilPos = FVector::ZeroVector, RecoilVel = FVector::ZeroVector;   // cm, fighter space
	FVector2D Lean = FVector2D::ZeroVector, LeanVel = FVector2D::ZeroVector;    // degrees: X = backwards, Y = to the right
	float BurstAge = -1.f;
	bool bAiming = false;
	FString AimClip;
	TMap<FString, FVector> ImpactGlove;   // glove position at each punch clip's impact frame, in the fighter's own space
	FVector AimTarget = FVector::ZeroVector, Reach = FVector::ZeroVector;
	float AimWeight = 0.f, ReachSquash = 1.f, KnockNow = 0.f, StopLeft = 0.f, LightLeft = 0.f;

	FKSPose Current, Target;
	float FollowSpeed = 12.f;
	FLinearColor BodyColor, GloveColor, FlashColor;
	float BaseGlow = 0.12f;
	float FlashLeft = 0.f;
	bool bIsGhost = false;
};
