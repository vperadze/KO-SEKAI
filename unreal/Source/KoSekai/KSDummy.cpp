#include "KSDummy.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

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

	Current = Target = KSPoses::Guard();
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
