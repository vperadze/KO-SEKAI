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
	const float OppDistance = 150.f;      // cm between the two fighters (the real models lunge further than the dummies did)
	const float WindupSeconds[] = { 0.65f, 0.85f, 0.95f };   // jab, hook, uppercut
	const float AttackDamage[] = { 7.f, 12.f, 16.f };
	const float PunchDamage[] = { 3.f, 4.f, 5.f };            // straight, hook, uppercut

	// How hard each fight on the ladder is, whoever the opponent turns out to be.
	struct FLadderTier { float Health, WindupScale, DamageScale, FeintChance, IdleScale; int32 MaxHitsOpen; };
	const FLadderTier LadderTiers[] = {
		{ 100.f, 1.00f, 1.00f, 0.20f, 1.00f, 3 },
		{ 120.f, 0.88f, 1.15f, 0.28f, 0.85f, 3 },
		{ 140.f, 0.74f, 1.22f, 0.38f, 0.65f, 3 },
	};
	const int32 LadderLength = UE_ARRAY_COUNT(LadderTiers);

	// The fighters' punch clips, and the moment in each (seconds) when the fist is fully out.
	struct FPunchClip { const TCHAR* Name; float Impact; };
	const FPunchClip PunchClips[3][2] = {
		{ { TEXT("Left_Jab_from_Guard"), 0.50f },      { TEXT("Right_Jab_from_Guard"), 0.63f } },
		{ { TEXT("Left_Hook_from_Guard"), 0.33f },     { TEXT("Right_Upper_Hook_from_Guard"), 0.67f } },
		{ { TEXT("Left_Uppercut_from_Guard"), 0.37f }, { TEXT("Right_Uppercut_from_Guard"), 0.70f } },
	};
	const float PlayerPunchSeconds = 0.40f;   // how long the player's fighter stays in a punch clip

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
	// The cut roster. Health and the timing numbers here are only defaults: each fight takes its difficulty from
	// its place on the ladder (see LadderTiers), so any fighter can turn up first or last.
	static const TArray<FKSFighterDef> Fighters = {
		{ TEXT("SERGEANT MULLET"), TEXT("Cut by legal. Never told why."),
		  TEXT("The Sergeant salutes. First time anyone has seen him do it."),
		  FLinearColor(0.20f, 0.30f, 0.10f), KSColors::Red, 120.f, 0.90f, 1.15f, 0.20f, 0.95f, 3, 1.00f, TEXT("SergeantMulletV2") },
		{ TEXT("CHANNEL ZERO"), TEXT("His sprites were recorded over."),
		  TEXT("The static clears. For a second the screen shows a smile."),
		  FLinearColor(0.35f, 0.45f, 0.65f), KSColors::Green, 100.f, 1.00f, 1.00f, 0.20f, 1.00f, 3, 0.94f, TEXT("ChannelZero") },
		{ TEXT("SIR LASERCORN"), TEXT("Focus groups found him too majestic."),
		  TEXT("Sir Lasercorn bows. The horn dims, just a little."),
		  FLinearColor(0.95f, 0.55f, 0.80f), KSColors::Cyan, 130.f, 0.85f, 1.20f, 0.25f, 0.90f, 3, 1.06f, TEXT("SirLasercorn") },
		{ TEXT("AEROBICS ANNIHILATOR"), TEXT("Her workout tape outsold the game."),
		  TEXT("And rest. She finally lets you rest."),
		  FLinearColor(0.55f, 0.20f, 0.75f), KSColors::Magenta, 105.f, 0.92f, 0.95f, 0.25f, 0.85f, 3, 0.94f, TEXT("AerobicsAnnihilator") },
		{ TEXT("GRANDMA THUNDER"), TEXT("Removed after the playtesters cried."),
		  TEXT("Grandma Thunder pats your cheek. It still hurts."),
		  FLinearColor(0.60f, 0.45f, 0.40f), KSColors::Yellow, 150.f, 0.70f, 1.25f, 0.40f, 0.60f, 3, 0.83f, TEXT("GrandmaThunder") },
		{ TEXT("TURBOTRON"), TEXT("Shipped without batteries. Then not shipped."),
		  TEXT("TURBOTRON displays a heart, then pretends it did not."),
		  FLinearColor(0.60f, 0.62f, 0.66f), FLinearColor(1.0f, 0.45f, 0.10f), 140.f, 0.95f, 1.30f, 0.15f, 1.00f, 2, 1.00f, TEXT("Turbotron") },
		{ TEXT("COUNT HAIRSPRAY"), TEXT("Too much hair for 16 kilobytes."),
		  TEXT("The Count takes a bow and checks his hair in your reflection."),
		  FLinearColor(0.10f, 0.08f, 0.12f), FLinearColor(0.55f, 0.20f, 0.85f), 125.f, 0.72f, 1.00f, 0.40f, 0.70f, 2, 1.08f, TEXT("CountHairspray") },
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
	bAutoNoGuard = FParse::Param(Cmd, TEXT("KSNoGuard"));
	bFakeArms = FParse::Param(Cmd, TEXT("KSFakeArms"));
	bCalDemo = FParse::Param(Cmd, TEXT("KSCalDemo"));
	bMirrorArms = bFakeArms || FParse::Param(Cmd, TEXT("KSMirrorArms"));
	bHitShots = FParse::Param(Cmd, TEXT("KSHitShots"));
	FParse::Value(Cmd, TEXT("KSShots="), ShotEvery);
	FString Start;
	FParse::Value(Cmd, TEXT("KSStart="), Start);
	if (Start == TEXT("fight"))
	{
		FParse::Value(Cmd, TEXT("KSFighter="), ForcedOpponent);   // roster index of the first opponent
		FParse::Value(Cmd, TEXT("KSPlayer="), ForcedPlayer);      // roster index of the player's fighter
		EnterArena();
		BeginLadder();
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
	Spectators.Reset(); Flashes.Reset(); FlashLights.Reset(); FlashesOwed = 0.f;
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
	if (Phase == EKSPhase::Fight || Phase == EKSPhase::Card || Phase == EKSPhase::Calibrate)
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
	case EKSPhase::Calibrate: TickCalibrate(GameDt); break;
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
	// -KSHitShots: a burst of frames after each hit, so the reaction can be checked.
	for (int32 i = BurstShots.Num() - 1; i >= 0; i--)
	{
		if (Clock >= BurstShots[i].Key)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / BurstShots[i].Value, true, false);
			BurstShots.RemoveAt(i);
			break;   // one screenshot per frame
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
		BeginLadder();
	}
}

/* ================= inside the cabinet ================= */

void AKSGameMode::EnterArena()
{
	ClearWorld();
	FKSBuilder B = MakeBuilder();
	KSWorld::BuildArena(B, Arena);
	PickFighters();
	PlayerDummy = GetWorld()->SpawnActor<AKSDummy>(FVector::ZeroVector, FRotator::ZeroRotator);
	PlayerDummy->Setup(KSColors::Cyan, KSColors::Magenta, true);
	if (PlayerFighter().Asset) PlayerDummy->UseCharacter(PlayerFighter().Asset);
	OppDummy = GetWorld()->SpawnActor<AKSDummy>(FVector(OppDistance, 0, 0), FRotator(0, 180, 0));
	WorldActors.Add(PlayerDummy);
	WorldActors.Add(OppDummy);
	Subtitle.Reset(); Prompt.Reset();
}

void AKSGameMode::PickFighters()
{
	// A random fighter for the player, then a random ladder of different opponents.
	const int32 Count = Roster().Num();
	PlayerChar = ForcedPlayer >= 0 ? FMath::Clamp(ForcedPlayer, 0, Count - 1) : FMath::RandRange(0, Count - 1);
	TArray<int32> Others;
	for (int32 i = 0; i < Count; i++) if (i != PlayerChar) Others.Add(i);
	for (int32 i = Others.Num() - 1; i > 0; i--) Others.Swap(i, FMath::RandRange(0, i));
	if (ForcedOpponent >= 0)
	{
		const int32 Wanted = FMath::Clamp(ForcedOpponent, 0, Count - 1);
		Others.Remove(Wanted);
		Others.Insert(Wanted, 0);
	}
	Ladder.Reset();
	for (int32 i = 0; i < LadderLength && i < Others.Num(); i++) Ladder.Add(Others[i]);
}

void AKSGameMode::StartCard(int32 Index)
{
	FighterIndex = FMath::Clamp(Index, 0, Ladder.Num() - 1);
	CurrentDef = Roster()[Ladder[FighterIndex]];
	const FLadderTier& Tier = LadderTiers[FMath::Min(FighterIndex, LadderLength - 1)];
	CurrentDef.Health = Tier.Health; CurrentDef.WindupScale = Tier.WindupScale; CurrentDef.DamageScale = Tier.DamageScale;
	CurrentDef.FeintChance = Tier.FeintChance; CurrentDef.IdleScale = Tier.IdleScale; CurrentDef.MaxHitsOpen = Tier.MaxHitsOpen;
	const FKSFighterDef& F = Fighter();
	// A fresh actor for every bout. Putting a second fighter's mesh on the old actor left the drawn body frozen in
	// one pose (the animation ran, the picture did not follow), so the opponent is spawned new, as in the first fight.
	if (OppDummy) { WorldActors.Remove(OppDummy); OppDummy->Destroy(); }
	OppDummy = GetWorld()->SpawnActor<AKSDummy>(FVector(OppDistance, 0, 0), FRotator(0, 180, 0));
	WorldActors.Add(OppDummy);
	OppDummy->Setup(F.Body, F.Gloves, false, 0.25f);
	const bool bReal = F.Asset && OppDummy->UseCharacter(F.Asset);
	OppDummy->SetActorScale3D(FVector(bReal ? 1.f : F.Height));   // the real models are already the right size
	OppClipKey.Reset(); PlayerClipKey.Reset();
	OppDummy->Snap(KSPoses::Guard());
	PlayerDummy->Snap(KSPoses::Guard());
	if (Arena.NameSign) Arena.NameSign->GetTextRender()->SetText(FText::FromString(F.Name));
	SeatCrowd();

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
	if (PlayerDummy) PlayerDummy->StopTalking();
	bIntroSaid = false; VoiceQuietUntil = 0.f; LaterVoice = FLaterVoice();
	SetPhase(EKSPhase::Card);
}

void AKSGameMode::TickCard(float Dt)
{
	if (PhaseTime > 0.6f && PhaseTime < 2.6f && ConfirmPressed()) PhaseTime = 2.6f;
	if (PhaseTime >= 2.6f && !bFightStarted)
	{
		bFightStarted = true;
		Announce(FString::Printf(TEXT("FIGHT %d OF %d"), FighterIndex + 1, NumFights()), 1.3f);
	}
	if (!bIntroSaid && PhaseTime >= 0.5f) { bIntroSaid = true; Speak(OppDummy, TEXT("intro"), true); }
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	// The bell waits for him to finish his sentence (within reason).
	if (PhaseTime >= 4.1f && (!OppDummy || !OppDummy->IsTalking() || PhaseTime >= 7.5f)) StartFight();
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
		// After the bell the camera drifts round the two fighters instead of sitting still.
		const float U = FMath::SmoothStep(0.f, 2.f, PhaseTime);
		const float Orbit = FMath::DegreesToRadians(14.f * FMath::Sin(PhaseTime * 0.35f));
		const FVector Pivot(OppDistance * 0.6f, 0.f, 0.f), Arm = FightPos + FVector(60, 40, -50) - Pivot;
		const FVector Round(Pivot.X + Arm.X * FMath::Cos(Orbit) - Arm.Y * FMath::Sin(Orbit), Pivot.Y + Arm.X * FMath::Sin(Orbit) + Arm.Y * FMath::Cos(Orbit), Arm.Z);
		Pos = FMath::Lerp(FightPos, Round, U);
		Aim = FMath::Lerp(FightAim, FVector(OppDistance * 0.7f, 0, 90.f), U);
	}

	/* ---- what the fight itself does to the camera ---- */
	float WantDolly = 0.f, WantSide = 0.f, WantDrop = 0.f, WantFov = 0.f;
	if (Phase == EKSPhase::Fight)
	{
		// Leans in when the opponent is open or a combo is running, backs off as he winds up.
		if (OppState == EKSOppState::Open) { WantDolly = 48.f; WantFov = -4.f; }
		else if (OppState == EKSOppState::Windup) { WantDolly = -24.f; WantFov = 2.5f; }
		WantDolly += FMath::Min(30.f, 6.f * Combo);
		// Moves with the player: slides when he leans, sinks when he ducks, tightens when he covers up.
		switch (Defense())
		{
		case EKSDefense::EvadeLeft: WantSide = -38.f; break;
		case EKSDefense::EvadeRight: WantSide = 38.f; break;
		case EKSDefense::Duck: WantDrop = 34.f; break;
		case EKSDefense::Block: WantDolly += 14.f; break;
		default: break;
		}
		// Never quite still: a slow circling drift, and a nervous hand when the player is nearly out.
		const float Orbit = FMath::DegreesToRadians(5.f * FMath::Sin(Clock * 0.33f) + 2.f * FMath::Sin(Clock * 0.71f + 1.3f));
		const FVector Pivot(OppDistance * 0.5f, 0.f, 0.f), Arm = Pos - Pivot;
		Pos = FVector(Pivot.X + Arm.X * FMath::Cos(Orbit) - Arm.Y * FMath::Sin(Orbit), Pivot.Y + Arm.X * FMath::Sin(Orbit) + Arm.Y * FMath::Cos(Orbit), Arm.Z + 6.f * FMath::Sin(Clock * 0.52f));
		if (PlayerHP < 30.f) Pos += FVector(FMath::Sin(Clock * 7.3f), FMath::Sin(Clock * 9.1f + 2.f), FMath::Sin(Clock * 8.2f + 4.f)) * (2.5f * (1.f - PlayerHP / 30.f));
		// Keeps the opponent's head in the picture as he sways and reels.
		if (OppDummy) CamAimLag = FMath::VInterpTo(CamAimLag, (OppDummy->FaceWorld(false) - FVector(OppDistance, 0.f, 150.f)) * 0.35f, Dt, 5.f);
	}
	else CamAimLag = FMath::VInterpTo(CamAimLag, FVector::ZeroVector, Dt, 3.f);
	CamDolly = FMath::FInterpTo(CamDolly, WantDolly, Dt, 3.2f);
	CamSide = FMath::FInterpTo(CamSide, WantSide, Dt, 6.f);
	CamDrop = FMath::FInterpTo(CamDrop, WantDrop, Dt, 6.f);
	// Hits push on a spring, so the camera lurches and settles instead of jittering.
	const float Step = FMath::Min(Dt, 0.033f);
	CamVel += (-70.f * CamOffset - 11.f * CamVel) * Step;
	CamOffset += CamVel * Step;
	CamRollVel += (-110.f * CamRoll - 12.f * CamRollVel) * Step;
	CamRoll += CamRollVel * Step;
	CamFovKick = FMath::FInterpTo(CamFovKick, 0.f, Dt, 7.f);

	const FVector Forward = (Aim - Pos).GetSafeNormal2D(), Right(-Forward.Y, Forward.X, 0.f);
	const FVector Move = Forward * (CamDolly + CamOffset.X) + Right * (CamSide + CamOffset.Y) + FVector(0, 0, CamOffset.Z - CamDrop);
	Pos += Move;
	Aim += Move * 0.35f + CamAimLag + FVector(0, 0, -CamDrop * 0.5f);
	Pos += FMath::VRand() * Shake * 0.6f;
	FRotator Rot = (Aim - Pos).Rotation();
	Rot.Roll = CamRoll;
	Pawn->SetView(Pos, Rot, Fov + WantFov * FMath::Clamp(FMath::Abs(CamDolly) / 24.f, 0.f, 1.f) + CamFovKick);
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
	DriveCharacters();
	TickCrowd();
	if (Phase == EKSPhase::Fight && OppDummy && PlayerDummy) { TestOppHand += OppDummy->DrawnHandFromHead(); TestPlayerHand += PlayerDummy->DrawnHandFromHead(); }
}

/** Picks the clip and the face for each real fighter from the state of the fight. A clip only restarts when its key changes. */
/* ================= calibration ================= */

void AKSGameMode::BeginLadder()
{
	// Webcam players are first shown where to stand and taken through each move once. It also teaches the detector
	// how hard this player punches. Keyboard players, the self-playing test and a second run skip it.
	const bool bCamera = TrackingSubsystem && TrackingSubsystem->HasBuiltInTracker();
	if (bCalDemo || (bCamera && !bAuto && !bCalibrationDone && !FParse::Param(FCommandLine::Get(), TEXT("KSNoCal"))))
	{
		CalPeaks.Reset();
		SetPhase(EKSPhase::Calibrate);
		SetCalStep(0);
		return;
	}
	StartCard(0);
}

void AKSGameMode::SetCalStep(int32 Step)
{
	static const TCHAR* Names[CalSteps] = { TEXT("framing"), TEXT("guard"), TEXT("left jabs"), TEXT("right jabs"), TEXT("block"), TEXT("lean left"), TEXT("lean right"), TEXT("duck"), TEXT("ready") };
	CalStep = FMath::Clamp(Step, 0, CalSteps - 1);
	CalStepTime = 0.f; CalHold = 0.f; CalCount = 0; CalProgress = 0.f; CalDoneAt = -1.f; bCalRepCounted = false; CalCountNow = CalCountNeed = 0;
	if (TrackingSubsystem)
	{
		TrackingSubsystem->Mark(FString::Printf(TEXT("calibration step %d %s"), CalStep, Names[CalStep]));
		if (CalStep == 1) TrackingSubsystem->Recalibrate();
		if (CalStep == 4 && CalPeaks.Num() >= 4)
		{
			// The jabs just thrown show how fast this player's punches are. The detector's speed threshold is set to a
			// little over half of that, within limits, so a light puncher is heard and a hard one gets no phantom hits.
			CalPeaks.Sort();
			const float Median = CalPeaks[CalPeaks.Num() / 2];
			const float Sensitivity = FMath::Clamp(3.5f / (0.55f * Median), 0.8f, 1.5f);
			TrackingSubsystem->SetSensitivity(Sensitivity);
			TrackingSubsystem->Mark(FString::Printf(TEXT("sensitivity %.2f from %d strokes, median peak %.1f"), Sensitivity, CalPeaks.Num(), Median));
			UE_LOG(LogTemp, Display, TEXT("KSTEST calibration sensitivity=%.2f strokes=%d median_peak=%.1f"), Sensitivity, CalPeaks.Num(), Median);
		}
	}
	if (CalStep > 0) Sfx(EKSSfx::Coin, 0.6f);
}

void AKSGameMode::FinishCalibration(bool bCompleted)
{
	bCalibrationDone = true;
	if (TrackingSubsystem) TrackingSubsystem->Mark(bCompleted ? TEXT("calibration finished") : TEXT("calibration skipped"));
	UE_LOG(LogTemp, Display, TEXT("KSTEST calibration done completed=%d"), bCompleted ? 1 : 0);
	CalClipKey.Reset(); CalChecks.Reset(); CalTitle.Reset(); CalHint.Reset();
	StartCard(0);
}

void AKSGameMode::TickCalibrate(float Dt)
{
	CalStepTime += Dt;
	// The camera faces the player's own fighter, who copies the moves as they are recognised.
	const FVector Pos(300.f, 40.f, 140.f), Aim(0.f, 95.f, 96.f);   // aimed to one side, so the fighter stands right of the mirror
	Pawn->SetView(Pos, (Aim - Pos).Rotation(), 55.f);
	if (OppDummy) OppDummy->SetActorHiddenInGame(true);

	UKSTrackingSubsystem* T = TrackingSubsystem;
	if (Pressed(EKeys::K)) { FinishCalibration(false); return; }
	const bool bSkip = Pressed(EKeys::SpaceBar) || Pressed(EKeys::Enter) || (bCalDemo && CalStepTime > 3.0f);

	/* ---- where the player stands ---- */
	CalChecks.Reset();
	FString Framing;
	bool bFramed = false;
	const TArray<FVector2f>* L = T ? &T->GetImageLandmarks() : nullptr;
	if (!T || !T->IsConnected()) Framing = PhaseTime > 6.f ? TEXT("NO CAMERA FOUND  -  PRESS K TO PLAY WITH THE KEYBOARD") : TEXT("LOOKING FOR THE CAMERA...");
	else if (!T->IsBodySeen() || L->Num() < 33) Framing = TEXT("STEP INTO VIEW");
	else
	{
		const float ShoulderY = ((*L)[11].Y + (*L)[12].Y) * 0.5f, MidX = ((*L)[11].X + (*L)[12].X) * 0.5f;
		const float Width = FMath::Abs((*L)[11].X - (*L)[12].X), ElbowY = FMath::Max((*L)[13].Y, (*L)[14].Y);
		const bool bHigh = ShoulderY < 0.52f, bElbows = ElbowY < 0.95f, bCentred = FMath::Abs(MidX - 0.5f) < 0.2f, bNear = Width > 0.07f;
		CalChecks.Add({ TEXT("shoulders in the upper half of the picture"), bHigh });
		CalChecks.Add({ TEXT("both elbows inside the picture"), bElbows });
		CalChecks.Add({ TEXT("standing in the middle"), bCentred });
		CalChecks.Add({ TEXT("close enough to be seen clearly"), bNear });
		bFramed = bHigh && bElbows && bCentred && bNear;
		if (!bHigh || !bElbows) Framing = TEXT("STEP BACK  (or tilt the screen back)");
		else if (!bNear) Framing = TEXT("COME CLOSER");
		// A small x in the picture is the player's own right-hand side.
		else if (!bCentred) Framing = MidX < 0.5f ? TEXT("MOVE TO YOUR LEFT") : TEXT("MOVE TO YOUR RIGHT");
	}

	/* ---- the player's fighter copies what is recognised ---- */
	int32 Lefts = 0, Rights = 0;
	for (const TPair<EKSArm, EKSPunch>& Punch : FramePunches)
	{
		(Punch.Key == EKSArm::Left ? Lefts : Rights)++;
		const FPunchClip& Clip = PunchClips[(int32)Punch.Value][(int32)Punch.Key];
		if (PlayerDummy) PlayerDummy->PlayClip(Clip.Name, false, 1.25f, FMath::Max(0.f, Clip.Impact - 0.11f));
		CalPunchUntil = Clock + PlayerPunchSeconds; CalClipKey = TEXT("punch");
		Sfx(EKSSfx::Whoosh, 0.7f);
	}
	const bool bBlock = T && T->IsHeld(EKSDefense::Block);
	if (Clock >= CalPunchUntil && PlayerDummy)
	{
		const FString Key = bBlock ? TEXT("block") : TEXT("idle");
		if (Key != CalClipKey)
		{
			CalClipKey = Key;
			if (bBlock) PlayerDummy->PlayClip(TEXT("Block1"), true, 1.f, 0.6f); else PlayerDummy->PlayClip(TEXT("Combat_Stance"), true);
		}
	}
	TArray<float> Peaks = T ? T->TakeStrokePeaks() : TArray<float>();
	if (CalStep == 2 || CalStep == 3) for (float Peak : Peaks) if (Peak > 2.5f) CalPeaks.Add(Peak);

	/* ---- the steps ---- */
	// Every move is done three times, and every step starts with a moment to read it before anything counts.
	const float ReadSeconds = 2.2f, GoodSeconds = 0.9f;
	const bool bReading = CalStep >= 2 && CalStep <= 7 && CalStepTime < ReadSeconds;
	bool bDone = false;
	auto Hold = [&](bool bHeld, float Seconds) { CalHold = bHeld ? CalHold + Dt : FMath::Max(0.f, CalHold - Dt * 2.f); CalProgress = FMath::Clamp(CalHold / Seconds, 0.f, 1.f); return CalHold >= Seconds; };
	// One repetition of a held move: hold it for Seconds, then let go before the next one counts.
	auto Rep = [&](bool bHeld, float Seconds)
	{
		if (bReading) return false;
		if (!bHeld) { CalHold = 0.f; bCalRepCounted = false; }
		else if (!bCalRepCounted) { CalHold += Dt; if (CalHold >= Seconds) { bCalRepCounted = true; CalCount++; Sfx(EKSSfx::Blocked, 0.5f); } }
		CalProgress = FMath::Min(1.f, (CalCount + (bCalRepCounted ? 0.f : FMath::Clamp(CalHold / Seconds, 0.f, 1.f))) / 3.f);
		return CalCount >= 3;
	};
	CalCountNeed = 0;
	FString Tip;
	switch (CalStep)
	{
	case 0:
		CalTitle = TEXT("STAND WHERE THE CAMERA SEES YOUR ARMS");
		Tip = TEXT("good, hold it for a moment...");
		bDone = Hold(bFramed, 2.0f);
		break;
	case 1:
		CalTitle = TEXT("FISTS UP  -  HOLD STILL");
		Tip = TEXT("this is your guard: the game measures everything from here");
		CalProgress = T ? FMath::Max(0.f, T->GetCalibrationProgress()) : 0.f;
		bDone = T && T->IsTracking() && CalStepTime > 2.5f;
		if (bDone) CalProgress = 1.f;
		break;
	case 2: case 3:
		CalTitle = CalStep == 2 ? TEXT("LEFT JAB  x 3") : TEXT("RIGHT JAB  x 3");
		Tip = TEXT("punch straight at the camera, bring the fist back, then the next one");
		CalCountNeed = 3;
		if (!bReading) CalCount += CalStep == 2 ? Lefts : Rights;
		CalProgress = FMath::Min(1.f, CalCount / 3.f);
		bDone = CalCount >= 3;
		break;
	case 4:
		CalTitle = TEXT("BLOCK  x 3");
		Tip = TEXT("both fists up to your face, hold a moment, then lower them");
		CalCountNeed = 3;
		bDone = Rep(bBlock, 0.6f);
		break;
	case 5:
		CalTitle = TEXT("DODGE LEFT  x 3");
		Tip = TEXT("lean your head and shoulders to your left, then come back to the middle");
		CalCountNeed = 3;
		bDone = Rep(T && T->IsHeld(EKSDefense::EvadeLeft), 0.2f);
		break;
	case 6:
		CalTitle = TEXT("DODGE RIGHT  x 3");
		Tip = TEXT("lean your head and shoulders to your right, then come back to the middle");
		CalCountNeed = 3;
		bDone = Rep(T && T->IsHeld(EKSDefense::EvadeRight), 0.2f);
		break;
	case 7:
		CalTitle = TEXT("DUCK  x 3");
		Tip = TEXT("bend your knees so your head drops, then stand up again");
		CalCountNeed = 3;
		bDone = Rep(T && T->IsHeld(EKSDefense::Duck), 0.2f);
		break;
	default:
		CalTitle = TEXT("READY!");
		Tip = TEXT("first fighter coming up");
		CalProgress = FMath::Min(1.f, CalStepTime / 2.0f);
		if (CalStepTime > 2.0f) { FinishCalibration(true); return; }
		break;
	}
	CalCountNow = FMath::Min(CalCount, CalCountNeed);
	// Bad framing is always the first thing to fix; then a moment to read, the tip, and the way out if it will not register.
	if (!Framing.IsEmpty()) CalHint = Framing;
	else if (bReading) CalHint = TEXT("get ready...   ") + Tip;
	else if (CalStep >= 2 && CalStep <= 7 && CalStepTime > ReadSeconds + 12.f) CalHint = TEXT("not registering?  press SPACE to skip this move");
	else CalHint = Tip;

	// A finished step says GOOD for a moment before the next one appears.
	if (bDone && CalDoneAt < 0.f && CalStep < CalSteps - 1) { CalDoneAt = CalStepTime; Sfx(EKSSfx::Bell1, 0.5f); }
	if (CalDoneAt >= 0.f) { CalTitle = TEXT("GOOD!"); CalHint.Reset(); CalProgress = 1.f; }
	if (CalStep < CalSteps - 1 && (bSkip || (CalDoneAt >= 0.f && CalStepTime - CalDoneAt > GoodSeconds))) SetCalStep(CalStep + 1);
}

/* ================= the crowd ================= */

void AKSGameMode::SeatCrowd()
{
	if (Spectators.Num()) return;                      // they keep their places for the whole ladder
	for (AKSDummy* Dummy : Arena.Crowd) if (Dummy) Dummy->SetActorHiddenInGame(true);   // the faceless stand-ins go home
	static const TCHAR* Names[] = { TEXT("kid"), TEXT("robot"), TEXT("mom"), TEXT("luchador"), TEXT("punk"), TEXT("alien"), TEXT("nerd"), TEXT("rocker"),
		TEXT("skater"), TEXT("boss"), TEXT("bboy"), TEXT("oldman"), TEXT("ninja"), TEXT("astro"), TEXT("vampire"), TEXT("chef"), TEXT("cheer"), TEXT("cowboy"), TEXT("prof"), TEXT("wizard") };
	// Places on three sides of the ring: three rows behind the far ropes, two rows down each side. Each row stands a
	// little higher than the one in front, like stands.
	TArray<FVector> Places;
	for (int32 Row = 0; Row < 3; Row++)
	{
		const float Half = 430.f + 70.f * Row, Step = 118.f;
		for (float Y = -Half + (Row % 2) * Step * 0.5f; Y <= Half; Y += Step) Places.Add(FVector(465.f + 118.f * Row, Y, 112.f + 40.f * Row));
	}
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		for (int32 Row = 0; Row < 2; Row++)
		{
			for (float X = -190.f + (Row % 2) * 59.f; X <= 360.f; X += 118.f) Places.Add(FVector(X, Side * (470.f + 118.f * Row), 112.f + 40.f * Row));
		}
	}
	// Deal the characters out so that neighbours differ, and skip any whose picture is missing.
	TArray<int32> Deck;
	FKSBuilder B = MakeBuilder();
	const float Now = GetWorld()->GetTimeSeconds();
	for (const FVector& Home : Places)
	{
		if (Deck.Num() == 0)
		{
			for (int32 i = 0; i < UE_ARRAY_COUNT(Names); i++) Deck.Add(i);
			for (int32 i = Deck.Num() - 1; i > 0; i--) Deck.Swap(i, FMath::RandRange(0, i));
		}
		const TCHAR* Name = Names[Deck.Pop()];
		const FString Tex = FString::Printf(TEXT("/Game/KS/Arena/T_KS_Spec_%s.T_KS_Spec_%s"), Name, Name);
		// Each one faces the middle of the ring.
		const FVector ToRing = (FVector(75.f, 0.f, 0.f) - FVector(Home.X, Home.Y, 0.f)).GetSafeNormal();
		const float Size = FMath::FRandRange(0.88f, 1.08f);
		UMaterialInstanceDynamic* MID = B.Image(*Tex, false, Home + FVector(0, 0, 112.f * (Size - 1.f)), FVector(-ToRing.Y, ToRing.X, 0.f), FVector(0, 0, -1), 112.f * Size, 224.f * Size,
			FVector4(0.25f, 1.f, 0.f, 0.f), FMath::FRandRange(0.75f, 0.95f), TEXT("/Game/KS/Arena/M_KS_Sprite.M_KS_Sprite"));
		if (!MID) continue;
		FSpectator S; S.MID = MID; S.Actor = Cast<AActor>(MID->GetOuter()); S.Home = Home + FVector(0, 0, 112.f * (Size - 1.f));
		S.Phase = FMath::FRandRange(0.f, 20.f); S.bFlip = FMath::RandBool(); S.bRival = FMath::FRand() < 0.18f;
		S.IdleRate = FMath::FRandRange(0.7f, 2.6f); S.BobRate = FMath::FRandRange(1.5f, 4.5f); S.BobHeight = FMath::FRandRange(0.f, 6.f);
		S.JumpRate = FMath::FRandRange(6.f, 12.f); S.JumpHeight = FMath::FRandRange(10.f, 30.f);
		S.NextWhim = Now + FMath::FRandRange(1.f, 9.f);
		Spectators.Add(S);
	}
	// The flash pool.
	for (int32 i = 0; i < 10; i++)
	{
		UMaterialInstanceDynamic* MID = B.Shape(B.Sphere, FVector(0, 0, -500), FVector(20.f), FLinearColor::White, 90.f);
		FCrowdFlash F; F.MID = MID; F.Actor = MID ? Cast<AActor>(MID->GetOuter()) : nullptr;
		if (AActor* Actor = F.Actor.Get()) Actor->SetActorHiddenInGame(true);
		Flashes.Add(F);
	}
	for (int32 i = 0; i < 3; i++) FlashLights.Add(B.Light(FVector(0, 0, -500), FLinearColor(0.9f, 0.95f, 1.f), 0.f, 900.f));
}

void AKSGameMode::CrowdReact(bool bCheer, float Strength)
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (FSpectator& S : Spectators)
	{
		if (FMath::FRand() > Strength) continue;              // a light hit moves only part of the crowd
		S.CueAt = Now + FMath::FRandRange(0.03f, 0.55f);      // nobody reacts in the same instant
		S.CuePose = (bCheer != S.bRival) ? 1 : 2;             // rivals wince when the player scores and cheer when he is hit
	}
	CrowdFlashes(2.f + 10.f * Strength);
}

void AKSGameMode::TickCrowd()
{
	if (!GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	for (FSpectator& S : Spectators)
	{
		AActor* Actor = S.Actor.Get();
		if (!Actor || !S.MID) continue;
		if (S.CueAt > 0.f && Now >= S.CueAt) { S.Pose = S.CuePose; S.CueAt = 0.f; S.BusyUntil = Now + FMath::FRandRange(0.6f, 2.0f); }
		else if (S.BusyUntil > 0.f && Now >= S.BusyUntil) { S.Pose = 0; S.BusyUntil = 0.f; S.NextWhim = Now + FMath::FRandRange(1.5f, 8.f); }
		else if (S.BusyUntil <= 0.f && S.CueAt <= 0.f && Now >= S.NextWhim)
		{
			// Nobody stands still for a whole fight: now and then each one does something of his own.
			const float Roll = FMath::FRand();
			if (Roll < 0.45f) { S.Pose = 1; S.BusyUntil = Now + FMath::FRandRange(0.4f, 1.3f); }          // a cheer out of nowhere
			else if (Roll < 0.60f) { S.Pose = 2; S.BusyUntil = Now + FMath::FRandRange(0.3f, 0.8f); }     // a gasp
			else { S.bFlip = !S.bFlip; S.NextWhim = Now + FMath::FRandRange(1.f, 6.f); }                  // turns to look the other way
		}
		// Old-arcade animation: whole poses swapped a few times a second, never in-between frames.
		int32 Frame = S.Pose;
		float Lift = 0.f, Side = 0.f;
		if (S.Pose == 0) { Frame = FMath::FloorToInt(Now * S.IdleRate + S.Phase) % 2 ? 3 : 0; Lift = FMath::FloorToInt(Now * S.BobRate + S.Phase) % 2 ? S.BobHeight : 0.f; }
		else if (S.Pose == 1) Lift = FMath::FloorToInt(Now * S.JumpRate + S.Phase) % 2 ? S.JumpHeight : 4.f;      // jumping up and down
		else Side = FMath::FloorToInt(Now * 14.f + S.Phase) % 2 ? 3.f : -3.f;                                    // shaking
		if (Frame != S.Shown || S.bFlip != S.bShownFlip)
		{
			S.Shown = Frame; S.bShownFlip = S.bFlip;
			// Flipped: the picture is read right to left, so the cell starts at its right-hand edge.
			S.MID->SetVectorParameterValue(TEXT("UV"), FLinearColor(S.bFlip ? -0.25f : 0.25f, 1.f, 0.f, 0.f));
			S.MID->SetVectorParameterValue(TEXT("Offset"), FLinearColor(0.25f * (Frame + (S.bFlip ? 1 : 0)), 0.f, 0.f, 0.f));
		}
		Actor->SetActorLocation(S.Home + FVector(0.f, Side, Lift));
	}

	/* ---- photo flashes ---- */
	for (FCrowdFlash& F : Flashes)
	{
		if (F.Until > 0.f && Now >= F.Until) { F.Until = 0.f; if (AActor* Actor = F.Actor.Get()) Actor->SetActorHiddenInGame(true); }
	}
	bool bAnyLit = false;
	for (const FCrowdFlash& F : Flashes) bAnyLit |= F.Until > 0.f;
	if (!bAnyLit) for (const TWeakObjectPtr<APointLight>& L : FlashLights) if (APointLight* Light = L.Get()) Light->PointLightComponent->SetIntensity(0.f);
	// A steady trickle during a fight, and a burst whenever the crowd reacts.
	if (Phase == EKSPhase::Fight) FlashesOwed += GetWorld()->GetDeltaSeconds() * 0.9f;
	if (FlashesOwed >= 1.f && Now >= NextFlashAt && Spectators.Num())
	{
		for (int32 i = 0; i < Flashes.Num(); i++)
		{
			FCrowdFlash& F = Flashes[i];
			AActor* Actor = F.Actor.Get();
			if (F.Until > 0.f || !Actor) continue;
			const FSpectator& S = Spectators[FMath::RandRange(0, Spectators.Num() - 1)];
			const FVector ToRing = (FVector(75.f, 0.f, 0.f) - FVector(S.Home.X, S.Home.Y, 0.f)).GetSafeNormal();
			const FVector At = S.Home + ToRing * 20.f + FVector(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(40.f, 85.f));
			Actor->SetActorLocation(At);
			Actor->SetActorScale3D(FVector(FMath::FRandRange(0.10f, 0.22f)));
			Actor->SetActorHiddenInGame(false);
			F.Until = Now + FMath::FRandRange(0.05f, 0.09f);
			if (APointLight* Light = FlashLights.Num() ? FlashLights[i % FlashLights.Num()].Get() : nullptr)
			{
				Light->SetActorLocation(At + ToRing * 40.f);
				Light->PointLightComponent->SetIntensity(650.f);
			}
			FlashesOwed -= 1.f;
			NextFlashAt = Now + FMath::FRandRange(0.03f, 0.16f);
			break;
		}
	}
	FlashesOwed = FMath::Min(FlashesOwed, 30.f);
}

void AKSGameMode::DriveCharacters()
{
	const bool bLost = (Phase == EKSPhase::FightEnd || Phase == EKSPhase::Continue) && !bLastResultWin;
	const bool bWon = (Phase == EKSPhase::FightEnd || Phase == EKSPhase::Result) && bLastResultWin;

	/* ---- the opponent ---- */
	if (OppDummy->HasCharacter())
	{
		const FPunchClip& Punch = PunchClips[(int32)Attack.Type][(int32)Attack.Side];
		FString Key;
		if (OppState == EKSOppState::KO) Key = TEXT("ko");
		else if (bLost) Key = TEXT("win");
		else if (Clock < OppHurtUntil) Key = TEXT("hurt");
		else if (OppState == EKSOppState::Windup) Key = FString::Printf(TEXT("wind %.3f"), OppT);
		else if (OppState == EKSOppState::Strike) Key = TEXT("strike");
		else if (OppState == EKSOppState::Open) Key = TEXT("open");
		else Key = Clock < OppGuardUntil ? TEXT("block") : TEXT("idle");

		// A punch that connected is allowed to pull back before he returns to his stance.
		if (OppClipKey == TEXT("strike") && Key == TEXT("idle"))
		{
			if (OppFollowThroughUntil == 0.f) { OppFollowThroughUntil = Clock + 0.30f; OppDummy->SetClipRate(1.4f); }
			if (Clock < OppFollowThroughUntil) Key = TEXT("strike");
		}

		// Aim the punch at the player's face, or at the gloves when the player is blocking.
		if (Phase == EKSPhase::Fight && (OppState == EKSOppState::Windup || OppState == EKSOppState::Strike))
		{
			const float U = FMath::Clamp((Clock - OppT) / FMath::Max(0.01f, Attack.Duration), 0.f, 1.f);
			const float Weight = OppState == EKSOppState::Strike ? 1.f : FMath::SmoothStep(0.40f, 0.90f, U);
			const FVector Away = (OppDummy->GetActorLocation() - PlayerDummy->GetActorLocation()).GetSafeNormal2D();
			const bool bGuarded = Phase == EKSPhase::Fight && Defense() == EKSDefense::Block && Attack.Type != EKSPunch::Uppercut;
			OppDummy->AimPunch(Punch.Name, PlayerDummy->FaceWorld(true) + Away * (bGuarded ? 26.f : 9.f) - FVector(0, 0, 6.f), Weight);
		}
		else OppDummy->StopAim();

		if (Key != OppClipKey)
		{
			const bool bFromWindup = OppClipKey.StartsWith(TEXT("wind"));
			OppClipKey = Key;
			if (Key == TEXT("ko")) OppDummy->PlayClip(TEXT("Knock_Down"), false);
			else if (Key == TEXT("win")) OppDummy->PlayClip(TEXT("victory"), true);
			else if (Key == TEXT("hurt")) OppDummy->PlayClip(TEXT("Face_Punch_Reaction"), false, 1.8f, 0.15f);
			else if (Key.StartsWith(TEXT("wind")))
			{
				// Stretch the clip so the fist is fully out exactly when the game resolves the punch (after the grace window).
				OppDummy->PlayClip(Punch.Name, false, Punch.Impact / FMath::Max(0.05f, Attack.Duration + Grace));
			}
			else if (Key == TEXT("strike"))
			{
				if (!bFromWindup) OppDummy->PlayClip(Punch.Name, false, 1.f, FMath::Max(0.f, Punch.Impact - Grace));
				OppFollowThroughUntil = 0.f;
			}
			else if (Key == TEXT("open")) OppDummy->PlayClip(TEXT("Face_Punch_Reaction_1"), true, 1.f, 0.3f);
			else if (Key == TEXT("block")) OppDummy->PlayClip(TEXT("Block1"), true, 1.f, 0.6f);
			else OppDummy->PlayClip(TEXT("Combat_Stance"), true);
		}

		OppDummy->SetAttacking(Phase == EKSPhase::Fight && (OppState == EKSOppState::Strike || (OppState == EKSOppState::Windup && Clock - OppT > Attack.Duration * 0.45f)));
		if (OppState == EKSOppState::KO) OppDummy->SetFace({ TEXT("ko"), TEXT("hurt") });
		else if (bLost) OppDummy->SetFace({ TEXT("smug"), TEXT("happy"), TEXT("love") });
		else if (OppState == EKSOppState::Open) OppDummy->SetFace({ TEXT("dizzy"), TEXT("hit"), TEXT("hurt") });
		else if (Clock < OppHurtUntil + 0.5f) OppDummy->SetFace({ TEXT("hit"), TEXT("hurt") });
		else if (OppHP < OppMaxHP * 0.25f) OppDummy->SetFace({ TEXT("hurt"), TEXT("hit") });
		else OppDummy->SetFace({});
	}

	/* ---- the player's fighter ---- */
	if (PlayerDummy->HasCharacter())
	{
		const EKSDefense D = Phase == EKSPhase::Fight ? Defense() : EKSDefense::None;
		int32 PunchArm = -1;
		for (int32 i = 0; i < 2; i++)
		{
			if (Clock - PunchStart[i] < PlayerPunchSeconds && (PunchArm < 0 || PunchStart[i] > PunchStart[PunchArm])) PunchArm = i;
		}
		// With the webcam, the fighter's arms copy the player's own arms, so punch and block clips are not needed:
		// the body keeps its stance and the arms do whatever the player does, as it happens.
		const bool bKnockedOut = bLost && !bLastResultDecision;
		// Off by default: one webcam cannot measure depth well, so copied arms bend oddly and never reach the opponent.
		// Instead the webcam recognises the move and the fighter plays the authored, aimed punch, which does connect.
		const bool bMirror = bMirrorArms && TrackingSubsystem && TrackingSubsystem->HasLandmarks() && !bKnockedOut && !bWon;
		PlayerDummy->SetTrackedArms(bMirror ? &TrackingSubsystem->GetWorldLandmarks() : nullptr);
		if (bFakeArms && !bKnockedOut && !bWon)
		{
			// Test pose in camera space (x = image right = the person's left, y = down, z = away from the camera):
			// left arm straight out to the person's left, right arm bent with the forearm pointing at the camera.
			TArray<FVector> Fake; Fake.SetNumZeroed(33);
			Fake[11] = FVector(0.18, -0.45, 0);  Fake[13] = FVector(0.46, -0.45, 0);     Fake[15] = FVector(0.72, -0.45, 0);
			Fake[12] = FVector(-0.18, -0.45, 0); Fake[14] = FVector(-0.24, -0.20, -0.05); Fake[16] = FVector(-0.22, -0.22, -0.32);
			PlayerDummy->SetTrackedArms(&Fake);
		}
		FString Key;
		if (bKnockedOut) Key = TEXT("ko");
		else if (bWon) Key = TEXT("win");
		else if (Clock < PlayerHurtUntil && !bMirror) Key = TEXT("hurt");
		else if (PunchArm >= 0 && !bMirror) Key = FString::Printf(TEXT("punch %d %.3f"), PunchArm, PunchStart[PunchArm]);
		else if (D == EKSDefense::Block && !bMirror) Key = TEXT("block");
		else Key = TEXT("idle");

		if (Key != PlayerClipKey)
		{
			PlayerClipKey = Key;
			if (Key == TEXT("ko")) PlayerDummy->PlayClip(TEXT("Knock_Down"), false);
			else if (Key == TEXT("win")) PlayerDummy->PlayClip(TEXT("victory"), true);
			else if (Key == TEXT("hurt")) PlayerDummy->PlayClip(TEXT("Face_Punch_Reaction"), false, 1.8f, 0.15f);
			else if (PunchArm >= 0 && Key.StartsWith(TEXT("punch")))
			{
				// The hit lands 0.09 s after the input, so the clip starts just before its impact frame.
				const FPunchClip& Punch = PunchClips[(int32)PunchType[PunchArm]][PunchArm];
				PlayerDummy->PlayClip(Punch.Name, false, 1.25f, FMath::Max(0.f, Punch.Impact - 0.11f));
			}
			else if (Key == TEXT("block")) PlayerDummy->PlayClip(TEXT("Block1"), true, 1.f, 0.6f);
			else PlayerDummy->PlayClip(TEXT("Combat_Stance"), true);
		}
		// Aim the punch at the opponent's face when he is open, or at his guard when he is not.
		if (PunchArm >= 0 && Clock - PunchStart[PunchArm] < 0.24f && !bLost && OppState != EKSOppState::KO)
		{
			const FVector Away = (PlayerDummy->GetActorLocation() - OppDummy->GetActorLocation()).GetSafeNormal2D();
			const bool bOpen = OppState == EKSOppState::Open || OppState == EKSOppState::KO || Clock < OppHurtUntil;
			PlayerDummy->AimPunch(PunchClips[(int32)PunchType[PunchArm]][PunchArm].Name, OppDummy->FaceWorld(true) + Away * (bOpen ? 9.f : 28.f) - FVector(0, 0, 6.f), 1.f);
		}
		else PlayerDummy->StopAim();

		// Evading slides the fighter sideways (the player's own left is -Y) and ducking squashes them down.
		const float Side = D == EKSDefense::EvadeLeft ? -42.f : D == EKSDefense::EvadeRight ? 42.f : 0.f;
		PlayerDummy->SetBodyShift(Side, D == EKSDefense::Duck ? 0.68f : 1.f);

		PlayerDummy->SetAttacking(PunchArm >= 0 && Phase == EKSPhase::Fight);
		if (bLost && !bLastResultDecision) PlayerDummy->SetFace({ TEXT("ko"), TEXT("hurt") });
		else if (bWon) PlayerDummy->SetFace({ TEXT("smug"), TEXT("happy"), TEXT("love") });
		else if (Clock < PlayerHurtUntil + 0.5f) PlayerDummy->SetFace({ TEXT("hit"), TEXT("hurt") });
		else if (PlayerHP < 25.f) PlayerDummy->SetFace({ TEXT("hurt"), TEXT("hit") });
		else PlayerDummy->SetFace({});
	}

	// A line that was waiting for its moment (the winner speaks after the loser).
	if (LaterVoice.Who.IsValid() && Clock >= LaterVoice.When) { Speak(LaterVoice.Who.Get(), LaterVoice.Prefix, true); LaterVoice = FLaterVoice(); }
	// The subtitle: whoever is speaking.
	AKSDummy* Talker = OppDummy && OppDummy->IsTalking() ? OppDummy.Get() : PlayerDummy && PlayerDummy->IsTalking() ? PlayerDummy.Get() : nullptr;
	SpokenLine = Talker ? Talker->SpokenText() : FString();
	if (Talker) SpokenBy = Talker == OppDummy ? FString(Fighter().Name) : FString(PlayerFighter().Name);
}

/** One voice at a time. A taunt is dropped if somebody is still speaking or has only just stopped; bMust (the
 *  introduction, the last word after a knockout) cuts in. Returns the length of the line in seconds. */
float AKSGameMode::Speak(AKSDummy* Who, const TCHAR* Prefix, bool bMust)
{
	if (!Who || !Who->HasCharacter()) return 0.f;
	AKSDummy* Other = Who == OppDummy ? PlayerDummy.Get() : OppDummy.Get();
	if (!bMust && (Clock < VoiceQuietUntil || Who->IsTalking() || (Other && Other->IsTalking()))) return 0.f;
	if (Other) Other->StopTalking();
	const float Seconds = Who->Say(Prefix, Who == OppDummy ? 1.f : 0.85f);
	if (Seconds > 0.f) VoiceQuietUntil = Clock + Seconds + 2.2f;
	return Seconds;
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

void AKSGameMode::QueueHitShots(const TCHAR* Kind)
{
	const float Offsets[] = { 0.0f, 0.07f, 0.16f, 0.28f };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Offsets); i++)
	{
		BurstShots.Add({ Clock + Offsets[i], FString::Printf(TEXT("hit_%02d_%s_%d.png"), HitShotIndex, Kind, i) });
	}
	HitShotIndex++;
}

void AKSGameMode::PlayerHurt(float Damage, bool bLight)
{
	PlayerHP = FMath::Max(0.f, PlayerHP - Damage);
	Stats.Taken++;
	Shake = bLight ? 4.f : 10.f; RedFlash = bLight ? 0.4f : 1.f; Combo = 0;
	PlayerHurtUntil = Clock + 0.3f;
	// The camera takes the punch too: knocked back, tipped to one side.
	CamPunch(FVector(bLight ? -70.f : -170.f, FMath::RandBool() ? 60.f : -60.f, bLight ? 10.f : 30.f), (FMath::RandBool() ? 1.f : -1.f) * (bLight ? 22.f : 60.f), bLight ? 2.f : 5.f);
	Sfx(EKSSfx::Hurt);
	// He gloats about it, or the player's own fighter swears.
	if (PlayerHP > 0.f) { if (FMath::FRand() < 0.6f) Speak(OppDummy, TEXT("hit")); else if (FMath::FRand() < 0.5f) Speak(PlayerDummy, TEXT("hurt")); }
	CrowdReact(false, bLight ? 0.4f : 0.8f);
	if (PlayerDummy) { PlayerDummy->Flash(KSColors::Red, 0.12f); PlayerDummy->TakeHit(Attack.Type, Attack.Side, bLight ? 0.5f : 1.f); PlayerDummy->HitStop(0.08f); }
	if (OppDummy) OppDummy->HitStop(0.08f);
	if (bHitShots) QueueHitShots(TEXT("taken"));
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
		if (OppDummy) OppDummy->Knock(5.f);   // a blocked punch still nudges him
		Sfx(EKSSfx::Blocked);
		Popup(TEXT("BLOCKED"), KSColors::Muted, 0.56f, 0.34f);
		return;
	}
	OppHP = FMath::Max(0.f, OppHP - PunchDamage[(int32)Hit.Type] * Hit.Mult);
	Stats.Landed++; Combo++; Stats.BestCombo = FMath::Max(Stats.BestCombo, Combo);
	OppHurtUntil = Clock + 0.24f;
	// The camera drives in behind the punch, from the side of the hand that threw it, with a quick zoom.
	CamPunch(FVector(Hit.bCounter ? 150.f : 85.f, Hit.Arm == EKSArm::Left ? 45.f : -45.f, 0.f), (Hit.Arm == EKSArm::Left ? -1.f : 1.f) * (Hit.bCounter ? 26.f : 10.f), Hit.bCounter ? -7.f : -3.5f);
	if (OppDummy) { OppDummy->Flash(Hit.bCounter ? KSColors::Yellow : FLinearColor::White, 0.08f); OppDummy->TakeHit(Hit.Type, Hit.Arm, Hit.bCounter ? 1.5f : 0.9f); OppDummy->HitStop(0.07f); }
	if (PlayerDummy) PlayerDummy->HitStop(0.07f);
	if (bHitShots) QueueHitShots(TEXT("landed"));
	Shake = FMath::Max(Shake, Hit.bCounter ? 7.f : 4.f);
	CrowdReact(true, Hit.bCounter ? 1.f : FMath::Min(1.f, 0.35f + 0.2f * Combo));
	if (Hit.bCounter) { Sfx(EKSSfx::Counter); Popup(TEXT("COUNTER!"), KSColors::Yellow, 0.56f, 0.3f, true); }
	else { Sfx(EKSSfx::Hit); if (Combo > 1) Popup(FString::Printf(TEXT("%d HITS"), Combo), KSColors::Cyan, 0.66f, 0.38f); }
	if (OppHP > 0.f) { if (FMath::FRand() < (Hit.bCounter ? 0.7f : 0.4f)) Speak(OppDummy, TEXT("hurt")); else if (FMath::FRand() < 0.4f) Speak(PlayerDummy, TEXT("hit")); }
	if (OppHP <= 0.f) EndFight(true, false);
}

void AKSGameMode::EndFight(bool bWin, bool bDecision)
{
	bLastResultWin = bWin; bLastResultDecision = bDecision;
	// One line per bout for tools/check.sh. "travel" is how far the drawn hand moved: near zero means a frozen body.
	UE_LOG(LogTemp, Display, TEXT("KSTEST bout=%d opponent=%s result=%s opp_hand_travel=%.0f player_hand_travel=%.0f landed=%d taken=%d spectators=%d"), FighterIndex, Fighter().Name,
		bDecision ? TEXT("time") : bWin ? TEXT("win") : TEXT("loss"), TestOppHand.IsValid ? TestOppHand.GetSize().Size() : 0.0, TestPlayerHand.IsValid ? TestPlayerHand.GetSize().Size() : 0.0, Stats.Landed, Stats.Taken, Spectators.Num());
	TestOppHand = FBox(ForceInit); TestPlayerHand = FBox(ForceInit);
	// Faces for tools/check.sh: a fighter who showed one face all bout, or never opened the mouth, is the bug.
	int32 Faces = 0, Mouth = 0;
	if (OppDummy) OppDummy->TakeFaceStats(Faces, Mouth);
	UE_LOG(LogTemp, Display, TEXT("KSTEST faces bout=%d opponent=%s shown=%d mouth_opens=%d"), FighterIndex, Fighter().Name, Faces, Mouth);
	if (PlayerDummy) PlayerDummy->TakeFaceStats(Faces, Mouth);
	// The loser has the first word, the winner the last.
	AKSDummy* Winner = bWin ? PlayerDummy.Get() : OppDummy.Get();
	AKSDummy* Loser = bWin ? OppDummy.Get() : PlayerDummy.Get();
	const float LoserSeconds = Speak(Loser, TEXT("lose"), true);
	LaterVoice.Who = Winner; LaterVoice.Prefix = TEXT("win"); LaterVoice.When = Clock + LoserSeconds + 0.35f;
	CueMain.Reset(); CueHint.Reset(); Pending.Reset();
	CrowdReact(bWin, 1.f);
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
			if (!bAutoNoGuard && (OppState == EKSOppState::Windup || OppState == EKSOppState::Strike))
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
		if (bLastResultWin) { Sfx(EKSSfx::Bell3); Sfx(EKSSfx::Win, 0.8f); SetPhase(EKSPhase::Result); }
		else { ContinueLeft = 9.99f; Sfx(EKSSfx::Lose, 0.8f); SetPhase(EKSPhase::Continue); }
	}
}

void AKSGameMode::TickResult(float Dt)
{
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	if ((PhaseTime > 1.2f && ConfirmPressed()) || (bAuto && PhaseTime > 2.5f))
	{
		if (FighterIndex + 1 < NumFights()) StartCard(FighterIndex + 1);
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
		: PhaseTime < 8.f ? TEXT("The fighters nobody was meant to see finally got their match.")
		: PhaseTime < 12.f ? TEXT("The cabinet has what it waited thirty-nine years for: a player.")
		: TEXT("KO-SEKAI   -   thank you for playing");
	Prompt = PhaseTime > 6.f ? TEXT("Press ENTER to walk the street again") : TEXT("");
	if ((PhaseTime > 6.f && ConfirmPressed()) || (bAuto && PhaseTime > 16.f)) EnterStreet(false);
}
