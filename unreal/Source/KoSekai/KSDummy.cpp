#include "KSDummy.h"
#include "AnimationRuntime.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "KSImpactData.h"
#include "KSAudio.h"
#include "KSVoiceData.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

// How far each model reaches above the front of its face (the "headfront" bone), in cm at full size: hair, horn, hat or
// television set. Bones end at the skull, so a camera that frames by bones alone cuts a big hairdo off. The numbers are
// measured from the model itself in every editor run (UseCharacter, "KSTEST shape"), and tools/check.sh fails when a
// fighter is missing here or has grown past his entry: that is the guard for new characters.
static const TPair<const TCHAR*, float> KSHeadRooms[] = {
	{ TEXT("SergeantMulletV2"), 32.f }, { TEXT("ChannelZero"), 30.f }, { TEXT("SirLasercorn"), 37.f }, { TEXT("AerobicsAnnihilator"), 33.f },
	{ TEXT("GrandmaThunder"), 29.f }, { TEXT("Turbotron"), 37.f }, { TEXT("CountHairspray"), 37.f },
};
static const float KSHeadRoomUnknown = 60.f;      // a fighter not in the table yet: generous, so nothing is cut meanwhile

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

	// A second copy of the model whose bones can be set directly. Each frame it copies the clip's pose from Skel and
	// then has its arms turned to match the player's real arms.
	Puppet = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Puppet"));
	Puppet->SetupAttachment(Pivot);
	Puppet->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Puppet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Puppet->SetVisibility(false);
	// The imported rig is in metres with a 0.01 bone scale, and a poseable mesh takes its bounds from the unscaled asset:
	// about 1 cm across, so it is culled as off screen. Scale the bounds up to real size and more.
	Puppet->SetBoundsScale(400.f);

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

bool AKSDummy::UseCharacter(const FString& InFolder)
{
	static const TCHAR* ClipNames[] = { TEXT("Combat_Stance"), TEXT("Left_Jab_from_Guard"), TEXT("Right_Jab_from_Guard"),
		TEXT("Left_Hook_from_Guard"), TEXT("Right_Upper_Hook_from_Guard"), TEXT("Left_Uppercut_from_Guard"), TEXT("Right_Uppercut_from_Guard"),
		TEXT("Block1"), TEXT("Stand_Dodge"), TEXT("Stand_Dodge_1"), TEXT("Face_Punch_Reaction"), TEXT("Face_Punch_Reaction_1"),
		TEXT("Hit_Reaction_to_Waist"), TEXT("Knock_Down"), TEXT("victory"), TEXT("Chest_Pound_Taunt"), TEXT("Punch_Combo"),
		// The extra moves (art/tools/add_moves.py): two fun idles and a "picked" move for the select screen, the special
		// ability, the win celebration, and the stagger of an attacker whose punch was blocked.
		TEXT("Fun_A"), TEXT("Fun_B"), TEXT("Picked"), TEXT("Special"), TEXT("Win"), TEXT("Stagger"),
		// Hit by a special ability: jolted rigid by electricity, or thrown back (AKSGameMode::SpecialHitClip).
		TEXT("Shocked"), TEXT("Blown") };
	static const TCHAR* FaceNames[] = { TEXT("hurt"), TEXT("ko"), TEXT("dizzy"), TEXT("hit"), TEXT("smug"), TEXT("happy"), TEXT("love"), TEXT("neutral"),
		TEXT("calm"), TEXT("talk"), TEXT("angry"), TEXT("blink") };

	const FString Base = FString::Printf(TEXT("/Game/KoSekai/Characters/%s"), *InFolder);
	Folder = InFolder;
	StopTalking();
	Mood.Reset(); bMoodFixed = false; bAttacking = false; LastSpoken = nullptr; BlinkLeft = 0.f; BlinkAt = FaceClock + FMath::FRandRange(1.f, 3.f);
	const FString Meshes = Base + TEXT("/animation_glb/SkeletalMeshes/animation_glb");
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Meshes + TEXT(".animation_glb")));
	bCharacter = Mesh != nullptr;
	Skel->SetVisibility(bCharacter);
	for (UStaticMeshComponent* Bone : Bones) Bone->SetVisibility(!bCharacter);
	HeadMesh->SetVisibility(!bCharacter);
	for (UStaticMeshComponent* Fist : Fists) Fist->SetVisibility(!bCharacter);
	if (!bCharacter) return false;

	Skel->SetSkeletalMesh(Mesh);
	Puppet->SetSkinnedAssetAndUpdate(Mesh);
	HeadRoomCm = KSHeadRoomUnknown;
	bool bListed = false;
	for (const TPair<const TCHAR*, float>& Row : KSHeadRooms) if (InFolder == Row.Key) { HeadRoomCm = Row.Value; bListed = true; }
#if WITH_EDITOR
	{
		// Measure it from the model: the highest vertex above the "headfront" bone in the pose the model was made in.
		// (Only the editor keeps the vertices in memory; a packaged game has the table.)
		float Measured = -1.f;
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		int32 HeadBone = Ref.FindBoneIndex(TEXT("headfront"));
		if (HeadBone == INDEX_NONE) HeadBone = Ref.FindBoneIndex(TEXT("Head"));
		FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering();
		if (HeadBone != INDEX_NONE && Render && Render->LODRenderData.Num())
		{
			const FPositionVertexBuffer& Vertices = Render->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
			TArray<FTransform> Pose;
			FAnimationRuntime::FillUpComponentSpaceTransforms(Ref, Ref.GetRefBonePose(), Pose);
			const FTransform World = Skel->GetComponentTransform();
			const FVector Up = World.InverseTransformVectorNoScale(FVector::UpVector);
			if (Vertices.GetNumVertices() > 0 && Vertices.GetVertexData() && Pose.IsValidIndex(HeadBone))
			{
				float Top = -BIG_NUMBER, Bottom = BIG_NUMBER;
				for (uint32 i = 0; i < Vertices.GetNumVertices(); i++)
				{
					const float Height = (float)FVector::DotProduct(FVector(Vertices.VertexPosition(i)), Up);
					Top = FMath::Max(Top, Height); Bottom = FMath::Min(Bottom, Height);
				}
				// Meshy's models are stored in metres and drawn a hundred times that size (the rig carries the factor).
				const float ToCm = Top - Bottom < 10.f ? 100.f : 1.f;
				Measured = (Top - (float)FVector::DotProduct(Pose[HeadBone].GetLocation(), Up)) * ToCm;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("KSTEST shape fighter=%s head_room=%.0f table=%.0f listed=%d"), *InFolder, Measured, HeadRoomCm, bListed ? 1 : 0);
	}
#endif
	bPuppet = false;
	Skel->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Skel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	NormalFace = Mesh->GetMaterials().Num() ? Mesh->GetMaterials()[0].MaterialInterface : nullptr;
	Skel->SetMaterial(0, NormalFace);
	Puppet->SetMaterial(0, NormalFace);
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
	// One line per fighter for tools/check.sh: a fighter without faces or lines stands there with one expression, mute.
	int32 Sounds = 0;
	for (const FKSVoiceLine& Line : KSVoiceLines)
	{
		if (Folder != Line.Fighter) continue;
		const FString Asset = FString::Printf(TEXT("V_%s_%s"), Line.Fighter, Line.Key);
		if (LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/KS/Voice/%s.%s"), *Asset, *Asset), nullptr, LOAD_NoWarn | LOAD_Quiet)) Sounds++;
	}
	int32 Moves = 0;
	for (const TCHAR* Name : { TEXT("Fun_A"), TEXT("Fun_B"), TEXT("Picked"), TEXT("Special"), TEXT("Win"), TEXT("Stagger"), TEXT("Shocked"), TEXT("Blown") }) Moves += Clips.Contains(Name) ? 1 : 0;
	UE_LOG(LogTemp, Display, TEXT("KSTEST fighter=%s faces=%d voice_lines=%d moves=%d"), *Folder, Faces.Num(), Sounds, Moves);
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
	// A clip never starts with a jump: remember the pose on screen and ease from it into the new clip, so a hand that
	// is out at the end of a jab travels straight into the uppercut instead of snapping back to the guard first.
	BlendFrom = ShownPose;
	CurrentClip = Clip;
	if (Clip == TEXT("Knock_Down")) { FTimerHandle Handle; GetWorldTimerManager().SetTimer(Handle, [this]() { KSAudio::Play(this, EKSSfx::Fall, 0.9f); }, 0.55f, false); }
	BlendPrev = PrevShownPose;
	BlendDt = ShownDt;
	// Into one of his own idle moves slowly, so it grows out of the stance; out of it at the usual speed of a punch.
	BlendTime = Clip.Contains(TEXT("Reaction")) ? 0.06f : Clip.StartsWith(TEXT("Fun_")) ? 0.32f : (Clip == TEXT("Combat_Stance") ? 0.18f : 0.11f);
	BlendLeft = BlendTime;
	Skel->PlayAnimation(*Found, bLoop);
	Skel->SetPlayRate(Rate);
	if (StartSeconds > 0.f) Skel->SetPosition(FMath::Min(StartSeconds, (*Found)->GetPlayLength() - 0.01f), false);
}

float AKSDummy::ClipSeconds(const FString& Clip) const
{
	const TObjectPtr<UAnimSequence>* Found = Clips.Find(Clip);
	return Found && *Found ? (*Found)->GetPlayLength() : 0.f;
}

void AKSDummy::SetClipRate(float Rate)
{
	if (bCharacter) Skel->SetPlayRate(Rate);
}

void AKSDummy::SetFace(const TArray<FString>& Candidates)
{
	if (!bCharacter) return;
	Mood.Reset();
	for (const FString& Name : Candidates)
	{
		if (Faces.Contains(Name)) { Mood = Name; break; }
	}
	bMoodFixed = Candidates.Num() > 0 && Candidates[0] == TEXT("ko");
}

UMaterialInterface* AKSDummy::FaceMaterial(const TCHAR* Name) const
{
	const TObjectPtr<UMaterialInterface>* Face = Faces.Find(Name);
	return Face ? Face->Get() : nullptr;
}

void AKSDummy::ShowFace(const FString& Name, UMaterialInterface* Material)
{
	if (!Material) Material = NormalFace;
	TestFaces.Add(Name);
	if (Name == FaceShown) return;
	FaceShown = Name;
	Skel->SetMaterial(0, Material);
	Puppet->SetMaterial(0, Material);
}

/** Picks the face for this frame. In order: knocked out; speaking (the mouth opens and closes with the loudness of the
 *  line); the mood the game asked for; throwing a punch; a blink; and otherwise the resting face. A fighter whose
 *  painted face is already a shout (the Count, Channel Zero, TURBOTRON) rests on a calmer one and keeps the shout
 *  for punching and for the open mouth. */
void AKSDummy::UpdateFace(float DeltaSeconds)
{
	if (!bCharacter) return;
	FaceClock += DeltaSeconds;
	// The resting face: "calm" where one was painted, else the nearest thing the fighter has.
	const TCHAR* RestName = Faces.Contains(TEXT("calm")) ? TEXT("calm") : Faces.Contains(TEXT("neutral")) ? TEXT("neutral")
		: (!Faces.Contains(TEXT("talk")) && Faces.Contains(TEXT("smug"))) ? TEXT("smug") : TEXT("");

	bool bOpen = false;
	if (Spoken)
	{
		SpokenT += DeltaSeconds;
		const int32 Index = FMath::FloorToInt(SpokenT * KSVoiceMouthHz);
		const int32 Count = FCStringAnsi::Strlen(Spoken->Mouth);
		if (Index >= Count) { Spoken = nullptr; VoicePlayer = nullptr; }
		else bOpen = Spoken->Mouth[Index] >= '3';
	}
	if (bOpen && !bMouthOpen) TestMouthOpens++;
	bMouthOpen = bOpen;

	if (BlinkLeft > 0.f) BlinkLeft -= DeltaSeconds;
	else if (FaceClock >= BlinkAt) { BlinkLeft = 0.13f; BlinkAt = FaceClock + FMath::FRandRange(1.8f, 5.f); }

	if (bMoodFixed) ShowFace(Mood, FaceMaterial(*Mood));
	else if (Spoken && bOpen) ShowFace(TEXT("talk"), FaceMaterial(TEXT("talk")));
	else if (Spoken) ShowFace(Mood.IsEmpty() ? RestName : *Mood, FaceMaterial(Mood.IsEmpty() ? RestName : *Mood));
	else if (!Mood.IsEmpty()) ShowFace(Mood, FaceMaterial(*Mood));
	else if (bAttacking) ShowFace(TEXT("angry"), FaceMaterial(TEXT("angry")));
	else if (BlinkLeft > 0.f && Faces.Contains(TEXT("blink"))) ShowFace(TEXT("blink"), FaceMaterial(TEXT("blink")));
	else ShowFace(RestName, FaceMaterial(RestName));
}

float AKSDummy::Say(const TCHAR* Prefix, float Volume)
{
	if (!bCharacter) return 0.f;
	TArray<const FKSVoiceLine*> Choice;
	for (const FKSVoiceLine& Line : KSVoiceLines)
	{
		if (Folder == Line.Fighter && FCString::Strncmp(Line.Key, Prefix, FCString::Strlen(Prefix)) == 0) Choice.Add(&Line);
	}
	if (Choice.Num() > 1) Choice.Remove(LastSpoken);   // not the same taunt twice in a row
	if (Choice.Num() == 0) return 0.f;
	StopTalking();
	Spoken = LastSpoken = Choice[FMath::RandRange(0, Choice.Num() - 1)];
	SpokenT = 0.f;
	const FString Asset = FString::Printf(TEXT("V_%s_%s"), Spoken->Fighter, Spoken->Key);
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/KS/Voice/%s.%s"), *Asset, *Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
	// With no sound device (the self-test runs with -nosound) there is no player, and the mouth still moves by the clock.
	VoicePlayer = Sound ? UGameplayStatics::SpawnSound2D(this, Sound, Volume) : nullptr;
	UE_LOG(LogTemp, Display, TEXT("KSTEST voice fighter=%s line=%s sound=%d"), Spoken->Fighter, Spoken->Key, Sound ? 1 : 0);
	return Spoken->Seconds;
}

void AKSDummy::StopTalking()
{
	if (VoicePlayer) VoicePlayer->Stop();
	VoicePlayer = nullptr;
	Spoken = nullptr;
}

FString AKSDummy::SpokenText() const
{
	return Spoken ? FString(Spoken->Text) : FString();
}

void AKSDummy::TakeFaceStats(int32& OutFaces, int32& OutMouthOpens)
{
	OutFaces = TestFaces.Num(); OutMouthOpens = TestMouthOpens;
	TestFaces.Reset(); TestMouthOpens = 0;
}

FVector AKSDummy::DrawnHandFromHead() const
{
	if (!bCharacter) return FVector::ZeroVector;
	return Puppet->GetBoneLocationByName(TEXT("LeftHand"), EBoneSpaces::WorldSpace) - Puppet->GetBoneLocationByName(TEXT("headfront"), EBoneSpaces::WorldSpace);
}

void AKSDummy::SetBodyShift(float SideCm, float Squash)
{
	ShiftTarget = SideCm; SquashTarget = Squash;
}

void AKSDummy::SetTrackedArms(const TArray<FVector>* Landmarks)
{
	bPuppet = bCharacter && Landmarks && Landmarks->Num() >= 25;
	if (bPuppet) Tracked = *Landmarks;
}

/** Turns the upper arm and the forearm of the puppet so they point the way the player's own arm points. */
void AKSDummy::PoseArm(bool bLeft)
{
	const FName UpperBone = bLeft ? TEXT("LeftArm") : TEXT("RightArm"), ForeBone = bLeft ? TEXT("LeftForeArm") : TEXT("RightForeArm"), HandBone = bLeft ? TEXT("LeftHand") : TEXT("RightHand");
	const int32 Sh = bLeft ? 11 : 12, El = bLeft ? 13 : 14, Wr = bLeft ? 15 : 16;
	// MediaPipe: x = image right (the person's left), y = down, z = away from the camera. The fighter faces the opponent
	// the way the player faces the webcam, so "towards the camera" is the fighter's forward (the same mapping as ApplyTracked).
	auto ToWorld = [this](const FVector& M) { return GetActorTransform().TransformVectorNoScale(FVector(-M.Z, -M.X, -M.Y)).GetSafeNormal(); };
	const FVector WantUpper = ToWorld(Tracked[El] - Tracked[Sh]), WantFore = ToWorld(Tracked[Wr] - Tracked[El]);
	if (WantUpper.IsNearlyZero() || WantFore.IsNearlyZero()) return;

	const FTransform U = Puppet->GetBoneTransformByName(UpperBone, EBoneSpaces::WorldSpace);
	const FVector Elbow = Puppet->GetBoneLocationByName(ForeBone, EBoneSpaces::WorldSpace);
	const FQuat TurnUpper = FQuat::FindBetweenNormals((Elbow - U.GetLocation()).GetSafeNormal(), WantUpper);
	Puppet->SetBoneRotationByName(UpperBone, (TurnUpper * U.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	Puppet->RefreshBoneTransforms();

	const FTransform F = Puppet->GetBoneTransformByName(ForeBone, EBoneSpaces::WorldSpace);
	const FVector Wrist = Puppet->GetBoneLocationByName(HandBone, EBoneSpaces::WorldSpace);
	const FQuat TurnFore = FQuat::FindBetweenNormals((Wrist - F.GetLocation()).GetSafeNormal(), WantFore);
	Puppet->SetBoneRotationByName(ForeBone, (TurnFore * F.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
}

/** Bends one leg of the drawn body so its foot is on Target (two-bone IK, knee kept pointing forwards), then levels the foot. */
void AKSDummy::SolveLeg(bool bLeft, const FVector& FootAt, const FQuat& FootRotation)
{
	const FName Up = bLeft ? TEXT("LeftUpLeg") : TEXT("RightUpLeg"), Shin = bLeft ? TEXT("LeftLeg") : TEXT("RightLeg"), Foot = bLeft ? TEXT("LeftFoot") : TEXT("RightFoot");
	const FTransform UpT = Puppet->GetBoneTransformByName(Up, EBoneSpaces::WorldSpace);
	const FVector Hip = UpT.GetLocation(), KneeAt = Puppet->GetBoneLocationByName(Shin, EBoneSpaces::WorldSpace), Ankle = Puppet->GetBoneLocationByName(Foot, EBoneSpaces::WorldSpace);
	const float Thigh = FVector::Dist(Hip, KneeAt), Calf = FVector::Dist(KneeAt, Ankle);
	if (Thigh < 1.f || Calf < 1.f) return;
	const FVector ToFoot = FootAt - Hip;
	const float Span = FMath::Clamp(ToFoot.Size(), FMath::Abs(Thigh - Calf) + 0.5f, Thigh + Calf - 0.3f);
	const FVector Dir = ToFoot.GetSafeNormal();
	// The knee keeps the side it was on in the clip, pulled towards the way the fighter faces so it never folds backwards.
	FVector Pole = (KneeAt - Hip) - Dir * FVector::DotProduct(KneeAt - Hip, Dir) + GetActorForwardVector() * (0.5f * Thigh);
	Pole = (Pole - Dir * FVector::DotProduct(Pole, Dir)).GetSafeNormal();
	if (Pole.IsNearlyZero()) return;
	const float Along = (Thigh * Thigh - Calf * Calf + Span * Span) / (2.f * Span);
	const FVector NewKnee = Hip + Dir * Along + Pole * FMath::Sqrt(FMath::Max(0.f, Thigh * Thigh - Along * Along));
	Puppet->SetBoneRotationByName(Up, (FQuat::FindBetweenNormals((KneeAt - Hip).GetSafeNormal(), (NewKnee - Hip).GetSafeNormal()) * UpT.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	Puppet->RefreshBoneTransforms();
	const FTransform ShinT = Puppet->GetBoneTransformByName(Shin, EBoneSpaces::WorldSpace);
	const FVector AnkleNow = Puppet->GetBoneLocationByName(Foot, EBoneSpaces::WorldSpace);
	const FQuat Turn = FQuat::FindBetweenNormals((AnkleNow - ShinT.GetLocation()).GetSafeNormal(), (Hip + Dir * Span - ShinT.GetLocation()).GetSafeNormal());
	Puppet->SetBoneRotationByName(Shin, (Turn * ShinT.GetRotation()).Rotator(), EBoneSpaces::WorldSpace);
	Puppet->RefreshBoneTransforms();
	Puppet->SetBoneRotationByName(Foot, FootRotation.Rotator(), EBoneSpaces::WorldSpace);
}

/**
 * Footwork for the drawn body. The clips are standing punches and the body is slid about by the punch aim and the
 * recoil, which left the legs frozen and the feet skating. Here the feet stay planted on the canvas and take a real
 * step when the body has moved away from them, the fighter bounces on his knees like a boxer, and he shuffles a
 * foot every half second or so even when nothing is happening.
 */
void AKSDummy::Footwork(float DeltaSeconds)
{
	// Not while falling or celebrating: those clips move the legs themselves.
	static const TCHAR* OwnLegs[] = { TEXT("Knock_Down"), TEXT("victory"), TEXT("Chest_Pound_Taunt"), TEXT("Fun_A"), TEXT("Fun_B"), TEXT("Picked"), TEXT("Special"), TEXT("Win"), TEXT("Stagger"), TEXT("Shocked"), TEXT("Blown") };
	bool bWanted = true;
	for (const TCHAR* Name : OwnLegs) bWanted &= CurrentClip != Name;
	LegWeight = FMath::FInterpTo(LegWeight, bWanted ? 1.f : 0.f, DeltaSeconds, 6.f);
	if (LegWeight < 0.02f) { Feet[0].bPlaced = Feet[1].bPlaced = false; return; }
	FootClock += DeltaSeconds;

	static const FName FootBones[2] = { TEXT("LeftFoot"), TEXT("RightFoot") };
	FVector Home[2]; FQuat FootRot[2];
	for (int32 i = 0; i < 2; i++)
	{
		const FTransform T = Puppet->GetBoneTransformByName(FootBones[i], EBoneSpaces::WorldSpace);
		Home[i] = T.GetLocation(); FootRot[i] = T.GetRotation();
	}

	// The bounce: hips sink and rise about twice a second, with a little weight shift from foot to foot.
	const float Beat = FootClock * 2.3f * 2.f * PI;
	const float Sink = 4.5f * (0.5f - 0.5f * FMath::Cos(Beat)) * LegWeight, Sway = 1.6f * FMath::Sin(Beat * 0.5f) * LegWeight;
	const FVector HipsNow = Puppet->GetBoneLocationByName(TEXT("Hips"), EBoneSpaces::WorldSpace);
	Puppet->SetBoneLocationByName(TEXT("Hips"), HipsNow + GetActorRightVector() * Sway - FVector(0, 0, Sink), EBoneSpaces::WorldSpace);
	Puppet->RefreshBoneTransforms();

	// A shuffle: every so often one foot (they take turns) hops to a slightly different spot.
	if (FootClock >= NextShuffle && Feet[0].StepT < 0.f && Feet[1].StepT < 0.f && Feet[ShuffleFoot].bPlaced)
	{
		FFoot& F = Feet[ShuffleFoot];
		F.From = F.Planted;
		F.To = Home[ShuffleFoot] + GetActorForwardVector() * FMath::FRandRange(-6.f, 6.f) + GetActorRightVector() * FMath::FRandRange(-4.f, 4.f);
		F.StepT = 0.f;
		KSAudio::Play(this, EKSSfx::Footstep, 0.12f);
		ShuffleFoot = 1 - ShuffleFoot;
		NextShuffle = FootClock + FMath::FRandRange(0.4f, 0.95f);
	}
	for (int32 i = 0; i < 2; i++)
	{
		FFoot& F = Feet[i];
		if (!F.bPlaced) { F.Planted = Home[i]; F.bPlaced = true; F.StepT = -1.f; }
		const float Away = FVector::Dist2D(F.Planted, Home[i]);
		if (F.StepT < 0.f && Feet[1 - i].StepT < 0.f && Away > 13.f)
		{
			// The body has left this foot behind: step to where it should be, and a little past, in the direction of travel.
			F.From = F.Planted; F.To = Home[i] + (Home[i] - F.Planted).GetSafeNormal2D() * 5.f; F.StepT = 0.f;
			KSAudio::Play(this, EKSSfx::Footstep, 0.3f);
		}
		else if (Away > 45.f) { F.Planted = Home[i]; F.StepT = -1.f; }      // far too far (a new bout, a teleport): just put it there
		float Lift = 0.f;
		if (F.StepT >= 0.f)
		{
			F.StepT += DeltaSeconds / 0.13f;
			const float U = FMath::Min(1.f, F.StepT);
			F.Planted = FMath::Lerp(F.From, F.To, FMath::SmoothStep(0.f, 1.f, U));
			Lift = 7.f * FMath::Sin(U * PI);
			if (U >= 1.f) F.StepT = -1.f;
		}
		const FVector OnFloor(F.Planted.X, F.Planted.Y, Home[i].Z + Lift);
		SolveLeg(i == 0, FMath::Lerp(Home[i], OnFloor, LegWeight), FootRot[i]);
	}
	Puppet->RefreshBoneTransforms();
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

FVector AKSDummy::BodyMiddle() const
{
	TArray<FVector> Points;
	BodyPoints(Points);
	if (!Points.Num()) return GetActorLocation();
	FVector Sum = FVector::ZeroVector;
	for (const FVector& Point : Points) Sum += Point;
	return Sum / Points.Num();
}

FBox AKSDummy::BodyBox() const
{
	TArray<FVector> Points;
	BodyPoints(Points);
	FBox Box(ForceInit);
	for (const FVector& Point : Points) Box += Point;
	if (!Box.IsValid) return FBox(GetActorLocation() - FVector(40.f), GetActorLocation() + FVector(40.f, 40.f, 190.f));
	const float Scale = GetActorScale3D().Z;
	Box = Box.ExpandBy(15.f * Scale);                  // flesh, gloves and boots round the bones
	// The head: whichever way it is turned, everything on it lies within this far of the face.
	const FVector Face = FaceWorld(false);
	const float Room = bCharacter ? HeadRoom() : 20.f * Scale;
	Box += Face - FVector(Room);
	Box += Face + FVector(Room);
	return Box;
}

void AKSDummy::BodyPoints(TArray<FVector>& Out) const
{
	Out.Reset();
	if (!bCharacter || !Skel)
	{
		for (int32 i = 0; i < KSJoint::Num; i++) Out.Add(GetJointWorld((KSJoint::Type)i));
		return;
	}
	const FTransform World = Skel->GetComponentTransform();
	for (const FTransform& Bone : Skel->GetComponentSpaceTransforms()) Out.Add(World.TransformPosition(Bone.GetLocation()));
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

FVector2D AKSDummy::HipsOwn() const
{
	const FVector Own = GetActorTransform().InverseTransformPosition(Skel->GetSocketLocation(TEXT("Hips"))) - Skel->GetRelativeLocation();
	return FVector2D(Own.X, Own.Y);
}

FVector2D AKSDummy::TakeShift()
{
	if (!bCharacter) return FVector2D::ZeroVector;
	const FVector World = GetActorTransform().TransformVector(FVector(0.f, ShiftNow, 0.f));
	ShiftNow = ShiftTarget = 0.f;
	return FVector2D(World.X, World.Y);
}

FVector2D AKSDummy::CommitTravel()
{
	if (!bCharacter || !bHipsHomeKnown) return FVector2D::ZeroVector;
	const FVector2D Own = HipsOwn() - HipsHome;
	if (Own.Size() < 8.f) return FVector2D::ZeroVector;      // he has not gone anywhere worth a step
	TravelFix += Own; TravelClip = CurrentClip;
	const FVector World = GetActorTransform().TransformVector(FVector(Own.X, Own.Y, 0.f));
	// The feet are planted in the world: without this they would be dragged along by the move of the actor.
	Feet[0].bPlaced = Feet[1].bPlaced = false;
	return FVector2D(World.X, World.Y);
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
		// The burst at the point of contact used to be a glowing ball here ("hit circles", player, 2026-11-13: make
		// them look better). It is a particle effect now, played by the game: AKSGameMode::HitSpark.
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
	UpdateFace(DeltaSeconds);
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
		// A dancer stays on the floor (player report, 2026-11-13: Turbotron's breakdance went into the canvas). The lowest
		// bone of the pose is brought back up to where bones rest: a finger above the floor when he is on his feet (toes),
		// a hand's breadth when he is down on it (the middle of a back or a thigh).
		float Lift = 0.f;
		if (bKeepOnFloor)
		{
			const FTransform World = Skel->GetComponentTransform();
			const float Scale = FMath::Max(0.01f, (float)GetActorScale3D().Z), Base = GetActorLocation().Z + FloorLift;
			float Low = BIG_NUMBER;
			for (const FTransform& Bone : Skel->GetComponentSpaceTransforms()) Low = FMath::Min(Low, (float)World.TransformPosition(Bone.GetLocation()).Z);
			if (Low < BIG_NUMBER)
			{
				const bool bDown = Skel->GetSocketLocation(TEXT("headfront")).Z - Base < 95.f * Scale;
				Lift = FMath::Max(0.f, (bDown ? 9.f : 1.f) * Scale - (Low - Base));
			}
		}
		FloorLift = Lift;
		if (bStayOnSpot)
		{
			// Where the middle of the body would be without the shift it has now, and the shift that puts it over the spot.
			const FVector Shifted = GetActorTransform().TransformVectorNoScale(FVector(SpotShift.X, SpotShift.Y, 0.f));
			const FVector Middle = BodyMiddle() - Shifted;
			const FVector Need = GetActorTransform().InverseTransformVectorNoScale(FVector(Spot.X - Middle.X, Spot.Y - Middle.Y, 0.f));
			SpotShift = FVector2D(Need.X, Need.Y);
		}
		else SpotShift = FVector2D::ZeroVector;
		const float OwnScale = FMath::Max(0.01f, (float)GetActorScale3D().Z);
		// Travel handed over to the actor (CommitTravel): taken off the drawn body in full while the clip that travelled
		// is still on, and less and less as the next clip is eased in (the same curve as the blend below).
		FVector2D Travel = FVector2D::ZeroVector;
		if (!TravelFix.IsZero())
		{
			if (CurrentClip == TravelClip) Travel = TravelFix;
			else if (BlendLeft > 0.f) Travel = TravelFix * (1.f - FMath::SmoothStep(0.f, 1.f, 1.f - BlendLeft / BlendTime));
			else TravelFix = FVector2D::ZeroVector;
		}
		// Where the stance keeps the hips, learnt while he stands in it: what CommitTravel measures travel from.
		if (CurrentClip == TEXT("Combat_Stance") && BlendLeft <= 0.f) { HipsHome = bHipsHomeKnown ? FMath::Lerp(HipsHome, HipsOwn(), 0.1f) : HipsOwn(); bHipsHomeKnown = true; }
		Skel->SetRelativeLocation(FVector(Reach.X - KnockNow + SpotShift.X / OwnScale - Travel.X, ShiftNow + Reach.Y + SpotShift.Y / OwnScale - Travel.Y, FloorLift / OwnScale));
		Skel->SetRelativeScale3D(FVector(1.f, 1.f, SquashNow * ReachSquash));

		// Tracked arms: show the puppet, give it the clip's pose, then point its arms where the player's arms point.
		Puppet->SetRelativeLocation(Skel->GetRelativeLocation());
		Puppet->SetRelativeScale3D(Skel->GetRelativeScale3D());
		// The clip plays on the hidden Skel; what is drawn is the Puppet, which gets the clip's pose eased in from
		// whatever was on screen when the clip changed, and then, with the webcam, the player's own arm directions.
		Skel->SetVisibility(false);
		Puppet->SetVisibility(true);
		Puppet->CopyPoseFromSkeletalComponent(Skel);
		TArray<FTransform>& Local = Puppet->BoneSpaceTransforms;
		if (BlendLeft > 0.f && BlendFrom.Num() == Local.Num())
		{
			const float A = FMath::SmoothStep(0.f, 1.f, 1.f - BlendLeft / BlendTime);
			// Keep the momentum: the old pose is not frozen, it carries on at the speed each bone had when the clip changed,
			// slowing down over a few hundredths of a second. A moving hand flows into the next move instead of stopping dead.
			const float Tau = 0.05f, Age = BlendTime - BlendLeft;
			const bool bCarry = BlendPrev.Num() == Local.Num() && BlendDt > 0.002f;
			const float Carry = bCarry ? FMath::Min(3.f, Tau * (1.f - FMath::Exp(-Age / Tau)) / BlendDt) : 0.f;
			for (int32 i = 0; i < Local.Num(); i++)
			{
				FTransform From = BlendFrom[i];
				if (bCarry)
				{
					FQuat Step = BlendFrom[i].GetRotation() * BlendPrev[i].GetRotation().Inverse();
					if (Step.W < 0.f) Step = FQuat(-Step.X, -Step.Y, -Step.Z, -Step.W);
					FVector Axis; double Angle; Step.ToAxisAndAngle(Axis, Angle);
					if (Angle > 1e-4 && Angle < 1.0) From.SetRotation((FQuat(Axis, Angle * Carry) * BlendFrom[i].GetRotation()).GetNormalized());
					From.SetTranslation(BlendFrom[i].GetTranslation() + (BlendFrom[i].GetTranslation() - BlendPrev[i].GetTranslation()) * Carry);
				}
				const FTransform To = Local[i];
				Local[i].Blend(From, To, A);
			}
			BlendLeft -= DeltaSeconds;
		}
		PrevShownPose = ShownPose;
		ShownDt = DeltaSeconds;
		ShownPose = Local;
		Puppet->RefreshBoneTransforms();
		Footwork(DeltaSeconds);
		if (bPuppet)
		{
			PoseArm(true);
			Puppet->RefreshBoneTransforms();
			PoseArm(false);
			Puppet->RefreshBoneTransforms();
		}

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
