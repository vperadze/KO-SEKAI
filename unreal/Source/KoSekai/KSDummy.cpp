#include "KSDummy.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "KSImpactData.h"

using namespace KSJoint;

namespace
{
	struct FBoneDef { Type A, B; float Radius; };
	const FBoneDef BoneDefs[] = {
		{ Hip, Neck, 13.f }, { ShL, ShR, 7.f }, { HpL, HpR, 8.f },
		{ ShL, ElL, 5.5f }, { ElL, WrL, 5.f }, { ShR, ElR, 5.5f }, { ElR, WrR, 5.f },
		{ HpL, KnL, 7.5f }, { KnL, AnL, 6.f }, { HpR, KnR, 7.5f }, { KnR, AnR, 6.f },
	};
	const float HeadRadius = 13.f, FistRadius = 9.f;
	const Type Upper[] = { Neck, Head, ShL, ShR, ElL, ElR, WrL, WrR };
}

/* ---------------- poses ---------------- */

void KSPoses::SetArm(FKSPose& P, EKSArm Arm, const FVector& Elbow, const FVector& Wrist)
{
	const bool bRight = Arm == EKSArm::Right;
	const FVector Mirror(1.f, bRight ? 1.f : -1.f, 1.f);
	const FVector Shoulder = P.J[bRight ? ShR : ShL];
	P.J[bRight ? ElR : ElL] = Shoulder + Elbow * Mirror;
	P.J[bRight ? WrR : WrL] = Shoulder + Wrist * Mirror;
}

void KSPoses::Lean(FKSPose& P, float Y, float Crouch, float Back)
{
	for (Type J : Upper) P.J[J] += FVector(-Back, Y, -Crouch);
	for (Type J : { Hip, HpL, HpR }) P.J[J] += FVector(-Back * 0.3f, Y * 0.35f, -Crouch);
	for (Type J : { KnL, KnR }) P.J[J] += FVector(Crouch * 0.4f, Y * 0.15f, -Crouch * 0.35f);
}

FKSPose KSPoses::Blend(const FKSPose& A, const FKSPose& B, float Alpha)
{
	FKSPose Out;
	for (int32 i = 0; i < Num; i++) Out.J[i] = FMath::Lerp(A.J[i], B.J[i], Alpha);
	return Out;
}

FKSPose KSPoses::Guard()
{
	FKSPose P;
	P.J[Hip] = FVector(0, 0, 95);   P.J[Neck] = FVector(5, 0, 148);  P.J[Head] = FVector(9, 0, 168);
	P.J[ShL] = FVector(5, -21, 146); P.J[ShR] = FVector(5, 21, 146);
	P.J[HpL] = FVector(0, -11, 94);  P.J[HpR] = FVector(0, 11, 94);
	P.J[KnL] = FVector(12, -15, 50); P.J[KnR] = FVector(-2, 15, 50);
	P.J[AnL] = FVector(14, -18, 4);  P.J[AnR] = FVector(-14, 18, 4);
	SetArm(P, EKSArm::Left, FVector(10, 4, -23), FVector(27, -8, 2));
	SetArm(P, EKSArm::Right, FVector(10, 4, -23), FVector(27, -8, 2));
	return P;
}

FKSPose KSPoses::Block()
{
	FKSPose P = Guard();
	for (EKSArm Arm : { EKSArm::Left, EKSArm::Right }) SetArm(P, Arm, FVector(18, -3, -20), FVector(24, -14, 17));
	Lean(P, 0, 4, 3);
	return P;
}

FKSPose KSPoses::Windup(EKSPunch Punch, EKSArm Arm)
{
	FKSPose P = Guard();
	const float Side = Arm == EKSArm::Right ? 1.f : -1.f;
	switch (Punch)
	{
	case EKSPunch::Straight: SetArm(P, Arm, FVector(-14, 9, -18), FVector(6, 3, -2)); Lean(P, Side * 3, 0, 5); break;
	case EKSPunch::Hook:     SetArm(P, Arm, FVector(-6, 30, -6), FVector(8, 40, 6)); Lean(P, Side * 10, 0, 3); break;
	case EKSPunch::Uppercut: SetArm(P, Arm, FVector(-8, 9, -27), FVector(10, 5, -46)); Lean(P, Side * 4, 16, 0); break;
	}
	return P;
}

FKSPose KSPoses::Strike(EKSPunch Punch, EKSArm Arm)
{
	FKSPose P = Guard();
	const float Side = Arm == EKSArm::Right ? 1.f : -1.f;
	switch (Punch)
	{
	case EKSPunch::Straight: Lean(P, -Side * 3, 0, -10); SetArm(P, Arm, FVector(32, -6, 3), FVector(66, -17, 8)); break;
	case EKSPunch::Hook:     Lean(P, -Side * 9, 0, -8);  SetArm(P, Arm, FVector(30, 14, 4), FVector(52, -30, 10)); break;
	case EKSPunch::Uppercut: Lean(P, -Side * 2, -4, -9); SetArm(P, Arm, FVector(28, -2, -6), FVector(46, -14, 34)); break;
	}
	return P;
}

FKSPose KSPoses::Open()
{
	FKSPose P = Guard();
	for (EKSArm Arm : { EKSArm::Left, EKSArm::Right }) SetArm(P, Arm, FVector(2, 11, -27), FVector(12, 13, -50));
	Lean(P, 0, 3, -4);
	P.J[Head] += FVector(4, 5, -3);
	return P;
}

FKSPose KSPoses::Hurt()
{
	FKSPose P = Guard();
	Lean(P, 4, 2, 18);
	P.J[Head] += FVector(-9, 3, 0);
	return P;
}

FKSPose KSPoses::KO()
{
	FKSPose P = Open();
	// Fall straight backwards: what was "up" now points behind the fighter.
	for (FVector& J : P.J) J = FVector(-J.Z * 0.95f, J.Y * 1.15f, 13.f + FMath::Max(0.f, J.X) * 0.25f);
	return P;
}

FKSPose KSPoses::Victory()
{
	FKSPose P = Guard();
	for (EKSArm Arm : { EKSArm::Left, EKSArm::Right }) SetArm(P, Arm, FVector(2, 12, 20), FVector(4, 16, 50));
	return P;
}

FKSPose KSPoses::Idle()
{
	FKSPose P = Guard();
	for (EKSArm Arm : { EKSArm::Left, EKSArm::Right }) SetArm(P, Arm, FVector(-2, 7, -27), FVector(2, 9, -52));
	P.J[KnL] = FVector(2, -12, 50); P.J[KnR] = FVector(2, 12, 50);
	P.J[AnL] = FVector(0, -13, 4);  P.J[AnR] = FVector(0, 13, 4);
	return P;
}

void KSPoses::ApplyTracked(FKSPose& P, const TArray<FVector>& Lm)
{
	if (Lm.Num() < 25) return;
	// MediaPipe: x = image right (the person's left side), y = down, z = away from the camera, metres, origin between the hips.
	// The fighter faces the opponent the way the player faces the webcam, so "towards the camera" is the fighter's forward.
	auto Map = [](const FVector& M) { return FVector(-M.Z, -M.X, -M.Y) * 100.f; };
	const FVector HipMid = (Lm[23] + Lm[24]) * 0.5f;
	const FVector Base = P.J[Hip];
	auto At = [&](int32 Index) { return Base + Map(Lm[Index] - HipMid); };
	// MediaPipe's "left" (11, 13, 15) is the person's own left.
	P.J[ShL] = At(11); P.J[ShR] = At(12);
	P.J[ElL] = At(13); P.J[ElR] = At(14);
	P.J[WrL] = At(15); P.J[WrR] = At(16);
	P.J[Neck] = (P.J[ShL] + P.J[ShR]) * 0.5f + FVector(0, 0, 3);
	// Landmark 0 is the nose; the head centre sits a little behind and above it.
	P.J[Head] = At(0) + FVector(-7, 0, 4);
	P.J[HpL] = At(23); P.J[HpR] = At(24);
}

/* ---------------- the actor ---------------- */

AKSDummy::AKSDummy()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Solid(TEXT("/Game/KS/Materials/M_KS_Solid"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ghost(TEXT("/Game/KS/Materials/M_KS_Ghost"));
	SolidMaterial = Solid.Object;
	GhostMaterial = Ghost.Object;

	auto MakePart = [this](const FName& Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Root);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetMobility(EComponentMobility::Movable);
		return Part;
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(BoneDefs); i++)
	{
		Bones.Add(MakePart(*FString::Printf(TEXT("Bone%d"), i), Cylinder.Object));
	}
	HeadMesh = MakePart(TEXT("Head"), Sphere.Object);
	Fists[0] = MakePart(TEXT("FistL"), Sphere.Object);
	Fists[1] = MakePart(TEXT("FistR"), Sphere.Object);

	// The real fighter, when one is assigned. The imported models face +Y, so they are turned to face +X like the dummy.
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Skel = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Skel"));
	Skel->SetupAttachment(Pivot);
	Skel->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Skel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Skel->SetVisibility(false);

	// A burst of light at the point of contact when a hit lands.
	HitLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HitLight"));
	HitLight->SetupAttachment(Root);
	HitLight->SetIntensityUnits(ELightUnits::Candelas);
	HitLight->SetIntensity(0.f);
	HitLight->SetAttenuationRadius(170.f);
	HitLight->SetCastShadows(false);

	Burst = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Burst"));
	Burst->SetupAttachment(Root);
	Burst->SetStaticMesh(Sphere.Object);
	Burst->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Burst->SetCastShadow(false);
	Burst->SetUsingAbsoluteLocation(true);
	Burst->SetUsingAbsoluteScale(true);
	Burst->SetVisibility(false);

	Current = Target = KSPoses::Guard();
}

bool AKSDummy::UseCharacter(const FString& Folder)
{
	static const TCHAR* ClipNames[] = { TEXT("Combat_Stance"), TEXT("Left_Jab_from_Guard"), TEXT("Right_Jab_from_Guard"),
		TEXT("Left_Hook_from_Guard"), TEXT("Right_Upper_Hook_from_Guard"), TEXT("Left_Uppercut_from_Guard"), TEXT("Right_Uppercut_from_Guard"),
		TEXT("Block1"), TEXT("Stand_Dodge"), TEXT("Stand_Dodge_1"), TEXT("Face_Punch_Reaction"), TEXT("Face_Punch_Reaction_1"),
		TEXT("Hit_Reaction_to_Waist"), TEXT("Knock_Down"), TEXT("victory"), TEXT("Chest_Pound_Taunt"), TEXT("Punch_Combo") };
	static const TCHAR* FaceNames[] = { TEXT("hurt"), TEXT("ko"), TEXT("dizzy"), TEXT("hit"), TEXT("smug"), TEXT("happy"), TEXT("love"), TEXT("neutral") };

	const FString Base = FString::Printf(TEXT("/Game/KoSekai/Characters/%s"), *Folder);
	const FString Meshes = Base + TEXT("/animation_glb/SkeletalMeshes/animation_glb");
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Meshes + TEXT(".animation_glb")));
	bCharacter = Mesh != nullptr;
	Skel->SetVisibility(bCharacter);
	for (UStaticMeshComponent* Bone : Bones) Bone->SetVisibility(!bCharacter);
	HeadMesh->SetVisibility(!bCharacter);
	for (UStaticMeshComponent* Fist : Fists) Fist->SetVisibility(!bCharacter);
	if (!bCharacter) return false;

	Skel->SetSkeletalMesh(Mesh);
	Skel->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Skel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	NormalFace = Mesh->GetMaterials().Num() ? Mesh->GetMaterials()[0].MaterialInterface : nullptr;
	Skel->SetMaterial(0, NormalFace);
	FaceShown.Reset();

	Clips.Reset();
	for (const TCHAR* Name : ClipNames)
	{
		const FString Path = FString::Printf(TEXT("%s%s.animation_glb%s"), *Meshes, Name, Name);
		if (UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *Path)) Clips.Add(Name, Clip);
	}
	// Face materials are named after the fighter, without the version suffix on the folder.
	FString Prefix = Folder;
	Prefix.RemoveFromEnd(TEXT("V2"));
	Faces.Reset();
	for (const TCHAR* Name : FaceNames)
	{
		const FString Asset = FString::Printf(TEXT("MI_%s_Face_%s"), *Prefix, Name);
		const FString Path = FString::Printf(TEXT("%s/Faces/%s.%s"), *Base, *Asset, *Asset);
		if (UMaterialInterface* Face = LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) Faces.Add(Name, Face);
	}
	ImpactGlove.Reset();
	for (const FKSImpact& Row : KSImpacts) if (Folder == Row.Fighter) ImpactGlove.Add(Row.Clip, Row.Glove);
	ShiftNow = ShiftTarget = 0.f; SquashNow = SquashTarget = 1.f;
	Reach = FVector::ZeroVector; ReachSquash = 1.f; KnockNow = 0.f; bAiming = false;
	RecoilPos = RecoilVel = FVector::ZeroVector; Lean = LeanVel = FVector2D::ZeroVector;
	PlayClip(TEXT("Combat_Stance"), true);
	return true;
}

void AKSDummy::PlayClip(const FString& Clip, bool bLoop, float Rate, float StartSeconds)
{
	if (!bCharacter) return;
	const TObjectPtr<UAnimSequence>* Found = Clips.Find(Clip);
	if (!Found || !*Found) return;
	Skel->PlayAnimation(*Found, bLoop);
	Skel->SetPlayRate(Rate);
	if (StartSeconds > 0.f) Skel->SetPosition(FMath::Min(StartSeconds, (*Found)->GetPlayLength() - 0.01f), false);
}

void AKSDummy::SetClipRate(float Rate)
{
	if (bCharacter) Skel->SetPlayRate(Rate);
}

void AKSDummy::SetFace(const TArray<FString>& Candidates)
{
	if (!bCharacter) return;
	FString Pick;
	UMaterialInterface* Material = NormalFace;
	for (const FString& Name : Candidates)
	{
		if (const TObjectPtr<UMaterialInterface>* Face = Faces.Find(Name)) { Pick = Name; Material = *Face; break; }
	}
	if (Pick == FaceShown) return;
	FaceShown = Pick;
	Skel->SetMaterial(0, Material);
}

void AKSDummy::SetBodyShift(float SideCm, float Squash)
{
	ShiftTarget = SideCm; SquashTarget = Squash;
}

void AKSDummy::AimPunch(const FString& Clip, const FVector& Point, float Weight)
{
	bAiming = true; AimClip = Clip; AimTarget = Point; AimWeight = FMath::Clamp(Weight, 0.f, 1.f);
}

FVector AKSDummy::GloveWorld(EKSArm Arm) const
{
	if (!bCharacter) return GetJointWorld(Arm == EKSArm::Left ? KSJoint::WrL : KSJoint::WrR);
	const FVector Hand = Skel->GetSocketLocation(Arm == EKSArm::Left ? TEXT("LeftHand") : TEXT("RightHand"));
	const FVector Fore = Skel->GetSocketLocation(Arm == EKSArm::Left ? TEXT("LeftForeArm") : TEXT("RightForeArm"));
	return Hand + (Hand - Fore).GetSafeNormal() * 10.f;   // the glove sits a little past the wrist
}

FVector AKSDummy::FaceWorld(bool bHome) const
{
	if (!bCharacter) return GetJointWorld(KSJoint::Head);
	FVector Face = Skel->GetSocketLocation(TEXT("headfront"));
	if (bHome)
	{
		const FVector Base = GetActorLocation();
		Face -= GetActorRightVector() * ShiftNow;
		Face.Z = Base.Z + (Face.Z - Base.Z) / FMath::Max(0.3f, SquashNow);
	}
	return Face;
}

void AKSDummy::TakeHit(EKSPunch Type, EKSArm AttackerArm, float Strength)
{
	if (!bCharacter) return;
	// The attacker's left arm comes from this fighter's right, so it throws them to their left (-Y), and vice versa.
	const float Side = AttackerArm == EKSArm::Left ? -1.f : 1.f;
	switch (Type)
	{
	case EKSPunch::Straight:
		RecoilVel.X -= 240.f * Strength; LeanVel.X += 190.f * Strength;
		break;
	case EKSPunch::Hook:
		RecoilVel.X -= 90.f * Strength; RecoilVel.Y += Side * 260.f * Strength;
		LeanVel.X += 60.f * Strength; LeanVel.Y += Side * 230.f * Strength;
		break;
	case EKSPunch::Uppercut:
		RecoilVel.X -= 120.f * Strength; RecoilVel.Z += 230.f * Strength; LeanVel.X += 260.f * Strength;
		break;
	}
}

void AKSDummy::HitStop(float Seconds)
{
	if (!bCharacter) return;
	StopLeft = Seconds;
	Skel->GlobalAnimRateScale = 0.05f;
}

void AKSDummy::Setup(const FLinearColor& Body, const FLinearColor& Gloves, bool bGhost, float Glow)
{
	BodyColor = Body; GloveColor = Gloves; bIsGhost = bGhost; BaseGlow = Glow;
	UMaterialInterface* Base = bGhost ? GhostMaterial : SolidMaterial;
	if (!Base) return;
	BodyMID = UMaterialInstanceDynamic::Create(Base, this);
	for (UStaticMeshComponent* Bone : Bones) { Bone->SetMaterial(0, BodyMID); Bone->SetCastShadow(!bGhost); }
	HeadMesh->SetMaterial(0, BodyMID); HeadMesh->SetCastShadow(!bGhost);
	for (int32 i = 0; i < 2; i++)
	{
		GloveMID[i] = UMaterialInstanceDynamic::Create(Base, this);
		Fists[i]->SetMaterial(0, GloveMID[i]);
		Fists[i]->SetCastShadow(!bGhost);
	}
	ApplyColors(false);
	Layout();
}

void AKSDummy::ApplyColors(bool bFlashing)
{
	if (!BodyMID) return;
	const float Glow = bIsGhost ? 1.6f : BaseGlow;
	BodyMID->SetVectorParameterValue(TEXT("Color"), bFlashing ? FlashColor : BodyColor);
	BodyMID->SetScalarParameterValue(TEXT("Glow"), bFlashing ? 6.f : Glow);
	if (bIsGhost) BodyMID->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
	for (UMaterialInstanceDynamic* Glove : GloveMID)
	{
		Glove->SetVectorParameterValue(TEXT("Color"), GloveColor);
		Glove->SetScalarParameterValue(TEXT("Glow"), bIsGhost ? 4.f : 1.2f);
		if (bIsGhost) Glove->SetScalarParameterValue(TEXT("Opacity"), 0.8f);
	}
}

void AKSDummy::SetTarget(const FKSPose& Pose, float Speed)
{
	Target = Pose;
	FollowSpeed = Speed;
}

void AKSDummy::Snap(const FKSPose& Pose)
{
	Current = Target = Pose;
	Layout();
}

void AKSDummy::Flash(const FLinearColor& Color, float Seconds)
{
	if (bCharacter)
	{
		// The real fighters cannot glow, so a hit lights up their face instead.
		HitLight->SetWorldLocation(FaceWorld(false) + GetActorForwardVector() * 30.f);
		HitLight->SetLightColor(Color);
		HitLight->SetIntensity(14.f);
		LightLeft = Seconds + 0.06f;
		// and a quick burst at the point of contact
		if (!BurstMID && SolidMaterial) { BurstMID = UMaterialInstanceDynamic::Create(SolidMaterial, this); Burst->SetMaterial(0, BurstMID); }
		if (BurstMID)
		{
			BurstMID->SetVectorParameterValue(TEXT("Color"), Color);
			Burst->SetWorldLocation(FaceWorld(false) + GetActorForwardVector() * 10.f);
			Burst->SetWorldScale3D(FVector(0.10f));
			Burst->SetVisibility(true);
			BurstAge = 0.f;
		}
	}
	FlashColor = Color;
	FlashLeft = Seconds;
	ApplyColors(true);
}

FVector AKSDummy::GetJointWorld(KSJoint::Type Joint) const
{
	return GetActorTransform().TransformPosition(Current.J[Joint]);
}

void AKSDummy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Alpha = 1.f - FMath::Exp(-FollowSpeed * DeltaSeconds);
	for (int32 i = 0; i < Num; i++) Current.J[i] = FMath::Lerp(Current.J[i], Target.J[i], Alpha);
	if (FlashLeft > 0.f)
	{
		FlashLeft -= DeltaSeconds;
		if (FlashLeft <= 0.f) ApplyColors(false);
	}
	if (bCharacter)
	{
		if (StopLeft > 0.f)
		{
			StopLeft -= DeltaSeconds;
			if (StopLeft <= 0.f) Skel->GlobalAnimRateScale = 1.f;
		}
		const float Ease = 1.f - FMath::Exp(-16.f * DeltaSeconds);
		ShiftNow = FMath::Lerp(ShiftNow, ShiftTarget, Ease);
		SquashNow = FMath::Lerp(SquashNow, SquashTarget, Ease);

		// Punch aim: nudge the whole body every frame until the glove is on the target. The clip supplies the punch;
		// this supplies the step in, the sideways correction and the crouch that make it actually connect.
		// We know where the glove will be on the clip's impact frame, so the step in, the sideways correction and
		// the crouch needed to put it on the target are worked out directly and eased in.
		FVector WantReach = FVector::ZeroVector;
		float WantSquash = 1.f;
		const FVector* Glove = bAiming ? ImpactGlove.Find(AimClip) : nullptr;
		if (Glove)
		{
			const FVector Local = GetActorTransform().InverseTransformPositionNoScale(AimTarget);
			WantReach.X = FMath::Clamp(Local.X - Glove->X, -45.f, 70.f) * AimWeight;
			WantReach.Y = FMath::Clamp(Local.Y - Glove->Y, -40.f, 40.f) * AimWeight;
			WantSquash = FMath::Lerp(1.f, FMath::Clamp(Local.Z / FMath::Max(20.f, Glove->Z), 0.76f, 1.06f), AimWeight);
		}
		const float Follow = 1.f - FMath::Exp(-(Glove ? 26.f : 9.f) * DeltaSeconds);
		Reach = FMath::Lerp(Reach, WantReach, Follow);
		ReachSquash = FMath::Lerp(ReachSquash, WantSquash, Follow);
		KnockNow = FMath::Lerp(KnockNow, 0.f, 1.f - FMath::Exp(-7.f * DeltaSeconds));

		// Hit recoil: a damped spring, so a hit throws the body and it wobbles back instead of sliding back.
		{
			const float Dt = FMath::Min(DeltaSeconds, 0.033f), Stiff = 230.f, Damp = 15.f;
			RecoilVel += (-Stiff * RecoilPos - Damp * RecoilVel) * Dt;
			RecoilPos += RecoilVel * Dt;
			if (RecoilPos.Z < 0.f) { RecoilPos.Z = 0.f; RecoilVel.Z = FMath::Abs(RecoilVel.Z) * 0.25f; }   // feet stay on the floor
			LeanVel += (-Stiff * Lean - Damp * LeanVel) * Dt;
			Lean += LeanVel * Dt;
			Lean.X = FMath::Clamp(Lean.X, -30.f, 30.f); Lean.Y = FMath::Clamp(Lean.Y, -30.f, 30.f);
			Pivot->SetRelativeLocation(RecoilPos);
			Pivot->SetRelativeRotation(FRotator(Lean.X, 0.f, Lean.Y));
		}
		if (BurstAge >= 0.f)
		{
			BurstAge += DeltaSeconds;
			const float U = BurstAge / 0.14f;
			if (U >= 1.f) { BurstAge = -1.f; Burst->SetVisibility(false); }
			else
			{
				Burst->SetWorldScale3D(FVector(FMath::Lerp(0.10f, 0.55f, FMath::Sqrt(U))));
				if (BurstMID) BurstMID->SetScalarParameterValue(TEXT("Glow"), 30.f * (1.f - U));
			}
		}
		Skel->SetRelativeLocation(FVector(Reach.X - KnockNow, ShiftNow + Reach.Y, 0.f));
		Skel->SetRelativeScale3D(FVector(1.f, 1.f, SquashNow * ReachSquash));

		if (LightLeft > 0.f)
		{
			LightLeft -= DeltaSeconds;
			if (LightLeft <= 0.f) HitLight->SetIntensity(0.f);
		}
	}
	Layout();
}

void AKSDummy::Layout()
{
	for (int32 i = 0; i < UE_ARRAY_COUNT(BoneDefs); i++)
	{
		const FBoneDef& Def = BoneDefs[i];
		const FVector A = Current.J[Def.A], B = Current.J[Def.B];
		const FVector Dir = B - A;
		const float Length = FMath::Max(1.f, Dir.Size());
		// The engine cylinder is 100 tall along Z with radius 50.
		Bones[i]->SetRelativeLocationAndRotation((A + B) * 0.5f, FRotationMatrix::MakeFromZ(Dir).Rotator());
		Bones[i]->SetRelativeScale3D(FVector(Def.Radius / 50.f, Def.Radius / 50.f, Length / 100.f));
	}
	HeadMesh->SetRelativeLocation(Current.J[Head]);
	HeadMesh->SetRelativeScale3D(FVector(HeadRadius / 50.f));
	Fists[0]->SetRelativeLocation(Current.J[WrL]);
	Fists[1]->SetRelativeLocation(Current.J[WrR]);
	for (UStaticMeshComponent* Fist : Fists) Fist->SetRelativeScale3D(FVector(FistRadius / 50.f));
}
