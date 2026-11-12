#include "KSGameMode.h"
#include "KSDummy.h"
#include "KSPawn.h"
#include "KSHUD.h"
#include "KSTracking.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/PointLight.h"
#include "Engine/TextRenderActor.h"
#include "Engine/GameInstance.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

namespace
{
	const float RoundSeconds = 120.f;
	const float Grace = 0.14f;            // a dodge this late after his punch arrives still counts (camera delay)
	const float OppDistance = 112.f;      // cm between the two fighters
	const float WindupSeconds[] = { 0.65f, 0.85f, 0.95f };   // jab, hook, uppercut
	const float AttackDamage[] = { 7.f, 12.f, 16.f };
	const float PunchDamage[] = { 3.f, 4.f, 5.f };            // straight, hook, uppercut

	enum class EOutcome : uint8 { HitPlus, Hit, Half, Blocked, Dodged };   // ordered worst to best for the player

	// What beats what. Side is the hand he throws; his left comes from the player's right.
	EOutcome Outcome(EKSPunch Type, EKSArm Side, EKSDefense Def)
	{
		const bool bEvading = Def == EKSDefense::EvadeLeft || Def == EKSDefense::EvadeRight;
		if (Type == EKSPunch::Straight)
		{
			return Def == EKSDefense::None ? EOutcome::Hit : Def == EKSDefense::Block ? EOutcome::Blocked : EOutcome::Dodged;
		}
		if (Type == EKSPunch::Hook)
		{
			const EKSDefense Away = Side == EKSArm::Left ? EKSDefense::EvadeLeft : EKSDefense::EvadeRight;
			if (Def == EKSDefense::Duck || Def == Away) return EOutcome::Dodged;
			return Def == EKSDefense::Block ? EOutcome::Half : EOutcome::Hit;
		}
		if (bEvading) return EOutcome::Dodged;                                  // uppercut
		return Def == EKSDefense::Duck ? EOutcome::HitPlus : EOutcome::Hit;     // it breaks a block, and ducking walks into it
	}
}

const TArray<FKSFighterDef>& AKSGameMode::Roster()
{
	static const TArray<FKSFighterDef> Fighters = {
		{ TEXT("PROTO"), TEXT("The first fighter drawn. The first one cut."),
		  TEXT("PROTO steps aside and lets you through."),
		  FLinearColor(0.45f, 0.40f, 0.62f), KSColors::Lilac, 100.f, 1.00f, 1.00f, 0.20f, 1.00f, 3, 1.00f },
		{ TEXT("SERGEANT MULLET"), TEXT("Cut by legal. Never told why."),
		  TEXT("The Sergeant salutes. First time anyone has seen him do it."),
		  FLinearColor(0.20f, 0.30f, 0.10f), KSColors::Yellow, 120.f, 0.90f, 1.15f, 0.20f, 0.95f, 3, 1.08f },
		{ TEXT("LADY STATIC"), TEXT("Her sprites were recorded over."),
		  TEXT("The static clears. For a second you can see her face."),
		  FLinearColor(0.05f, 0.35f, 0.45f), KSColors::Cyan, 110.f, 0.72f, 0.90f, 0.35f, 0.70f, 2, 0.96f },
		{ TEXT("NULL"), TEXT("No name in the ROM. Only a hitbox."),
		  TEXT("NULL finally gets a name on the high-score table: yours."),
		  FLinearColor(0.02f, 0.02f, 0.03f), KSColors::Red, 150.f, 0.70f, 1.25f, 0.40f, 0.60f, 3, 1.18f },
	};
	return Fighters;
}

AKSGameMode::AKSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AKSPawn::StaticClass();
	HUDClass = AKSHUD::StaticClass();

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Solid(TEXT("/Game/KS/Materials/M_KS_Solid"));
	static ConstructorHelpers::FObjectFinder<UTextureCube> Sky(TEXT("/Engine/EngineResources/GrayLightTextureCube"));
	CubeMesh = Cube.Object; SphereMesh = Sphere.Object; CylinderMesh = Cylinder.Object;
	SolidMaterial = Solid.Object; SkyCube = Sky.Object;
}

void AKSGameMode::BeginPlay()
{
	Super::BeginPlay();
	TrackingSubsystem = GetGameInstance()->GetSubsystem<UKSTrackingSubsystem>();

	const TCHAR* Cmd = FCommandLine::Get();
	bAuto = FParse::Param(Cmd, TEXT("KSAuto"));
	FParse::Value(Cmd, TEXT("KSShots="), ShotEvery);
	FString Start;
	FParse::Value(Cmd, TEXT("KSStart="), Start);
	if (Start == TEXT("fight"))
	{
		int32 Index = 0;
		FParse::Value(Cmd, TEXT("KSFighter="), Index);
		EnterArena();
		StartCard(FMath::Clamp(Index, 0, Roster().Num() - 1));
	}
	else
	{
		EnterStreet(Start == TEXT("ending"));
	}
}

/* ================= helpers ================= */

FKSBuilder AKSGameMode::MakeBuilder()
{
	FKSBuilder B;
	B.World = GetWorld(); B.Out = &WorldActors;
	B.Cube = CubeMesh; B.Sphere = SphereMesh; B.Cylinder = CylinderMesh; B.Solid = SolidMaterial; B.SkyCube = SkyCube;
	return B;
}

void AKSGameMode::ClearWorld()
{
	for (AActor* Actor : WorldActors) if (IsValid(Actor)) Actor->Destroy();
	WorldActors.Reset();
	Street = FKSStreet();
	Arena = FKSArena();
	PlayerDummy = nullptr; OppDummy = nullptr;
}

void AKSGameMode::SetPhase(EKSPhase NewPhase)
{
	Phase = NewPhase;
	PhaseTime = 0.f;
}

bool AKSGameMode::Pressed(const FKey& Key) const { return PC && PC->WasInputKeyJustPressed(Key); }
bool AKSGameMode::Down(const FKey& Key) const { return PC && PC->IsInputKeyDown(Key); }

bool AKSGameMode::ConfirmPressed()
{
	return Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::E) || Pressed(EKeys::Gamepad_FaceButton_Bottom) || FramePunches.Num() > 0;
}

void AKSGameMode::Popup(const FString& Text, const FLinearColor& Color, float X, float Y, bool bBig)
{
	Popups.Add({ Text, Color, FVector2D(X, Y), Clock, bBig ? 0.9f : 0.65f, bBig });
}

bool AKSGameMode::Stance(EKSDefense D) const
{
	if (bAuto && AutoDefense == D) return true;
	if (TrackingSubsystem && TrackingSubsystem->IsHeld(D)) return true;
	switch (D)
	{
	case EKSDefense::Block: return Down(EKeys::SpaceBar);
	case EKSDefense::EvadeLeft: return Down(EKeys::Left);
	case EKSDefense::EvadeRight: return Down(EKeys::Right);
	case EKSDefense::Duck: return Down(EKeys::Down);
	default: return false;
	}
}

EKSDefense AKSGameMode::Defense() const
{
	for (EKSDefense D : { EKSDefense::EvadeLeft, EKSDefense::EvadeRight, EKSDefense::Duck, EKSDefense::Block })
	{
		if (Stance(D)) return D;
	}
	return EKSDefense::None;
}

/* ================= main loop ================= */

void AKSGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!PC) PC = GetWorld()->GetFirstPlayerController();
	if (PC && !Pawn)
	{
		Pawn = Cast<AKSPawn>(PC->GetPawn());
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	if (!PC || !Pawn) return;

	// Collect this frame's punches: the webcam tracker always, the keyboard only while fighting (A and D walk in the street).
	FramePunches.Reset();
	if (TrackingSubsystem)
	{
		for (const FKSPunchEvent& E : TrackingSubsystem->TakePunches()) FramePunches.Add({ E.Arm, E.Type });
	}
	if (Phase == EKSPhase::Fight || Phase == EKSPhase::Card)
	{
		if (Pressed(EKeys::A)) FramePunches.Add({ EKSArm::Left, EKSPunch::Straight });
		if (Pressed(EKeys::Q)) FramePunches.Add({ EKSArm::Left, EKSPunch::Hook });
		if (Pressed(EKeys::Z)) FramePunches.Add({ EKSArm::Left, EKSPunch::Uppercut });
		if (Pressed(EKeys::D)) FramePunches.Add({ EKSArm::Right, EKSPunch::Straight });
		if (Pressed(EKeys::E)) FramePunches.Add({ EKSArm::Right, EKSPunch::Hook });
		if (Pressed(EKeys::C)) FramePunches.Add({ EKSArm::Right, EKSPunch::Uppercut });
	}

	if (Phase == EKSPhase::Fight && Pressed(EKeys::P)) bPaused = !bPaused;
	if (Phase != EKSPhase::Fight) bPaused = false;
	const float GameDt = bPaused ? 0.f : Dt;
	Clock += GameDt;
	PhaseTime += GameDt;

	switch (Phase)
	{
	case EKSPhase::Street: TickStreet(GameDt); break;
	case EKSPhase::PullIn: TickPullIn(GameDt); break;
	case EKSPhase::Card: TickCard(GameDt); break;
	case EKSPhase::Fight: TickFight(GameDt); break;
	case EKSPhase::FightEnd: TickFightEnd(GameDt); break;
	case EKSPhase::Result: TickResult(GameDt); break;
	case EKSPhase::Continue: TickContinue(GameDt); break;
	case EKSPhase::Ending: TickEnding(GameDt); break;
	}

	if (Phase != EKSPhase::PullIn) WhiteFade = FMath::Max(0.f, WhiteFade - Dt * 0.8f);
	RedFlash = FMath::Max(0.f, RedFlash - GameDt * 2.5f);
	Shake = FMath::Max(0.f, Shake - GameDt * 30.f);
	Popups.RemoveAll([this](const FKSPopup& P) { return Clock - P.Start > P.Life; });

	if (ShotEvery > 0.f)
	{
		NextShot -= Dt;
		if (NextShot <= 0.f)
		{
			NextShot = ShotEvery;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/ks_%02d.png"), ShotIndex++), true, false);
		}
	}
}

/* ================= the street ================= */

void AKSGameMode::EnterStreet(bool bDead)
{
	ClearWorld();
	bStreetDead = bDead;
	FKSBuilder B = MakeBuilder();
	KSWorld::BuildStreet(B, Street, bDead);
	WalkPos = FVector(-250, 0, 0); WalkYaw = 0.f; WalkPitch = 0.f;
	Subtitle.Reset(); Prompt.Reset(); CueMain.Reset(); CueHint.Reset(); Announcement.Reset();
	BlackFade = 0.f;
	SetPhase(bDead ? EKSPhase::Ending : EKSPhase::Street);
}

void AKSGameMode::TickStreet(float Dt)
{
	float MouseX = 0.f, MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	WalkYaw += MouseX * 2.2f + ((Down(EKeys::Right) ? 1.f : 0.f) - (Down(EKeys::Left) ? 1.f : 0.f)) * 110.f * Dt;
	WalkPitch = FMath::Clamp(WalkPitch + MouseY * 2.2f, -55.f, 55.f);

	float Forward = (Down(EKeys::W) || Down(EKeys::Up) ? 1.f : 0.f) - (Down(EKeys::S) || Down(EKeys::Down) ? 1.f : 0.f);
	float Right = (Down(EKeys::D) ? 1.f : 0.f) - (Down(EKeys::A) ? 1.f : 0.f);
	if (bAuto) { Forward = 1.f; Right = 0.f; WalkYaw = 0.f; }
	const float Speed = Down(EKeys::LeftShift) || bAuto ? 620.f : 330.f;
	FVector Move = FRotator(0, WalkYaw, 0).RotateVector(FVector(Forward, Right, 0));
	if (!Move.IsNearlyZero())
	{
		Move = Move.GetClampedToMaxSize(1.f) * Speed * Dt;
		WalkPos += Move;
		const int32 StepBefore = FMath::FloorToInt(StepPhase / PI);
		StepPhase += Move.Size() / 42.f;
		if (FMath::FloorToInt(StepPhase / PI) != StepBefore) Sfx(EKSSfx::Step, 0.6f);
	}
	WalkPos.X = FMath::Clamp(WalkPos.X, -420.f, FKSStreet::CabinetX - 95.f);
	WalkPos.Y = FMath::Clamp(WalkPos.Y, -230.f, 230.f);
	Pawn->SetView(WalkPos + FVector(0, 0, 165.f + FMath::Sin(StepPhase * 2.f) * 1.8f), FRotator(WalkPitch, WalkYaw, 0), 90.f);

	const float Dist = FVector::Dist2D(WalkPos, FVector(FKSStreet::CabinetX - 95.f, 0, 0));
	const float Near = FMath::Clamp((950.f - Dist) / 650.f, 0.f, 1.f);

	// The cabinet: a flickering screen, and a camera that wakes up as you get close.
	const float Flicker = 4.f + 1.2f * FMath::Sin(Clock * 31.f) * FMath::Sin(Clock * 7.3f) + (FMath::Frac(Clock * 0.37f) > 0.96f ? -3.f : 0.f);
	if (Street.Screen) Street.Screen->SetScalarParameterValue(TEXT("Glow"), Flicker);
	if (Street.ScreenLight) Street.ScreenLight->PointLightComponent->SetIntensity(14.f * Flicker);
	if (Street.Eye)
	{
		Street.Eye->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.03f, 0.02f));
		Street.Eye->SetScalarParameterValue(TEXT("Glow"), Near * (10.f + 4.f * FMath::Sin(Clock * 9.f)));
	}
	if (Street.EyeLight) Street.EyeLight->PointLightComponent->SetIntensity(Near * 30.f);
	if (Street.ScreenText) Street.ScreenText->GetTextRender()->SetText(FText::FromString(Near > 0.45f ? TEXT("I SEE\nYOU") : TEXT("INSERT\nYOURSELF")));

	const float X = WalkPos.X;
	Subtitle = X < 500 ? TEXT("1987. The fighting game KO-SEKAI is cancelled three weeks before launch.")
		: X < 1250 ? TEXT("Its whole roster is cut. Nobody ever gets to play them.")
		: X < 2000 ? TEXT("Every cabinet is recalled and scrapped.")
		: X < 2700 ? TEXT("Every cabinet but one.")
		: Dist > 300 ? TEXT("It is still switched on.")
		: TEXT("Its camera is looking back at you.");

	const bool bClose = Dist < 300.f;
	const bool bTracked = TrackingSubsystem && TrackingSubsystem->IsTracking();
	Prompt = bClose ? (bTracked ? TEXT("Raise your guard to step up to it   (or press E)") : TEXT("Press E to step up to it"))
		: PhaseTime < 9.f ? TEXT("W A S D  walk     MOUSE  look     SHIFT  run") : TEXT("");
	if (bClose && (Pressed(EKeys::E) || Pressed(EKeys::Enter) || (bTracked && TrackingSubsystem->IsHeld(EKSDefense::Block)) || bAuto))
	{
		PullFrom = Pawn->GetActorLocation();
		PullFromRot = Pawn->GetActorRotation();
		Sfx(EKSSfx::Coin);
		Sfx(EKSSfx::PullIn);
		Prompt.Reset();
		SetPhase(EKSPhase::PullIn);
	}
}

void AKSGameMode::TickPullIn(float Dt)
{
	const float U = FMath::Clamp(PhaseTime / 3.f, 0.f, 1.f), Ease = U * U * U;
	const FVector To = Street.ScreenPos + FVector(-5, 0, 0);
	FRotator Rot = FMath::Lerp(PullFromRot, FRotator::ZeroRotator, FMath::Min(1.f, U * 3.f));
	Rot.Roll = 22.f * FMath::Sin(U * 9.f) * Ease;
	const FVector Jitter = FMath::VRand() * (1.5f * U);
	Pawn->SetView(FMath::Lerp(PullFrom, To, Ease) + Jitter, Rot, 90.f + 65.f * Ease);
	if (Street.Screen) Street.Screen->SetScalarParameterValue(TEXT("Glow"), 4.f + Ease * 60.f);
	if (Street.ScreenLight) Street.ScreenLight->PointLightComponent->SetIntensity(57.f + Ease * 1300.f);
	if (Street.Eye) Street.Eye->SetScalarParameterValue(TEXT("Glow"), 30.f);
	Subtitle = U < 0.45f ? TEXT("PLAYER 1 DETECTED") : TEXT("It pulls.");
	WhiteFade = FMath::Clamp((U - 0.62f) / 0.38f, 0.f, 1.f);
	if (U >= 1.f)
	{
		WhiteFade = 1.f;
		Subtitle.Reset();
		EnterArena();
		StartCard(0);
	}
}

/* ================= inside the cabinet ================= */

void AKSGameMode::EnterArena()
{
	ClearWorld();
	FKSBuilder B = MakeBuilder();
	KSWorld::BuildArena(B, Arena);
	PlayerDummy = GetWorld()->SpawnActor<AKSDummy>(FVector::ZeroVector, FRotator::ZeroRotator);
	PlayerDummy->Setup(KSColors::Cyan, KSColors::Magenta, true);
	OppDummy = GetWorld()->SpawnActor<AKSDummy>(FVector(OppDistance, 0, 0), FRotator(0, 180, 0));
	WorldActors.Add(PlayerDummy);
	WorldActors.Add(OppDummy);
	Subtitle.Reset(); Prompt.Reset();
}

void AKSGameMode::StartCard(int32 Index)
{
	FighterIndex = Index;
	const FKSFighterDef& F = Fighter();
	OppDummy->Setup(F.Body, F.Gloves, false, 0.25f);
	OppDummy->SetActorScale3D(FVector(F.Height));
	OppDummy->Snap(KSPoses::Guard());
	PlayerDummy->Snap(KSPoses::Guard());
	if (Arena.NameSign) Arena.NameSign->GetTextRender()->SetText(FText::FromString(F.Name));

	PlayerHP = PlayerTrail = 100.f;
	OppHP = OppMaxHP = OppTrail = F.Health;
	TimeLeft = RoundSeconds;
	Combo = 0; Stats = FKSStats();
	Pending.Reset(); Popups.Reset();
	OppState = EKSOppState::Idle; OppT = Clock; OppUntil = Clock + 1000.f;
	OpenHits = BlockStreak = 0;
	OppGuardUntil = OppHurtUntil = PlayerHurtUntil = 0.f;
	PunchStart[0] = PunchStart[1] = -10.f;
	CueMain.Reset(); CueHint.Reset(); Announcement.Reset();
	bFightStarted = false;
	BlackFade = 0.f;
	Sfx(EKSSfx::Coin);
	SetPhase(EKSPhase::Card);
}

void AKSGameMode::TickCard(float Dt)
{
	if (PhaseTime > 0.6f && PhaseTime < 2.6f && ConfirmPressed()) PhaseTime = 2.6f;
	if (PhaseTime >= 2.6f && !bFightStarted)
	{
		bFightStarted = true;
		Announce(FString::Printf(TEXT("FIGHT %d OF %d"), FighterIndex + 1, Roster().Num()), 1.3f);
	}
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	if (PhaseTime >= 4.1f) StartFight();
}

void AKSGameMode::StartFight()
{
	Sfx(EKSSfx::Bell1);
	Announce(TEXT("FIGHT!"), 0.8f);
	SetPhase(EKSPhase::Fight);
	OppIdle(1.2f);
}

void AKSGameMode::UpdateFightCamera(float Dt)
{
	// Over the player's right shoulder, looking at the opponent.
	const FVector FightPos(-215.f, 118.f, 212.f), FightAim(OppDistance, -8.f, 128.f);
	FVector Pos = FightPos, Aim = FightAim;
	float Fov = 62.f;
	if (Phase == EKSPhase::Card)
	{
		// The VS card: start close on the opponent's face and pull back to the fighting view.
		const float U = FMath::SmoothStep(1.6f, 3.6f, PhaseTime);
		const float Swing = -0.9f + PhaseTime * 0.25f;
		const FVector Close(OppDistance - 330.f * FMath::Cos(Swing), -330.f * FMath::Sin(Swing), 150.f);
		Pos = FMath::Lerp(Close, FightPos, U);
		Aim = FMath::Lerp(FVector(OppDistance, 0, 118.f * Fighter().Height), FightAim, U);
		Fov = FMath::Lerp(50.f, 62.f, U);
	}
	else if (Phase == EKSPhase::FightEnd || Phase == EKSPhase::Result || Phase == EKSPhase::Continue)
	{
		const float U = FMath::SmoothStep(0.f, 2.f, PhaseTime);
		Pos = FMath::Lerp(FightPos, FightPos + FVector(60, 40, -50), U);
		Aim = FMath::Lerp(FightAim, FVector(OppDistance * 0.7f, 0, 90.f), U);
	}
	Pos += FMath::VRand() * Shake * 0.6f;
	Pawn->SetView(Pos, (Aim - Pos).Rotation(), Fov);
}

void AKSGameMode::UpdateFighterPoses(float Dt)
{
	if (!PlayerDummy || !OppDummy) return;

	/* ---- the player ---- */
	FKSPose P = KSPoses::Guard();
	const bool bLost = (Phase == EKSPhase::FightEnd || Phase == EKSPhase::Continue) && !bLastResultWin;
	const bool bWon = (Phase == EKSPhase::FightEnd || Phase == EKSPhase::Result) && bLastResultWin;
	const bool bTracked = TrackingSubsystem && TrackingSubsystem->HasLandmarks();
	if (bLost && !bLastResultDecision)
	{
		PlayerDummy->SetTarget(KSPoses::KO(), 5.f);
	}
	else
	{
		const EKSDefense D = Phase == EKSPhase::Fight ? Defense() : EKSDefense::None;
		if (D == EKSDefense::Block && !bTracked) P = KSPoses::Block();
		// The player's own left is -Y.
		if (D == EKSDefense::EvadeLeft) KSPoses::Lean(P, -38.f, 6.f);
		if (D == EKSDefense::EvadeRight) KSPoses::Lean(P, 38.f, 6.f);
		if (D == EKSDefense::Duck) KSPoses::Lean(P, 0.f, 46.f);
		if (Clock < PlayerHurtUntil) KSPoses::Lean(P, 0.f, 4.f, 16.f);
		if (bTracked)
		{
			// The webcam tracker drives the upper body directly: the dummy copies your arms and head.
			KSPoses::ApplyTracked(P, TrackingSubsystem->GetWorldLandmarks());
			PlayerDummy->SetTarget(P, 28.f);
		}
		else
		{
			if (bWon) P = KSPoses::Victory();
			for (int32 i = 0; i < 2; i++)
			{
				const float T = Clock - PunchStart[i];
				if (T >= 0.f && T < 0.28f)
				{
					const float W = T < 0.08f ? T / 0.08f : 1.f - (T - 0.08f) / 0.2f;
					P = KSPoses::Blend(P, KSPoses::Strike(PunchType[i], (EKSArm)i), FMath::Clamp(W, 0.f, 1.f));
				}
			}
			PlayerDummy->SetTarget(P, 26.f);
		}
	}

	/* ---- the opponent ---- */
	const EKSArm OppArm = Attack.Side;
	FKSPose O = KSPoses::Guard();
	float Speed = 14.f;
	switch (OppState)
	{
	case EKSOppState::Idle:
		if (Clock < OppGuardUntil) O = KSPoses::Block();
		break;
	case EKSOppState::Windup:
	{
		const float U = FMath::Clamp((Clock - OppT) / FMath::Max(0.01f, Attack.Duration), 0.f, 1.f);
		O = KSPoses::Blend(O, KSPoses::Windup(Attack.Type, OppArm), FMath::SmoothStep(0.f, 0.7f, U));
		break;
	}
	case EKSOppState::Strike: O = KSPoses::Strike(Attack.Type, OppArm); Speed = 38.f; break;
	case EKSOppState::Open: O = KSPoses::Open(); KSPoses::Lean(O, 6.f * FMath::Sin(Clock * 5.f), 0.f); break;
	case EKSOppState::KO: O = KSPoses::KO(); Speed = 5.f; break;
	}
	if (OppState != EKSOppState::KO)
	{
		if (Clock < OppHurtUntil) { O = KSPoses::Hurt(); Speed = 30.f; }
		if (bLost) O = KSPoses::Victory();
		KSPoses::Lean(O, 2.5f * FMath::Sin(Clock * 2.3f), 1.5f + 1.5f * FMath::Sin(Clock * 4.6f));   // breathing bob
	}
	OppDummy->SetTarget(O, Speed);
}

/* ================= fight rules ================= */

void AKSGameMode::OppIdle(float Seconds)
{
	OppState = EKSOppState::Idle; OppT = Clock; OppUntil = Clock + Seconds; OppWaitUntil = Clock + Seconds + 1.5f;
	Combo = 0;
	if (Phase == EKSPhase::Fight) { CueMain.Reset(); CueHint.Reset(); }
}

void AKSGameMode::StartWindup(EKSPunch Type, float QuickSeconds)
{
	const FKSFighterDef& F = Fighter();
	const int32 T = (int32)Type;
	Attack.Type = Type;
	Attack.Side = FMath::RandBool() ? EKSArm::Left : EKSArm::Right;
	Attack.Damage = AttackDamage[T] * F.DamageScale;
	Attack.Duration = QuickSeconds > 0.f ? QuickSeconds : WindupSeconds[T] * F.WindupScale * (1.f - 0.2f * Rage());
	Attack.bFeint = QuickSeconds <= 0.f && Rage() > 0.5f && FMath::FRand() < F.FeintChance;
	OppState = EKSOppState::Windup; OppT = Clock; OppUntil = Clock + Attack.Duration; bTellBeeped = false;
	const bool bFromRight = Attack.Side == EKSArm::Left;
	if (Type == EKSPunch::Straight) Cue(TEXT("JAB"), TEXT("block, duck or evade"));
	else if (Type == EKSPunch::Hook) Cue(bFromRight ? TEXT("HOOK FROM YOUR RIGHT") : TEXT("HOOK FROM YOUR LEFT"), bFromRight ? TEXT("duck, or evade left") : TEXT("duck, or evade right"));
	else Cue(TEXT("UPPERCUT"), TEXT("evade left or right"));
}

void AKSGameMode::OpenUp(float Seconds)
{
	OppState = EKSOppState::Open; OppT = Clock; OppUntil = Clock + Seconds; OpenHits = 0;
	if (Seconds >= 0.8f) Cue(TEXT("OPEN!"), TEXT("punch now"), true);
	else { CueMain.Reset(); CueHint.Reset(); }
}

void AKSGameMode::OppAI()
{
	const FKSFighterDef& F = Fighter();
	if (OppState == EKSOppState::Idle && Clock >= OppUntil)
	{
		const EKSDefense D = Defense();
		// He waits for you to come out of a dodge, so holding one does not win the fight.
		if ((D == EKSDefense::Duck || D == EKSDefense::EvadeLeft || D == EKSDefense::EvadeRight) && Clock < OppWaitUntil) { OppUntil = Clock + 0.15f; return; }
		const bool bBlocking = Stance(EKSDefense::Block);
		const float R = FMath::FRand(), Jab = bBlocking ? 0.15f : 0.45f, Hook = bBlocking ? 0.25f : 0.35f;
		StartWindup(R < Jab ? EKSPunch::Straight : R < Jab + Hook ? EKSPunch::Hook : EKSPunch::Uppercut);
	}
	else if (OppState == EKSOppState::Windup)
	{
		const float U = (Clock - OppT) / Attack.Duration;
		if (!bTellBeeped && U > 0.5f) { bTellBeeped = true; Sfx(EKSSfx::Tell); }
		if (Attack.bFeint && U > 0.6f) { Popup(TEXT("FAKE!"), FLinearColor(1.f, 0.4f, 0.1f), 0.62f, 0.4f); OppIdle(FMath::FRandRange(0.5f, 0.9f)); return; }
		if (U >= 1.f)
		{
			OppState = EKSOppState::Strike; OppT = Clock; OppUntil = Clock + Grace; Def0 = Defense();
			Sfx(EKSSfx::Whoosh);
			CueMain.Reset(); CueHint.Reset();
		}
	}
	else if (OppState == EKSOppState::Strike && Clock >= OppUntil)
	{
		ResolveStrike();
	}
	else if (OppState == EKSOppState::Open && (Clock >= OppUntil || OpenHits >= F.MaxHitsOpen))
	{
		OppIdle(FMath::FRandRange(0.7f, 1.3f) * (1.f - 0.3f * Rage()) * F.IdleScale);
	}
}

void AKSGameMode::ResolveStrike()
{
	// Take the better of the defence held when the punch arrived and the one held now (camera delay).
	const EKSDefense D1 = Def0, D2 = Defense();
	const EOutcome O = FMath::Max(Outcome(Attack.Type, Attack.Side, D1), Outcome(Attack.Type, Attack.Side, D2));
	if (O == EOutcome::Dodged) { Stats.Dodges++; Sfx(EKSSfx::Dodge); Popup(TEXT("DODGE!"), KSColors::Green, 0.38f, 0.45f); OpenUp(1.2f); return; }
	if (O == EOutcome::Blocked) { Stats.Blocks++; Sfx(EKSSfx::Blocked); Popup(TEXT("BLOCKED"), KSColors::Lilac, 0.38f, 0.45f); OpenUp(0.55f); return; }
	if (Attack.Type == EKSPunch::Uppercut && (D1 == EKSDefense::Block || D2 == EKSDefense::Block)) Popup(TEXT("GUARD BREAK!"), KSColors::Red, 0.5f, 0.5f, true);
	PlayerHurt(Attack.Damage * (O == EOutcome::Half ? 0.5f : O == EOutcome::HitPlus ? 1.25f : 1.f), O == EOutcome::Half);
	if (PlayerHP > 0.f) OppIdle(FMath::FRandRange(0.8f, 1.2f));
}

void AKSGameMode::PlayerHurt(float Damage, bool bLight)
{
	PlayerHP = FMath::Max(0.f, PlayerHP - Damage);
	Stats.Taken++;
	Shake = bLight ? 4.f : 10.f; RedFlash = bLight ? 0.4f : 1.f; Combo = 0;
	PlayerHurtUntil = Clock + 0.3f;
	Sfx(EKSSfx::Hurt);
	if (PlayerDummy) PlayerDummy->Flash(KSColors::Red, 0.12f);
	if (PlayerHP <= 0.f) EndFight(false, false);
}

void AKSGameMode::PlayerPunch(EKSArm Arm, EKSPunch Type)
{
	PunchStart[(int32)Arm] = Clock;
	PunchType[(int32)Arm] = Type;
	if (Phase != EKSPhase::Fight) return;
	float Mult = 0.f;
	bool bCounter = false;
	if (OppState == EKSOppState::Open && OpenHits < Fighter().MaxHitsOpen) { Mult = 1.f; OpenHits++; }
	else if (OppState == EKSOppState::Windup && (Clock - OppT) / Attack.Duration >= 0.35f)
	{
		Mult = 2.5f; bCounter = true; Stats.Counters++; OpenUp(1.0f);
	}
	Pending.Add({ Clock + 0.09f, Arm, Type, Mult, bCounter });
	if (Mult == 0.f && OppState == EKSOppState::Idle)
	{
		// Punching into his guard three times makes him snap back with a quick jab.
		if (++BlockStreak >= 3) { BlockStreak = 0; StartWindup(EKSPunch::Straight, 0.42f); }
	}
}

void AKSGameMode::LandHit(const FPendingHit& Hit)
{
	if (Phase != EKSPhase::Fight) return;
	if (Hit.Mult == 0.f)
	{
		OppGuardUntil = Clock + 0.22f;
		Sfx(EKSSfx::Blocked);
		Popup(TEXT("BLOCKED"), KSColors::Muted, 0.56f, 0.34f);
		return;
	}
	OppHP = FMath::Max(0.f, OppHP - PunchDamage[(int32)Hit.Type] * Hit.Mult);
	Stats.Landed++; Combo++; Stats.BestCombo = FMath::Max(Stats.BestCombo, Combo);
	OppHurtUntil = Clock + 0.24f;
	if (OppDummy) OppDummy->Flash(Hit.bCounter ? KSColors::Yellow : FLinearColor::White, 0.08f);
	Shake = FMath::Max(Shake, Hit.bCounter ? 7.f : 4.f);
	if (Hit.bCounter) { Sfx(EKSSfx::Counter); Popup(TEXT("COUNTER!"), KSColors::Yellow, 0.56f, 0.3f, true); }
	else { Sfx(EKSSfx::Hit); if (Combo > 1) Popup(FString::Printf(TEXT("%d HITS"), Combo), KSColors::Cyan, 0.66f, 0.38f); }
	if (OppHP <= 0.f) EndFight(true, false);
}

void AKSGameMode::EndFight(bool bWin, bool bDecision)
{
	bLastResultWin = bWin; bLastResultDecision = bDecision;
	CueMain.Reset(); CueHint.Reset(); Pending.Reset();
	if (bDecision) { Sfx(EKSSfx::Bell2); Announce(TEXT("TIME!"), 1.8f); }
	else if (bWin) { OppState = EKSOppState::KO; OppT = Clock; Sfx(EKSSfx::KO); Announce(TEXT("K.O.!"), 2.3f); }
	else { Announce(TEXT("DOWN!"), 2.2f); }
	SetPhase(EKSPhase::FightEnd);
}

void AKSGameMode::TickFight(float Dt)
{
	if (!bPaused)
	{
		if (bAuto)
		{
			// Test bot: defends correctly and punches every opening.
			AutoDefense = EKSDefense::None;
			if (OppState == EKSOppState::Windup || OppState == EKSOppState::Strike)
			{
				AutoDefense = Attack.Type == EKSPunch::Straight ? EKSDefense::Block
					: Attack.Type == EKSPunch::Hook ? (Attack.Side == EKSArm::Left ? EKSDefense::EvadeLeft : EKSDefense::EvadeRight) : EKSDefense::EvadeLeft;
			}
			if (OppState == EKSOppState::Open && Clock >= AutoNextPunch)
			{
				AutoNextPunch = Clock + 0.3f;
				FramePunches.Add({ FMath::RandBool() ? EKSArm::Left : EKSArm::Right, (EKSPunch)FMath::RandRange(0, 2) });
			}
		}
		for (const TPair<EKSArm, EKSPunch>& Punch : FramePunches) PlayerPunch(Punch.Key, Punch.Value);
		if (Phase == EKSPhase::Fight) OppAI();
		for (int32 i = Pending.Num() - 1; i >= 0 && Phase == EKSPhase::Fight; i--)
		{
			if (Clock >= Pending[i].At) { const FPendingHit Hit = Pending[i]; Pending.RemoveAt(i); LandHit(Hit); }
		}
		if (Phase == EKSPhase::Fight)
		{
			TimeLeft -= Dt;
			if (TimeLeft <= 0.f) EndFight(PlayerHP > OppHP / OppMaxHP * 100.f, true);
		}
		PlayerTrail = FMath::Max(PlayerHP, PlayerTrail - 25.f * Dt);
		OppTrail = FMath::Max(OppHP, OppTrail - 25.f * Dt);
	}
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
}

void AKSGameMode::TickFightEnd(float Dt)
{
	PlayerTrail = FMath::Max(PlayerHP, PlayerTrail - 25.f * Dt);
	OppTrail = FMath::Max(OppHP, OppTrail - 25.f * Dt);
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	if (PhaseTime >= 2.7f)
	{
		if (bLastResultWin) { Sfx(EKSSfx::Bell3); SetPhase(EKSPhase::Result); }
		else { ContinueLeft = 9.99f; SetPhase(EKSPhase::Continue); }
	}
}

void AKSGameMode::TickResult(float Dt)
{
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	if ((PhaseTime > 1.2f && ConfirmPressed()) || (bAuto && PhaseTime > 2.5f))
	{
		if (FighterIndex + 1 < Roster().Num()) StartCard(FighterIndex + 1);
		else { WhiteFade = 1.f; EnterStreet(true); }
	}
}

void AKSGameMode::TickContinue(float Dt)
{
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	ContinueLeft -= Dt;
	BlackFade = FMath::Clamp(1.f - ContinueLeft / 10.f, 0.f, 0.85f);
	if ((PhaseTime > 1.0f && ConfirmPressed()) || (bAuto && PhaseTime > 2.f)) { StartCard(FighterIndex); return; }
	if (ContinueLeft <= 0.f) EnterStreet(false);
}

/* ================= the ending ================= */

void AKSGameMode::TickEnding(float Dt)
{
	const float U = FMath::SmoothStep(0.f, 14.f, PhaseTime);
	const FVector Pos = FMath::Lerp(FVector(FKSStreet::CabinetX - 210.f, 40.f, 160.f), FVector(FKSStreet::CabinetX - 620.f, 110.f, 172.f), U);
	Pawn->SetView(Pos, (FVector(FKSStreet::CabinetX, 0, 120.f) - Pos).Rotation(), 75.f);
	Subtitle = PhaseTime < 4.f ? TEXT("You are back on the street. The screen is dark.")
		: PhaseTime < 8.f ? TEXT("Four fighters nobody was meant to see finally got their match.")
		: PhaseTime < 12.f ? TEXT("The cabinet has what it waited thirty-nine years for: a player.")
		: TEXT("KO-SEKAI   -   thank you for playing");
	Prompt = PhaseTime > 6.f ? TEXT("Press ENTER to walk the street again") : TEXT("");
	if ((PhaseTime > 6.f && ConfirmPressed()) || (bAuto && PhaseTime > 16.f)) EnterStreet(false);
}
