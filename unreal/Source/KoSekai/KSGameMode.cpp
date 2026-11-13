#include "KSGameMode.h"
#include "KSCineData.h"
#include "KSAnnouncerData.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "KSDummy.h"
#include "KSPawn.h"
#include "KSHUD.h"
#include "KSTracking.h"
#include "KSNet.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Components/SpotLightComponent.h"
#include "Engine/TextRenderActor.h"
#include "Engine/GameInstance.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "HAL/IConsoleManager.h"
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

	// The special abilities, in the order of the roster: what the player shouts, what it looks like, and how far into
	// the fighter's Special clip it lands.
	enum class EFxKind : uint8 { Slam, Static, Laser, FireRing, Bolt, Blast, Cloud };
	struct FSpecialDef { const TCHAR* Cry; EFxKind Kind; FLinearColor Color; float ImpactFrac; };
	const FSpecialDef Specials[] = {
		{ TEXT("MULLET POWER!"),  EFxKind::Slam,     FLinearColor(1.0f, 0.45f, 0.05f), 0.62f },
		{ TEXT("NO SIGNAL!"),     EFxKind::Static,   FLinearColor(0.2f, 1.0f, 0.35f),  0.60f },
		{ TEXT("RAINBOW LASER!"), EFxKind::Laser,    FLinearColor(1.0f, 0.3f, 0.8f),   0.58f },
		{ TEXT("FEEL THE BURN!"), EFxKind::FireRing, FLinearColor(1.0f, 0.55f, 0.05f), 0.55f },
		{ TEXT("THUNDER!"),       EFxKind::Bolt,     FLinearColor(1.0f, 0.95f, 0.5f),  0.62f },
		{ TEXT("TURBO BLAST!"),   EFxKind::Blast,    FLinearColor(1.0f, 0.35f, 0.05f), 0.66f },
		{ TEXT("HAIRSPRAY!"),     EFxKind::Cloud,    FLinearColor(0.65f, 0.2f, 1.0f),  0.60f },
	};
	const float SpecialDamage = 24.f, OppSpecialDamage = 22.f;

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
	bGiveUp = FParse::Param(Cmd, TEXT("KSGiveUp"));
	bAutoNoGuard = bGiveUp || FParse::Param(Cmd, TEXT("KSNoGuard"));
	FParse::Value(Cmd, TEXT("KSEscAt="), TestEscAt);
	bFakeFilmPunches = FParse::Param(Cmd, TEXT("KSFakePunch"));
	bFakeArms = FParse::Param(Cmd, TEXT("KSFakeArms"));
	bCalDemo = FParse::Param(Cmd, TEXT("KSCalDemo"));
	bCalJitter = FParse::Param(Cmd, TEXT("KSCalJitter"));
	bMirrorArms = bFakeArms || FParse::Param(Cmd, TEXT("KSMirrorArms"));
	bHitShots = FParse::Param(Cmd, TEXT("KSHitShots"));
	FParse::Value(Cmd, TEXT("KSShots="), ShotEvery);
	FParse::Value(Cmd, TEXT("KSArena="), ForcedArena);
	bArenaTour = FParse::Param(Cmd, TEXT("KSArenaTour"));
	FString Start;
	FParse::Value(Cmd, TEXT("KSStart="), Start);
	bPortraits = FParse::Param(Cmd, TEXT("KSPortraits"));
	// FXAA instead of the engine's default (TSR). TSR blends each frame with the last and needs to know how everything
	// moved; the fighters' drawn bodies (poseable meshes) do not say, so a fighter moving fast dissolved into noise.
	if (IConsoleVariable* Method = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))) Method->Set(1, ECVF_SetByGameOverride);
	// The microphone, for shouting a special. Not in runs without sound (the self-check) or with -KSNoMic.
	// -KSMic opens it even without sound, for the self-check.
	if ((!FParse::Param(Cmd, TEXT("nosound")) || FParse::Param(Cmd, TEXT("KSMic"))) && !FParse::Param(Cmd, TEXT("KSNoMic")) && !bPortraits)
	{
		Mic = MakeUnique<FKSMic>();
		bMicOpen = Mic->Open();
		UE_LOG(LogTemp, Display, TEXT("KSTEST mic open=%d status=%s"), bMicOpen ? 1 : 0, *Mic->Status());
		if (!bMicOpen) Mic.Reset();
	}
	if (bPortraits || Start == TEXT("select")) EnterSelect();
	else if (Start == TEXT("fight"))
	{
		FParse::Value(Cmd, TEXT("KSFighter="), ForcedOpponent);   // roster index of the first opponent
		FParse::Value(Cmd, TEXT("KSPlayer="), ForcedPlayer);      // roster index of the player's fighter
		EnterArena();
		BeginLadder();
	}
	else
	{
		EnterStreet(Start == TEXT("ending"));
		// The opening film comes first, then the start menu; -KSStart=menu skips the film, -KSStart=street goes
		// straight to the pavement (the old way in, no longer on the menu).
		if (Start.IsEmpty()) SetPhase(EKSPhase::Intro);
		else if (Start == TEXT("menu")) SetPhase(EKSPhase::Menu);
	}
	// Online shortcuts, also used by tools/check.sh: -KSHost waits for a visitor, -KSJoin=<address> dials one.
	FParse::Value(Cmd, TEXT("KSPort="), HostPort);
	FString JoinArg;
	if (FParse::Param(Cmd, TEXT("KSHost"))) { EnterLobby(); LobbyHost(); }
	else if (FParse::Value(Cmd, TEXT("KSJoin="), JoinArg)) { EnterLobby(); JoinAddress = JoinArg; LobbyJoin(); }
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
	Spectators.Reset(); Flashes.Reset(); FlashLights.Reset(); FlashesOwed = 0.f; Confetti.Reset(); bArenaFxLogged = false;
	Motes.Reset(); ArenaIndex = -1; ArenaShowUntil = 0.f; bLookDirty = true;
	Fx.Reset(); bSpecialOn = false;
	PlayerDummy = nullptr; OppDummy = nullptr;
}

void AKSGameMode::SetPhase(EKSPhase NewPhase)
{
	Phase = NewPhase;
	PhaseTime = 0.f;
	// The fight music: full in the bout, lower while a fighter is introduced or the result is up (voices and
	// stings play there), silent on the street and in calibration.
	const float Level = Phase == EKSPhase::Fight ? 0.32f
		: (Phase == EKSPhase::Card || Phase == EKSPhase::FightEnd) ? 0.2f
		: (Phase == EKSPhase::Result || Phase == EKSPhase::Continue) ? 0.12f : 0.f;
	MusicLevel = Level;
	PlayMusic();
	// The hall's murmur runs under every phase that happens in the ring.
	KSAudio::Crowd(this, CrowdPlayer, Phase == EKSPhase::Fight ? 0.2f : Level > 0.f ? 0.14f : 0.f);
	// The start menu plays the fight music too, without the crowd.
	if (Phase == EKSPhase::Menu || Phase == EKSPhase::Intro || Phase == EKSPhase::Select || Phase == EKSPhase::Online) { MusicLevel = Phase == EKSPhase::Intro ? 0.34f : 0.26f; PlayMusic(); }
	// Choosing a fighter happens in the hall, so the hall is heard.
	if (Phase == EKSPhase::Select) KSAudio::Crowd(this, CrowdPlayer, 0.12f);
}

void AKSGameMode::ToMenu()
{
	OnlineHangUp();   // whatever led here, a match against another player is over
	if (OppDummy) OppDummy->StopTalking();
	if (PlayerDummy) PlayerDummy->StopTalking();
	ClearWorld();
	Subtitle.Reset(); Prompt.Reset(); CueMain.Reset(); CueHint.Reset(); Announcement.Reset(); Popups.Reset();
	SpokenLine.Reset(); OppBody.Reset(); PlayerBody.Reset(); LaterVoices.Reset();
	BlackFade = 0.f; RedFlash = 0.f; bPaused = false; bCalFromMenu = false;
	WalkPos = FVector(-250, 0, 0);
	MenuIndex = 0; WhiteFade = FMath::Max(WhiteFade, 0.6f);
	UE_LOG(LogTemp, Display, TEXT("KSTEST to_menu from_phase=%d"), (int32)Phase);   // for tools/check.sh
	SetPhase(EKSPhase::Menu);
}

float AKSGameMode::Announcer(const FString& Key, float Volume)
{
	for (const FKSAnnouncerLine& Line : KSAnnouncerLines)
	{
		if (Key != Line.Key) continue;
		if (AnnouncerPlayer) AnnouncerPlayer->Stop();
		const FString Asset = FString::Printf(TEXT("V_Announcer_%s"), Line.Key);
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/KS/Voice/%s.%s"), *Asset, *Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
		// With no sound device (the self-check runs with -nosound) there is no player; everyone still waits for him by the clock.
		AnnouncerPlayer = Sound ? UGameplayStatics::SpawnSound2D(this, Sound, Volume) : nullptr;
		AnnouncerUntil = Clock + Line.Seconds;
		UE_LOG(LogTemp, Display, TEXT("KSTEST announcer line=%s sound=%d"), Line.Key, Sound ? 1 : 0);
		return Line.Seconds;
	}
	UE_LOG(LogTemp, Display, TEXT("KSTEST announcer line=%s sound=0 unknown=1"), *Key);
	return 0.f;
}

void AKSGameMode::HushAnnouncer()
{
	if (AnnouncerPlayer && Clock < AnnouncerUntil) AnnouncerPlayer->FadeOut(0.08f, 0.f);
	AnnouncerUntil = FMath::Min(AnnouncerUntil, Clock);
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

	// The music dips under a spoken line and comes back when the fighters are quiet, so the shouting is heard.
	const bool bTalking = (OppDummy && OppDummy->IsTalking()) || (PlayerDummy && PlayerDummy->IsTalking()) || Clock < AnnouncerUntil;
	if (bTalking != bMusicDucked)
	{
		bMusicDucked = bTalking;
		PlayMusic(bTalking ? 0.12f : 0.5f);
	}
	if (bLookDirty) { bLookDirty = false; Pawn->SetLook(ArenaIndex >= 0 ? KSArenas[ArenaIndex].Tint : FLinearColor::White, ArenaIndex > 0 ? 0.6f : 0.f); }

	// A shout into the microphone. Not while the announcer is booming out of the loudspeakers: that is him, not the player.
	bShoutNow = false;
	if (Mic) { Mic->Poll(); MicLevel = Mic->Level(); bShoutNow = Mic->TakeShout() && Clock >= AnnouncerUntil; }

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

	if (Phase == EKSPhase::Fight && Pressed(EKeys::P) && !bOnline) bPaused = !bPaused;   // nobody can stop the clock on another player
	if (Phase != EKSPhase::Fight) bPaused = false;
	const float GameDt = bPaused ? 0.f : Dt;
	Clock += GameDt;
	PhaseTime += GameDt;

	if (Net) TickOnline();

	// Esc leaves whatever is going on for the start menu (the film handles its own Esc; on the menu there is nowhere to go;
	// the online lobby steps back one screen at a time, see TickLobby).
	const bool bTestEsc = TestEscAt > 0.f && Phase == EKSPhase::Fight && PhaseTime > TestEscAt;
	if ((Pressed(EKeys::Escape) || bTestEsc) && Phase != EKSPhase::Menu && Phase != EKSPhase::Intro && Phase != EKSPhase::Online)
	{
		TestEscAt = 0.f;
		Sfx(EKSSfx::Select, 0.7f); HushAnnouncer();
		ToMenu();
		return;
	}

	switch (Phase)
	{
	case EKSPhase::Select: TickSelect(GameDt); break;
	case EKSPhase::Street: TickStreet(GameDt); break;
	case EKSPhase::PullIn: TickPullIn(GameDt); break;
	case EKSPhase::Card: TickCard(GameDt); break;
	case EKSPhase::Fight: TickFight(GameDt); break;
	case EKSPhase::FightEnd: TickFightEnd(GameDt); break;
	case EKSPhase::Result: TickResult(GameDt); break;
	case EKSPhase::Continue: TickContinue(GameDt); break;
	case EKSPhase::Ending: TickEnding(GameDt); break;
	case EKSPhase::Calibrate: TickCalibrate(GameDt); break;
	case EKSPhase::Menu: TickMenu(GameDt); break;
	case EKSPhase::Intro: TickIntro(GameDt); break;
	case EKSPhase::Online: TickLobby(GameDt); break;
	}

	TickFx(GameDt);
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

/* ================= the start menu ================= */

void AKSGameMode::TickMenu(float Dt)
{
	Pawn->SetView(WalkPos + FVector(0, 0, 165.f), FRotator::ZeroRotator, 90.f);
	const int32 Lines = 5;
	// The webcam works here too, once it has found the player: a left punch moves down the list, a right punch chooses.
	const bool bTracked = TrackingSubsystem && TrackingSubsystem->IsTracking() && PhaseTime > 1.5f;
	bool bNext = Pressed(EKeys::Down) || Pressed(EKeys::S) || Pressed(EKeys::Gamepad_DPad_Down), bChoose = false;
	if (bTracked) for (const TPair<EKSArm, EKSPunch>& Punch : FramePunches) { if (Punch.Key == EKSArm::Left) bNext = true; else bChoose = true; }
	if (Pressed(EKeys::Up) || Pressed(EKeys::W) || Pressed(EKeys::Gamepad_DPad_Up)) { MenuIndex = (MenuIndex + Lines - 1) % Lines; Sfx(EKSSfx::Menu, 0.7f); }
	if (bNext) { MenuIndex = (MenuIndex + 1) % Lines; Sfx(EKSSfx::Menu, 0.7f); }
	bChoose |= Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::Gamepad_FaceButton_Bottom);
	if ((PhaseTime > 0.6f && bChoose) || (bAuto && PhaseTime > 3.f))
	{
		Sfx(EKSSfx::Select, 0.8f);
		UE_LOG(LogTemp, Display, TEXT("KSTEST menu chosen=%d"), MenuIndex);   // for tools/check.sh
		if (MenuIndex == 0) { WhiteFade = 0.8f; EnterSelect(); }
		else if (MenuIndex == 1)
		{
			// Calibration is the player's choice, here: it teaches the detector how hard this player punches.
			WhiteFade = 1.f; EnterArena();
			bCalFromMenu = true; CalPeaks.Reset();
			SetPhase(EKSPhase::Calibrate); SetCalStep(0);
		}
		else if (MenuIndex == 2) { WhiteFade = 0.6f; SetPhase(EKSPhase::Intro); }
		// Online: first choose a fighter, as for a fight against the cabinet, then on to the lobby (TickSelect).
		else if (MenuIndex == 3) { WhiteFade = 0.8f; bSelectForOnline = true; EnterSelect(); }
		else PC->ConsoleCommand(TEXT("quit"));
	}
}

void AKSGameMode::TickIntro(float Dt)
{
	Pawn->SetView(WalkPos + FVector(0, 0, 165.f), FRotator::ZeroRotator, 90.f);
	float Total = 0.f;
	for (const FKSCineShot& Shot : KSCineShots) Total += Shot.Frames / KSCineFps;
	// A confirm key skips the film, and so does a jab at the webcam. Only a jab: "any punch" ended the film whenever
	// the player moved, because someone settling in front of the camera sets off the odd hook or uppercut.
	if (bFakeFilmPunches)
	{
		// For tools/check.sh: what a player moving about in front of the camera sets off, and then a real jab.
		const float Before = PhaseTime - Dt;
		if (PhaseTime > 1.6f && PhaseTime < 5.f && FMath::FloorToInt(PhaseTime * 2.f) != FMath::FloorToInt(Before * 2.f))
			FramePunches.Add({ EKSArm::Right, FMath::FloorToInt(PhaseTime * 2.f) % 2 ? EKSPunch::Hook : EKSPunch::Uppercut });
		if (Before < 6.f && PhaseTime >= 6.f) FramePunches.Add({ EKSArm::Left, EKSPunch::Straight });
	}
	bool bJab = false;
	if (PhaseTime > 1.5f) for (const TPair<EKSArm, EKSPunch>& Punch : FramePunches) bJab |= Punch.Value == EKSPunch::Straight;
	const bool bKey = Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::E) || Pressed(EKeys::Gamepad_FaceButton_Bottom) || Pressed(EKeys::Escape);
	// The self-playing test watches two and a half seconds.
	const bool bSkip = PhaseTime > 0.5f && (bKey || bJab);
	if (bSkip || PhaseTime >= Total || (bAuto && PhaseTime > 2.5f))
	{
		UE_LOG(LogTemp, Display, TEXT("KSTEST intro ended seconds=%.1f of=%.1f skipped=%d by_jab=%d"), PhaseTime, Total, PhaseTime < Total ? 1 : 0, bJab && !bKey ? 1 : 0);
		WhiteFade = 0.8f; MenuIndex = 0;
		SetPhase(EKSPhase::Menu);
		Announcer(TEXT("welcome"));
	}
}

/* ================= choosing a fighter ================= */

void AKSGameMode::EnterSelect()
{
	ClearWorld();
	RaiseArena(0);
	SeatCrowd();
	SelectIndex = FMath::Clamp(ChosenPlayer >= 0 ? ChosenPlayer : SelectIndex, 0, Roster().Num() - 1);
	SelectChosenAt = -1.f; SelectMovedAt = Clock; bSelectNameSaid = false; PortraitT = 0.f;
	Subtitle.Reset(); Prompt.Reset(); CueMain.Reset(); CueHint.Reset(); Announcement.Reset(); SpokenLine.Reset();
	ShowSelectFighter();
	SetPhase(EKSPhase::Select);
	if (!bPortraits) Announcer(TEXT("choose"));
}

void AKSGameMode::ShowSelectFighter()
{
	// A fresh actor each time, as for every bout (a second mesh on an old actor froze in one pose).
	if (PlayerDummy) { PlayerDummy->StopTalking(); WorldActors.Remove(PlayerDummy); PlayerDummy->Destroy(); }
	const FKSFighterDef& F = Roster()[SelectIndex];
	PlayerDummy = GetWorld()->SpawnActor<AKSDummy>(FVector::ZeroVector, FRotator::ZeroRotator);
	WorldActors.Add(PlayerDummy);
	PlayerDummy->Setup(F.Body, F.Gloves, false, 0.25f);
	const bool bReal = F.Asset && PlayerDummy->UseCharacter(F.Asset);
	PlayerDummy->SetActorScale3D(FVector(bReal ? 1.f : F.Height));
	PlayerDummy->Snap(KSPoses::Guard());
	if (bReal) PlayerDummy->PlayClip(TEXT("Combat_Stance"), true);
	PlayerClipKey.Reset(); SpokenLine.Reset();
	// He stands in his stance for a moment, then gets on with whatever he does when nobody is hitting him.
	SelectClipUntil = Clock + 0.35f; SelectClipStep = 0;
}

void AKSGameMode::TickSelect(float Dt)
{
	const int32 Count = Roster().Num();
	TickCrowd();
	if (bPortraits)
	{
		// One photograph of each head for the portraits (art/ui/make_avatars.py crops them): a second for the fighter
		// to load and settle, the picture, half a second for it to be written, the next fighter.
		const FVector Head = PlayerDummy ? PlayerDummy->FaceWorld(false) : FVector(0, 0, 160);
		const FVector Eye = Head + FVector(140.f, 28.f, 14.f);
		Pawn->SetView(Eye, (Head + FVector(0, 0, 10.f) - Eye).Rotation(), 40.f);
		const float Before = PortraitT; PortraitT += Dt;
		if (Before < 1.f && PortraitT >= 1.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/avatar_%s.png"), Roster()[SelectIndex].Asset), false, false);
		}
		if (PortraitT >= 1.6f)
		{
			PortraitT = 0.f;
			if (SelectIndex + 1 < Count) { SelectIndex++; ShowSelectFighter(); }
			else { UE_LOG(LogTemp, Display, TEXT("KSTEST portraits done count=%d"), Count); PC->ConsoleCommand(TEXT("quit")); }
		}
		return;
	}

	// The fighter stands right of the middle; the camera drifts a little so the hall behind is not a still picture.
	const float Sway = FMath::Sin(Clock * 0.5f);
	// Far enough back that a raised glove or a jump stays under the title band.
	const FVector Eye(415.f + 12.f * FMath::Cos(Clock * 0.37f), 92.f + 26.f * Sway, 120.f);
	Pawn->SetView(Eye, (FVector(0.f, 92.f, 108.f) - Eye).Rotation(), 52.f);
	SpokenLine = PlayerDummy && PlayerDummy->IsTalking() ? PlayerDummy->SpokenText() : FString();

	if (SelectChosenAt >= 0.f)
	{
		// He has had his say: on to the ladder as him.
		if (Clock - SelectChosenAt > FMath::Max(1.3f, SelectVoiceSeconds + 0.25f))
		{
			ChosenPlayer = SelectIndex; WhiteFade = 1.f;
			if (bSelectForOnline) { ToMenu(); EnterLobby(); return; }   // PLAY ONLINE was chosen: the fighter is picked, now find an opponent
			EnterArena(); BeginLadder();
		}
		return;
	}

	// While he waits he dances, in one unbroken loop (alternating two moves with the stance in between read as
	// stopping and starting). His win dance; a fighter without one does his first fun move.
	if (PlayerDummy && PlayerDummy->HasCharacter() && SelectClipStep == 0 && Clock >= SelectClipUntil)
	{
		SelectClipStep = 1;
		const TCHAR* Dance = PlayerDummy->HasClip(TEXT("Win")) ? TEXT("Win") : TEXT("Fun_A");
		if (PlayerDummy->HasClip(Dance))
		{
			PlayerDummy->PlayClip(Dance, true);
			UE_LOG(LogTemp, Display, TEXT("KSTEST select idle fighter=%s clip=%s"), Roster()[SelectIndex].Asset, Dance);
		}
	}

	// Keys, or the webcam once it has the player: a left punch moves on, a right punch chooses (as on the menu).
	const bool bTracked = TrackingSubsystem && TrackingSubsystem->IsTracking() && PhaseTime > 1.5f;
	int32 Move = 0; bool bChoose = false;
	if (Pressed(EKeys::Right) || Pressed(EKeys::D) || Pressed(EKeys::Gamepad_DPad_Right)) Move = 1;
	if (Pressed(EKeys::Left) || Pressed(EKeys::A) || Pressed(EKeys::Gamepad_DPad_Left)) Move = -1;
	if (bTracked) for (const TPair<EKSArm, EKSPunch>& Punch : FramePunches) { if (Punch.Key == EKSArm::Left) Move = 1; else bChoose = true; }
	bChoose |= Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::Gamepad_FaceButton_Bottom);
	if (Move != 0)
	{
		SelectIndex = (SelectIndex + Move + Count) % Count;
		SelectMovedAt = Clock; bSelectNameSaid = false;
		Sfx(EKSSfx::Menu, 0.7f); HushAnnouncer();
		ShowSelectFighter();
	}
	// The announcer calls the name once the choice has rested on someone for a moment, so flicking past is quiet.
	if (!bSelectNameSaid && Clock >= AnnouncerUntil && Clock - SelectMovedAt > 0.3f)
	{
		bSelectNameSaid = true;
		Announcer(FString(TEXT("name_")) + Roster()[SelectIndex].Asset);
	}
	if ((PhaseTime > 0.6f && bChoose) || (bAuto && PhaseTime > 2.5f))
	{
		SelectChosenAt = Clock;
		Sfx(EKSSfx::Select, 0.9f); Sfx(EKSSfx::Cheer, 0.5f); HushAnnouncer();
		CrowdReact(true, 0.8f); CrowdFlashes(6.f);
		WhiteFade = FMath::Max(WhiteFade, 0.45f);
		// He introduces himself and shows off.
		SelectVoiceSeconds = PlayerDummy ? PlayerDummy->Say(TEXT("intro"), 1.4f) : 0.f;
		if (PlayerDummy && PlayerDummy->HasCharacter())
		{
			const TCHAR* Clip = PlayerDummy->HasClip(TEXT("Picked")) ? TEXT("Picked") : TEXT("Chest_Pound_Taunt");
			PlayerDummy->PlayClip(Clip, false);
			SelectVoiceSeconds = FMath::Max(SelectVoiceSeconds, FMath::Min(PlayerDummy->ClipSeconds(Clip), 3.2f));
		}
		UE_LOG(LogTemp, Display, TEXT("KSTEST select chosen=%d fighter=%s"), SelectIndex, Roster()[SelectIndex].Asset);   // for tools/check.sh
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
	RaiseArena(0);
	PickFighters();
	PlayerDummy = GetWorld()->SpawnActor<AKSDummy>(FVector::ZeroVector, FRotator::ZeroRotator);
	PlayerDummy->Setup(KSColors::Cyan, KSColors::Magenta, true);
	if (PlayerFighter().Asset) PlayerDummy->UseCharacter(PlayerFighter().Asset);
	OppDummy = GetWorld()->SpawnActor<AKSDummy>(FVector(OppDistance, 0, 0), FRotator(0, 180, 0));
	WorldActors.Add(PlayerDummy);
	WorldActors.Add(OppDummy);
	Subtitle.Reset(); Prompt.Reset();
	if (bArenaTour) { for (int32 i = 1; i < KSArenaCount; i++) UseArena(i); UseArena(0); }
}

void AKSGameMode::PickFighters()
{
	// A random fighter for the player, then a random ladder of different opponents.
	const int32 Count = Roster().Num();
	// Online the host settled who fights and where (OnlinePickMatch).
	if (bOnline) { PlayerChar = OnlineMine; Ladder = { OnlineTheirs }; ArenaLadder = { OnlineArena }; return; }
	// The player's fighter: the one chosen on the select screen; a random one when the game starts straight in the ring.
	PlayerChar = ForcedPlayer >= 0 ? FMath::Clamp(ForcedPlayer, 0, Count - 1) : ChosenPlayer >= 0 ? FMath::Clamp(ChosenPlayer, 0, Count - 1) : FMath::RandRange(0, Count - 1);
	UE_LOG(LogTemp, Display, TEXT("KSTEST player fighter=%s chosen=%d"), Roster()[PlayerChar].Asset, ChosenPlayer >= 0 ? 1 : 0);   // for tools/check.sh
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
	// Where each bout is fought: at the opponent's home, and the last bout somewhere that belongs to nobody.
	TArray<int32> Nobodys;
	for (int32 a = 1; a < KSArenaCount; a++) if (KSArenas[a].Home < 0) Nobodys.Add(a);
	ArenaLadder.Reset();
	for (int32 i = 0; i < Ladder.Num(); i++)
	{
		int32 Where = 0;
		for (int32 a = 1; a < KSArenaCount; a++) if (KSArenas[a].Home == Ladder[i]) Where = a;
		if (i == Ladder.Num() - 1 && Nobodys.Num()) Where = Nobodys[FMath::RandRange(0, Nobodys.Num() - 1)];
		if (ForcedArena >= 0) Where = FMath::Clamp(ForcedArena, 0, KSArenaCount - 1);
		ArenaLadder.Add(Where);
	}
}

void AKSGameMode::RaiseArena(int32 Index)
{
	ArenaIndex = FMath::Clamp(Index, 0, KSArenaCount - 1);
	const FKSArenaDef& Def = KSArenas[ArenaIndex];
	FKSBuilder B = MakeBuilder();
	KSWorld::BuildArena(B, Arena, Def);
	// What is in the air here: stars, fireflies, dust, rain, petals, will-o'-the-wisps, sparks, embers, glitter.
	Motes.Reset();
	for (int32 i = 0; i < Def.Motes && Def.Mote != EKSMote::None; i++)
	{
		const bool bFlat = Def.Mote == EKSMote::Fall, bStreak = Def.Mote == EKSMote::Rain;
		const FVector Size = bStreak ? FVector(1.4f, 1.4f, 60.f) : bFlat ? FVector(15.f, 9.f, 0.8f) : FVector(Def.Mote == EKSMote::Sparks ? 6.f : FMath::FRandRange(5.f, 10.f));
		const float Shade = FMath::FRandRange(0.65f, 1.f);
		UMaterialInstanceDynamic* MID = B.Shape(bFlat || bStreak ? B.Cube : B.Sphere, FVector(0, 0, -500), Size,
			FLinearColor(Def.MoteColor.R * Shade, Def.MoteColor.G * Shade, Def.MoteColor.B * Shade), Def.MoteGlow);
		FMote M; M.Actor = MID ? Cast<AActor>(MID->GetOuter()) : nullptr; M.Phase = FMath::FRandRange(0.f, 100.f);
		ResetMote(M, true);
		Motes.Add(M);
	}
	NextBoltAt = GetWorld()->GetTimeSeconds() + FMath::FRandRange(3.f, 7.f); BoltUntil = 0.f;
	bLookDirty = true;
	// One line per arena for tools/check.sh: an arena whose picture or music is missing shows a bare wall or falls silent.
	auto Has = [](const FString& Path) { return LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr; };
	const bool bHall = !Def.Key[0];
	UE_LOG(LogTemp, Display, TEXT("KSTEST arena_built index=%d key=%s backdrop=%d mat=%d music=%d wall_panels=%d motes=%d"), ArenaIndex, bHall ? TEXT("Hall") : Def.Key,
		Has(bHall ? FString(TEXT("/Game/KS/Arena/T_KS_Stands.T_KS_Stands")) : FString::Printf(TEXT("/Game/KS/Arena/T_KS_Bg_%s.T_KS_Bg_%s"), Def.Key, Def.Key)) ? 1 : 0,
		Has(bHall ? FString(TEXT("/Game/KS/Arena/T_KS_Mat.T_KS_Mat")) : FString::Printf(TEXT("/Game/KS/Arena/T_KS_Mat_%s.T_KS_Mat_%s"), Def.Key, Def.Key)) ? 1 : 0,
		Has(FString::Printf(TEXT("/Game/KS/Audio/M_%s.M_%s"), Def.Music, Def.Music)) ? 1 : 0, Arena.Wall.Num(), Motes.Num());
}

void AKSGameMode::UseArena(int32 Index)
{
	Index = FMath::Clamp(Index, 0, KSArenaCount - 1);
	if (Index == ArenaIndex) return;
	// Everything of the old arena goes (its crowd too: SeatCrowd seats the new one); the two fighters stay.
	TObjectPtr<AKSDummy> Player = PlayerDummy, Opp = OppDummy;
	WorldActors.Remove(Player); WorldActors.Remove(Opp);
	ClearWorld();
	PlayerDummy = Player; OppDummy = Opp;
	if (Player) WorldActors.Add(Player);
	if (Opp) WorldActors.Add(Opp);
	RaiseArena(Index);
	WhiteFade = FMath::Max(WhiteFade, 0.5f);
	PlayMusic();
}

void AKSGameMode::PlayMusic(float FadeSeconds)
{
	const TCHAR* Track = (Phase == EKSPhase::Menu || Phase == EKSPhase::Intro || Phase == EKSPhase::Select || Phase == EKSPhase::Online || ArenaIndex < 0) ? TEXT("Fight") : KSArenas[ArenaIndex].Music;
	KSAudio::Music(this, MusicPlayer, MusicLevel * (bMusicDucked ? 0.4f : 1.f), FadeSeconds, Track);
}

void AKSGameMode::ResetMote(FMote& M, bool bAnywhere)
{
	// The air the fight camera looks through: over the ring and the front of the stands, not the whole hall.
	const FVector Lo(-150.f, -650.f, 0.f), Hi(800.f, 650.f, 520.f);
	M.Pos = FVector(FMath::FRandRange(Lo.X, Hi.X), FMath::FRandRange(Lo.Y, Hi.Y), FMath::FRandRange(Lo.Z, Hi.Z));
	auto Side = [](float Amount) { return FMath::FRandRange(-Amount, Amount); };
	switch (KSArenas[FMath::Clamp(ArenaIndex, 0, KSArenaCount - 1)].Mote)
	{
	case EKSMote::Drift: M.Vel = FVector(Side(6.f), Side(6.f), Side(3.f)); M.Pos.Z = FMath::FRandRange(40.f, Hi.Z); break;
	case EKSMote::Rise:  M.Vel = FVector(Side(10.f), Side(10.f), FMath::FRandRange(35.f, 110.f)); if (!bAnywhere) M.Pos.Z = 0.f; break;
	case EKSMote::Fall:  M.Vel = FVector(Side(15.f), Side(15.f), -FMath::FRandRange(45.f, 95.f)); if (!bAnywhere) M.Pos.Z = Hi.Z; break;
	case EKSMote::Rain:  M.Vel = FVector(-120.f, 60.f, -FMath::FRandRange(1700.f, 2100.f)); if (!bAnywhere) M.Pos.Z = Hi.Z; break;
	case EKSMote::Sparks:
	{
		// Welding going on at five places round the ring: each spark flies up and out and falls back.
		static const FVector From[] = { FVector(470, -380, 60), FVector(470, 380, 60), FVector(200, -470, 60), FVector(200, 470, 60), FVector(760, 0, 150) };
		M.Pos = From[FMath::RandRange(0, 4)];
		M.Vel = FVector(Side(260.f), Side(260.f), FMath::FRandRange(220.f, 620.f));
		M.Life = FMath::FRandRange(0.4f, 1.1f) * (bAnywhere ? FMath::FRand() : 1.f);
		break;
	}
	default: break;
	}
}

void AKSGameMode::TickMotes(float Now, float Dt)
{
	if (!Motes.Num() || ArenaIndex < 0) return;
	const EKSMote Kind = KSArenas[ArenaIndex].Mote;
	for (FMote& M : Motes)
	{
		AActor* Actor = M.Actor.Get();
		if (!Actor) continue;
		const FVector Wobble(FMath::Sin(Now * 0.9f + M.Phase), FMath::Cos(Now * 0.7f + M.Phase * 1.3f), FMath::Sin(Now * 1.1f + M.Phase * 2.1f));
		switch (Kind)
		{
		case EKSMote::Drift: M.Pos += (M.Vel + Wobble * 14.f) * Dt; if (M.Pos.Z < 20.f || M.Pos.Z > 560.f || FMath::Abs(M.Pos.Y) > 700.f || M.Pos.X < -200.f || M.Pos.X > 850.f) ResetMote(M, true); break;
		case EKSMote::Rise:  M.Pos += (M.Vel + Wobble * FVector(28.f, 28.f, 0.f)) * Dt; if (M.Pos.Z > 520.f) ResetMote(M, false); break;
		case EKSMote::Fall:  M.Pos += (M.Vel + Wobble * FVector(45.f, 45.f, 0.f)) * Dt; if (M.Pos.Z < 3.f) ResetMote(M, false); break;
		case EKSMote::Rain:  M.Pos += M.Vel * Dt; if (M.Pos.Z < 0.f) ResetMote(M, false); break;
		case EKSMote::Sparks: M.Life -= Dt; M.Vel.Z -= 900.f * Dt; M.Pos += M.Vel * Dt; if (M.Life <= 0.f || M.Pos.Z < 0.f) ResetMote(M, false); break;
		default: break;
		}
		if (Kind == EKSMote::Fall) Actor->SetActorLocationAndRotation(M.Pos, FRotator(Now * 70.f + M.Phase * 40.f, M.Phase * 60.f, Now * 110.f + M.Phase * 25.f));
		else Actor->SetActorLocation(M.Pos);
	}
}

void AKSGameMode::StartCard(int32 Index)
{
	FighterIndex = FMath::Clamp(Index, 0, Ladder.Num() - 1);
	UseArena(ArenaLadder.IsValidIndex(FighterIndex) ? ArenaLadder[FighterIndex] : 0);
	CurrentDef = Roster()[Ladder[FighterIndex]];
	const FLadderTier& Tier = LadderTiers[FMath::Min(FighterIndex, LadderLength - 1)];
	CurrentDef.Health = Tier.Health; CurrentDef.WindupScale = Tier.WindupScale; CurrentDef.DamageScale = Tier.DamageScale;
	CurrentDef.FeintChance = Tier.FeintChance; CurrentDef.IdleScale = Tier.IdleScale; CurrentDef.MaxHitsOpen = Tier.MaxHitsOpen;
	// Two players meet as equals: the same health as the player's own, and no ladder difficulty.
	if (bOnline) { CurrentDef.Health = 100.f; CurrentDef.WindupScale = CurrentDef.DamageScale = CurrentDef.IdleScale = 1.f; CurrentDef.FeintChance = 0.f; }
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
	bIntroSaid = false; bChampionSaid = false; VoiceQuietUntil = 0.f; LaterVoices.Reset();
	PlayerMeter = OppMeter = 0.f; bSpecialOn = false; bMeterFullSaid = false; OppStaggerUntil = 0.f; SpecialImpact = -10.f;
	SetPhase(EKSPhase::Card);
	// The announcer calls the opponent into the ring; the opponent answers when he has finished.
	if (F.Asset) Announcer(FString(TEXT("name_")) + F.Asset);
}

void AKSGameMode::TickCard(float Dt)
{
	if (PhaseTime > 0.6f && PhaseTime < 2.6f && ConfirmPressed()) PhaseTime = 2.6f;
	if (PhaseTime >= 2.6f && !bFightStarted)
	{
		bFightStarted = true;
		Announce(bOnline ? FString(TEXT("ONLINE MATCH")) : FString::Printf(TEXT("FIGHT %d OF %d"), FighterIndex + 1, NumFights()), 1.3f);
	}
	if (!bIntroSaid && PhaseTime >= 0.5f && Clock >= AnnouncerUntil + 0.1f) { bIntroSaid = true; Speak(OppDummy, TEXT("intro"), true); }
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	// The bell waits for him to finish his sentence (within reason).
	// Online the host rings the bell for both, so the two clocks start together; the guest waits for it (see OnlineLine).
	if (bOnline && Net && !Net->IsHost()) return;
	if (PhaseTime >= 4.1f && ((bIntroSaid && (!OppDummy || !OppDummy->IsTalking())) || PhaseTime >= 8.5f)) StartFight();
}

void AKSGameMode::StartFight()
{
	if (bOnline && Net && Net->IsHost()) Net->Send(TEXT("GO"));
	Sfx(EKSSfx::Bell1);
	Announce(TEXT("FIGHT!"), 0.8f);
	Announcer(bOnline ? TEXT("round1") : FighterIndex + 1 >= NumFights() ? TEXT("round3") : FighterIndex == 0 ? TEXT("round1") : TEXT("round2"));
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
		// Before that, a second and a half of the place itself: a wide look over the ring at the painted wall. The
		// fight camera sits low over the player's shoulder and never shows more of an arena than its crowd.
		const float Wide = 1.f - FMath::SmoothStep(1.5f, 2.3f, PhaseTime);
		Pos = FMath::Lerp(Pos, FVector(-560.f + 60.f * PhaseTime, -90.f + 50.f * PhaseTime, 250.f), Wide);
		Aim = FMath::Lerp(Aim, FVector(1200.f, 0.f, 430.f), Wide);
		Fov = FMath::Lerp(Fov, 84.f, Wide);
	}
	/* ---- the cameraman ----
	   A Steadicam operator inside the ropes. He walks round the two fighters and chooses his own shots: over either of
	   the player's shoulders, three-quarter, side-on, low, high and wide. OpAngle is where he stands: 0 straight behind
	   the player, positive on the player's right, +-180 behind the opponent. He is free to go all the way round only
	   while the player has nothing to defend against (the opponent is open or down, or the bout is over); otherwise he
	   keeps to the player's half, and when a punch is wound up while he is out at the side he runs back behind a
	   shoulder, so the player always reads an attack from his own point of view. */
	const FVector OpPivot(OppDistance * 0.5f, 0.f, 0.f), HomeArm = FightPos - OpPivot;
	const float HomeAngle = FMath::RadiansToDegrees(FMath::Atan2(HomeArm.Y, -HomeArm.X)), HomeRadius = HomeArm.Size2D();
	const bool bOperator = Phase == EKSPhase::Fight || Phase == EKSPhase::FightEnd || Phase == EKSPhase::Result || Phase == EKSPhase::Continue;
	const float OpDt = FMath::Min(Dt, 0.033f);
	float OpRollWant = 0.f, OpSway = 0.f;
	if (!bOperator)
	{
		// The card's last frame is the over-the-shoulder view: that is where he starts each bout.
		OpAngle = OpWantAngle = HomeAngle; OpRadius = OpWantRadius = HomeRadius; OpHeight = OpWantHeight = FightPos.Z;
		OpAngleVel = OpRadiusVel = OpHeightVel = 0.f; OpFov = OpWantFov = 62.f; OpShotUntil = 0.f; OpHurry = 0.f; bOpAimSet = false;
		OpSeenState = EKSOppState::Idle;
	}
	else
	{
		const bool bBout = Phase == EKSPhase::Fight;
		const bool bFree = !bBout || OppState == EKSOppState::Open || OppState == EKSOppState::KO;
		auto Wrap = [](float A) { return FRotator::NormalizeAxis(A); };
		auto Either = []() { return FMath::RandBool() ? 1.f : -1.f; };
		if (OpShotUntil <= 0.f) OpShotUntil = Clock + FMath::FRandRange(1.2f, 2.2f);
		bool bNewShot = false;
		if (bBout && OppState != OpSeenState)
		{
			if (OppState == EKSOppState::Windup && FMath::Abs(OpAngle) > 62.f)
			{
				// A punch is coming and he is out at the side: back behind the nearer shoulder, at a run.
				OpWantAngle = FMath::Sign(OpAngle) * FMath::FRandRange(20.f, 36.f); OpWantRadius = HomeRadius; OpWantHeight = FMath::FRandRange(192.f, 222.f); OpWantFov = 62.f;
				OpHurry = 1.f; OpShotUntil = Clock + FMath::FRandRange(2.2f, 3.4f); bNewShot = true;
			}
			else if (OppState == EKSOppState::Open)
			{
				// The opponent is wide open: he dives in close and low and swings round to see the punches land.
				OpWantAngle = (FMath::Abs(OpAngle) > 8.f && FMath::FRand() < 0.7f ? FMath::Sign(OpAngle) : Either()) * FMath::FRandRange(60.f, 124.f); OpWantRadius = FMath::FRandRange(205.f, 245.f); OpWantHeight = FMath::FRandRange(92.f, 150.f); OpWantFov = 58.f;
				OpHurry = 1.f; OpShotUntil = Clock + FMath::FRandRange(1.5f, 2.4f); bNewShot = true;
			}
		}
		OpSeenState = OppState;
		if (!bNewShot && Clock >= OpShotUntil)
		{
			bNewShot = true; OpHurry = 0.f;
			if (!bBout)
			{
				// After the bell: round and round the winner and the fallen, all the way.
				if (OpDir == 0.f) OpDir = Either();
				OpWantAngle = Wrap(OpAngle + OpDir * FMath::FRandRange(70.f, 150.f)); OpWantRadius = FMath::FRandRange(250.f, 330.f); OpWantHeight = FMath::FRandRange(100.f, 215.f); OpWantFov = FMath::FRandRange(58.f, 66.f);
				OpShotUntil = Clock + FMath::FRandRange(2.2f, 3.6f);
			}
			else
			{
				float Want = OpAngle;
				for (int32 Try = 0; Try < 6; Try++)
				{
					const float Roll = FMath::FRand(), Side = Either();
					if (Roll < 0.30f)      { Want = Side * FMath::FRandRange(18.f, 36.f);  OpWantRadius = HomeRadius;                     OpWantHeight = FMath::FRandRange(195.f, 225.f); OpWantFov = 62.f; }   // over a shoulder
					else if (Roll < 0.52f) { Want = Side * FMath::FRandRange(44.f, 66.f);  OpWantRadius = FMath::FRandRange(270.f, 320.f); OpWantHeight = FMath::FRandRange(150.f, 200.f); OpWantFov = 62.f; }   // three-quarter
					else if (Roll < 0.74f) { Want = Side * FMath::FRandRange(78.f, 102.f); OpWantRadius = FMath::FRandRange(300.f, 335.f); OpWantHeight = FMath::FRandRange(118.f, 165.f); OpWantFov = 60.f; }   // side-on, both in profile
					else if (Roll < 0.88f) { Want = Side * FMath::FRandRange(56.f, 100.f); OpWantRadius = FMath::FRandRange(228.f, 268.f); OpWantHeight = FMath::FRandRange(68.f, 104.f);  OpWantFov = 66.f; }   // down on one knee, looking up
					else                   { Want = Side * FMath::FRandRange(40.f, 96.f);  OpWantRadius = FMath::FRandRange(326.f, 345.f); OpWantHeight = FMath::FRandRange(268.f, 318.f); OpWantFov = 68.f; }   // held high over his head, wide
					if (FMath::Abs(Want - OpAngle) > 28.f) break;        // a new shot is somewhere else
				}
				OpWantAngle = Want;
				OpShotUntil = Clock + FMath::FRandRange(2.2f, 4.4f);
			}
		}
		if (bNewShot) OpShots++;
		if (bFree) OpDir = OpDir == 0.f ? Either() : OpDir; else OpDir = 0.f;
		// Out of bounds for the moment (the opponent recovered while he was behind him): back to the player's half.
		const float Limit = 104.f;
		if (!bFree && FMath::Abs(OpWantAngle) > Limit) { OpWantAngle = FMath::Sign(OpAngle) * FMath::FRandRange(60.f, 100.f); OpWantRadius = FMath::Max(OpWantRadius, 285.f); OpHurry = FMath::Max(OpHurry, 0.6f); }

		// Legs, not rails: he gathers speed, tops out, and settles with a little overshoot. In the player's half he
		// never takes the short cut behind the opponent.
		const float Delta = bFree ? FMath::FindDeltaAngleDegrees(OpAngle, OpWantAngle) : OpWantAngle - OpAngle;
		const float Stiff = FMath::Lerp(4.5f, 13.f, OpHurry), MaxSpeed = FMath::Lerp(bBout ? 78.f : 46.f, 195.f, OpHurry), MaxAccel = FMath::Lerp(190.f, 640.f, OpHurry);
		OpAngleVel += FMath::Clamp(Stiff * Delta - 1.75f * FMath::Sqrt(Stiff) * OpAngleVel, -MaxAccel, MaxAccel) * OpDt;
		OpAngleVel = FMath::Clamp(OpAngleVel, -MaxSpeed, MaxSpeed);
		OpAngle = Wrap(OpAngle + OpAngleVel * OpDt);
		if (FMath::Abs(Delta) < 6.f) OpHurry = FMath::FInterpTo(OpHurry, 0.f, Dt, 3.f);
		OpRadiusVel += (7.f * (OpWantRadius - OpRadius) - 4.6f * OpRadiusVel) * OpDt; OpRadius += OpRadiusVel * OpDt;
		OpHeightVel += (7.f * (OpWantHeight - OpHeight) - 4.6f * OpHeightVel) * OpDt; OpHeight += OpHeightVel * OpDt;
		OpFov = FMath::FInterpTo(OpFov, OpWantFov, Dt, 2.5f);

		const float Rad = FMath::DegreesToRadians(OpAngle);
		// Seen in stills: from straight behind either fighter that fighter's back hides the other one, and close in from
		// behind it fills the picture. So he comes close only from the side, keeps his distance behind a back, and when
		// he has to cross right behind one he lifts the camera to look over the head.
		const float OffSide = FMath::Abs(FMath::Sin(Rad));                                          // 0 behind a fighter, 1 square to the side
		const float Radius = FMath::Max(OpRadius, FMath::Lerp(HomeRadius, 205.f, FMath::SmoothStep(0.55f, 0.92f, OffSide)));
		const float Over = 1.f - FMath::SmoothStep(0.12f, 0.33f, OffSide);
		Pos = OpPivot + FVector(-FMath::Cos(Rad) * Radius, FMath::Sin(Rad) * Radius, FMath::Lerp(OpHeight, FMath::Max(OpHeight, 268.f), Over));
		Pos.X = FMath::Clamp(Pos.X, -328.f, 328.f); Pos.Y = FMath::Clamp(Pos.Y, -328.f, 328.f);      // he stays inside the ropes
		// His footsteps come through the rig as a small bob and sway, more the faster he goes; his breathing always.
		const float Ground = FMath::Abs(OpAngleVel) * PI / 180.f * Radius + FMath::Abs(OpRadiusVel), Stride = FMath::Clamp(Ground / 150.f, 0.f, 1.f);
		OpStep += Ground / 85.f * OpDt;
		Pos.Z += FMath::Sin(OpStep * 2.f * PI) * 2.2f * Stride + FMath::Sin(Clock * 1.3f + 2.f) * 1.2f;
		OpSway = FMath::Sin(OpStep * PI) * 2.6f * Stride + FMath::Sin(Clock * 0.9f) * 1.6f;
		OpRollWant = FMath::Sin(OpStep * PI) * 0.8f * Stride - OpAngleVel * 0.034f;                  // and he leans into the run
		// What he points at follows from where he stands: the opponent from behind the player, both from the side, the
		// player from behind the opponent; up at them from below, down from above. The frame trails him a little.
		const float Mix = 0.5f + 0.5f * FMath::Cos(Rad);
		FVector AimWant(OppDistance * Mix, -8.f * FMath::Sin(Rad), 128.f - 0.12f * (Pos.Z - 212.f));
		if (!bBout) AimWant = FMath::Lerp(AimWant, FVector(OppDistance * 0.5f, 0.f, 100.f), 0.85f * FMath::SmoothStep(0.f, 2.f, PhaseTime));      // after the bell: both of them, the one standing and the one on the floor
		AimWant += FVector(0.f, FMath::Sin(Clock * 0.53f) * 3.f, FMath::Sin(Clock * 0.81f + 0.7f) * 2.5f);
		if (!bOpAimSet) { OpAim = AimWant; bOpAimSet = true; }
		OpAim = FMath::VInterpTo(OpAim, AimWant, Dt, 6.f);
		Aim = OpAim; Fov = OpFov;
		OpTravel += FMath::Abs(OpAngleVel) * OpDt; OpMinAngle = FMath::Min(OpMinAngle, OpAngle); OpMaxAngle = FMath::Max(OpMaxAngle, OpAngle);
	}
	OpRoll = FMath::FInterpTo(OpRoll, OpRollWant, Dt, 4.f);

	if (Clock < SpecialImpact + 0.75f && (Phase == EKSPhase::Fight || Phase == EKSPhase::FightEnd))
	{
		// A special ability gets its own shot, from the side: close on whoever is doing it while he charges up, then
		// wide enough for both, so what crosses the ring is seen crossing it (from behind the player his own body hid it).
		const float T = Clock - SpecialStart;
		const float W = FMath::SmoothStep(0.f, 0.18f, T) * (1.f - FMath::SmoothStep(SpecialImpact + 0.4f, SpecialImpact + 0.75f, Clock));
		const float Wide = FMath::SmoothStep(SpecialStart + 0.45f * (SpecialImpact - SpecialStart), SpecialImpact - 0.1f, Clock);
		const FVector Who(bSpecialByPlayer ? 0.f : OppDistance, 0.f, 0.f);
		const float Face = bSpecialByPlayer ? 1.f : -1.f;
		const FVector ClosePos = Who + FVector(Face * (150.f - 15.f * T), 300.f, 140.f), CloseAim = Who + FVector(0.f, 0.f, 122.f);
		const FVector WidePos(OppDistance * 0.5f, 400.f, 132.f), WideAim(OppDistance * 0.5f, 0.f, 112.f);
		Pos = FMath::Lerp(Pos, FMath::Lerp(ClosePos, WidePos, Wide), W);
		Aim = FMath::Lerp(Aim, FMath::Lerp(CloseAim, WideAim, Wide), W);
		Fov = FMath::Lerp(Fov, FMath::Lerp(50.f, 60.f, Wide), W);
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
		// A nervous hand when the player is nearly out.
		if (PlayerHP < 30.f) Pos += FVector(FMath::Sin(Clock * 7.3f), FMath::Sin(Clock * 9.1f + 2.f), FMath::Sin(Clock * 8.2f + 4.f)) * (2.5f * (1.f - PlayerHP / 30.f));
		// Keeps the opponent's head in the picture as he sways and reels.
		if (OppDummy) CamAimLag = FMath::VInterpTo(CamAimLag, (OppDummy->FaceWorld(false) - FVector(OppDistance, 0.f, 150.f)) * (0.35f * (0.5f + 0.5f * FMath::Cos(FMath::DegreesToRadians(OpAngle)))), Dt, 5.f);
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
	const FVector Move = Forward * (CamDolly + CamOffset.X) + Right * (CamSide + CamOffset.Y + OpSway) + FVector(0, 0, CamOffset.Z - CamDrop);
	Pos += Move;
	Aim += Move * 0.35f + CamAimLag + FVector(0, 0, -CamDrop * 0.5f);
	Pos += FMath::VRand() * Shake * 0.6f;
	FRotator Rot = (Aim - Pos).Rotation();
	Rot.Roll = CamRoll + OpRoll;
	const float FinalFov = Fov + WantFov * FMath::Clamp(FMath::Abs(CamDolly) / 24.f, 0.f, 1.f) + CamFovKick;
	Pawn->SetView(Pos, Rot, FinalFov);
	// For tools/check.sh: a cameraman who wanders off is worse than one who stands still. Count the frames of a bout
	// in which the opponent's head is outside the picture (a special ability has its own shot and is not counted).
	if (Phase == EKSPhase::Fight && OppDummy && Clock >= SpecialImpact + 0.75f)
	{
		const FVector Local = Rot.UnrotateVector(OppDummy->FaceWorld(false) - Pos);
		const float Tan = FMath::Tan(FMath::DegreesToRadians(FinalFov * 0.5f));
		OpFrames++;
		if (Local.X < 10.f || FMath::Abs(Local.Y) > Local.X * Tan || FMath::Abs(Local.Z) > Local.X * Tan * 9.f / 16.f) OpLost++;
		// And those in which the player's own head and shoulders are in front of it (the camera straight behind his back).
		else if (PlayerDummy)
		{
			const FVector Mine = Rot.UnrotateVector(PlayerDummy->FaceWorld(false) - Pos);
			if (Mine.X > 10.f && Mine.X < Local.X && FMath::Abs(Mine.Y / Mine.X - Local.Y / Local.X) < 0.075f && Mine.Z / Mine.X > Local.Z / Local.X - 0.05f) OpHidden++;
		}
	}
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
	// Calibration is no longer forced on webcam players before the first bout: it is a line on the start menu.
	// Only the self-check's -KSCalDemo still enters it from here.
	if (bCalDemo)
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
	UE_LOG(LogTemp, Display, TEXT("KSTEST calibration hint_changes=%d"), CalHintChanges);
	UE_LOG(LogTemp, Display, TEXT("KSTEST calibration done completed=%d"), bCompleted ? 1 : 0);
	CalClipKey.Reset(); CalChecks.Reset(); CalTitle.Reset(); CalHint.Reset();
	if (bCalFromMenu) { WhiteFade = 0.8f; ToMenu(); return; }
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
	// -KSCalJitter (with -KSCalDemo): a player standing right on a limit, one reading flipping every frame.
	const bool bJitter = bCalJitter && bCalDemo;
	// What the screen says follows the camera only once a reading has stayed changed for a third of a second, and a
	// reading that is right stays right until it is clearly wrong. Read raw, every line of text flickered for a player
	// standing near a limit.
	auto Steady = [&](int32 i, bool bRaw)
	{
		if (bRaw == bCalSteady[i]) CalUnsteady[i] = 0.f;
		else if ((CalUnsteady[i] += Dt) > 0.35f) { bCalSteady[i] = bRaw; CalUnsteady[i] = 0.f; }
		return bCalSteady[i];
	};
	const bool bHave = bJitter || (T && T->IsBodySeen() && L->Num() >= 33);
	if (!bJitter && (!T || !T->IsConnected())) Framing = PhaseTime > 6.f ? TEXT("NO CAMERA FOUND  -  PRESS K TO PLAY WITH THE KEYBOARD") : TEXT("LOOKING FOR THE CAMERA...");
	else if (!Steady(0, bHave)) Framing = TEXT("STEP INTO VIEW");
	else
	{
		if (bHave)
		{
			const float ShoulderY = bJitter ? 0.3f : ((*L)[11].Y + (*L)[12].Y) * 0.5f, MidX = bJitter ? 0.5f : ((*L)[11].X + (*L)[12].X) * 0.5f;
			const float Width = bJitter ? 0.2f : FMath::Abs((*L)[11].X - (*L)[12].X), ElbowY = bJitter ? (GFrameCounter % 2 ? 0.93f : 0.97f) : FMath::Max((*L)[13].Y, (*L)[14].Y);
			Steady(1, ShoulderY < (bCalSteady[1] ? 0.55f : 0.52f));
			Steady(2, ElbowY < (bCalSteady[2] ? 0.985f : 0.95f));
			Steady(3, FMath::Abs(MidX - 0.5f) < (bCalSteady[3] ? 0.23f : 0.2f));
			Steady(4, Width > (bCalSteady[4] ? 0.06f : 0.07f));
			bCalLeftOfMiddle = MidX < 0.5f;
		}
		// (For the moment the body is lost but not yet called lost, the last readings stand.)
		const bool bHigh = bCalSteady[1], bElbows = bCalSteady[2], bCentred = bCalSteady[3], bNear = bCalSteady[4];
		CalChecks.Add({ TEXT("shoulders in the upper half of the picture"), bHigh });
		CalChecks.Add({ TEXT("both elbows inside the picture"), bElbows });
		CalChecks.Add({ TEXT("standing in the middle"), bCentred });
		CalChecks.Add({ TEXT("close enough to be seen clearly"), bNear });
		bFramed = bHigh && bElbows && bCentred && bNear;
		if (!bHigh || !bElbows) Framing = TEXT("STEP BACK  (or tilt the screen back)");
		else if (!bNear) Framing = TEXT("COME CLOSER");
		// A small x in the picture is the player's own right-hand side.
		else if (!bCentred) Framing = bCalLeftOfMiddle ? TEXT("MOVE TO YOUR LEFT") : TEXT("MOVE TO YOUR RIGHT");
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
	if (CalHint != CalHintWas) { CalHintWas = CalHint; CalHintChanges++; }

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
	// Places all round the ring (the stands are built in KSWorld::BuildArena): three rows behind the far ropes and
	// three behind the near ones, two rows and a platform row down each side, a block in each corner, a back row for the
	// giants on every side, and above them the ones that fly. Each row is a step higher than the one in front.
	// Row says who may stand there: 0 the front (small ones), 1 the middle, 2 the back, 3 giants only, 4 the air.
	struct FPlace { FVector Foot; int32 Row; };
	TArray<FPlace> Places;
	auto Jitter = [](float Amount) { return FMath::FRandRange(-Amount, Amount); };
	const float Gap = 94.f;
	for (int32 End = -1; End <= 1; End += 2)                 // +1 the far end, behind the opponent; -1 the near end, behind the player
	{
		for (int32 Row = 0; Row < 3; Row++)
		{
			const float Half = 430.f + 70.f * Row;
			for (float Y = -Half + (Row % 2) * Gap * 0.5f; Y <= Half; Y += Gap)
			{
				// The near end keeps an aisle down the middle: the wide shot that opens each bout looks along it.
				if (End < 0 && FMath::Abs(Y) < 235.f) continue;
				Places.Add({ FVector(End * (465.f + 118.f * Row + Jitter(16.f)), Y + Jitter(18.f), 40.f * Row), Row });
			}
		}
		for (float Y = -520.f; Y <= 520.f; Y += Gap) Places.Add({ FVector(End * (890.f + Jitter(12.f)), Y + Jitter(18.f), 140.f), 2 });      // the front of the back platform
		for (float Y = -600.f; Y <= 600.f; Y += 300.f) Places.Add({ FVector(End * (1010.f + Jitter(25.f)), Y + Jitter(50.f), 140.f), 3 });
		for (int32 i = 0; i < 3; i++) Places.Add({ FVector(End * FMath::FRandRange(520.f, 860.f), FMath::FRandRange(-620.f, 620.f), FMath::FRandRange(340.f, 520.f)), 4 });
		// The corners, between the end stands and the side stands.
		for (int32 Side = -1; Side <= 1; Side += 2) for (int32 i = 0; i < 3; i++) for (int32 k = 0; k < 3; k++)
			Places.Add({ FVector(End * (500.f + 104.f * i + Jitter(14.f)), Side * (655.f + 104.f * k + Jitter(14.f)), 60.f + 30.f * FMath::Min(i, k)), FMath::Min(i, k) });      // steps rise towards the corner, like the blocks under them
	}
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		for (int32 Row = 0; Row < 2; Row++)
		{
			for (float X = -330.f + (Row % 2) * Gap * 0.5f; X <= 400.f; X += Gap) Places.Add({ FVector(X + Jitter(18.f), Side * (470.f + 118.f * Row + Jitter(16.f)), 40.f * Row), Row });
		}
		for (float X = -300.f; X <= 400.f; X += Gap) Places.Add({ FVector(X + Jitter(18.f), Side * (770.f + Jitter(12.f)), 100.f), 2 });
		for (float X = -200.f; X <= 380.f; X += 290.f) Places.Add({ FVector(X + Jitter(40.f), Side * (890.f + Jitter(20.f)), 100.f), 3 });
		for (int32 i = 0; i < 2; i++) Places.Add({ FVector(FMath::FRandRange(-250.f, 380.f), Side * FMath::FRandRange(540.f, 780.f), FMath::FRandRange(320.f, 480.f)), 4 });
	}
	// The best places first: nearest the ring. Whoever is dealt first sits there.
	Places.Sort([](const FPlace& A, const FPlace& C) { return A.Row != C.Row ? A.Row < C.Row : FVector2D(A.Foot.X - 75.f, A.Foot.Y).SizeSquared() < FVector2D(C.Foot.X - 75.f, C.Foot.Y).SizeSquared(); });

	// Who may stand in a row, in order of preference: the small ones in front, so nobody stands behind a taller man.
	static const EKSBuild Front[] = { EKSBuild::Tiny, EKSBuild::Small, EKSBuild::Normal, EKSBuild::Tall };
	static const EKSBuild Middle[] = { EKSBuild::Small, EKSBuild::Normal, EKSBuild::Tall };
	static const EKSBuild Back[] = { EKSBuild::Tall, EKSBuild::Normal, EKSBuild::Small };
	static const EKSBuild Giants[] = { EKSBuild::Giant };
	static const EKSBuild Air[] = { EKSBuild::Flyer };
	// Nobody is in the hall twice (player request, 2026-11-13): every character is dealt once from a shuffled deck, and
	// a place nobody is left for stays empty. An arena's own people (named in its definition) are dealt first, so they
	// get the front rows; everybody else fills the rest of the house.
	TArray<FString> Locals;
	if (ArenaIndex >= 0) FString(KSArenas[ArenaIndex].Crowd).ParseIntoArray(Locals, TEXT(" "));
	TArray<int32> Deck;
	for (int32 i = 0; i < UE_ARRAY_COUNT(KSCrowdSheets); i++) Deck.Add(i);
	for (int32 i = Deck.Num() - 1; i > 0; i--) Deck.Swap(i, FMath::RandRange(0, i));
	Deck.StableSort([&](int32 A, int32 C) { return Locals.Contains(KSCrowdSheets[A].Name) && !Locals.Contains(KSCrowdSheets[C].Name); });
	FKSBuilder B = MakeBuilder();
	const float Now = GetWorld()->GetTimeSeconds();
	TSet<int32> Seated;
	for (const FPlace& Place : Places)
	{
		TArrayView<const EKSBuild> Wanted = Place.Row == 0 ? MakeArrayView(Front) : Place.Row == 1 ? MakeArrayView(Middle) : Place.Row == 2 ? MakeArrayView(Back) : Place.Row == 3 ? MakeArrayView(Giants) : MakeArrayView(Air);
		int32 Pick = INDEX_NONE;
		for (EKSBuild Build : Wanted)
		{
			for (int32 i = 0; i < Deck.Num(); i++) if (KSCrowdSheets[Deck[i]].Build == Build) { Pick = Deck[i]; Deck.RemoveAt(i); break; }
			if (Pick != INDEX_NONE) break;
		}
		if (Pick == INDEX_NONE) continue;
		if (Seated.Contains(Pick)) { CrowdTwice++; continue; }
		Seated.Add(Pick);
		const FKSCrowdSheet& Sheet = KSCrowdSheets[Pick];
		const FString Tex = FString::Printf(TEXT("/Game/KS/Arena/T_KS_Crowd_%s.T_KS_Crowd_%s"), Sheet.Name, Sheet.Name);
		// Each one faces the middle of the ring.
		const FVector ToRing = (FVector(75.f, 0.f, 0.f) - FVector(Place.Foot.X, Place.Foot.Y, 0.f)).GetSafeNormal();
		// People are people-sized: a head or so taller than life, because they stand well back from the ring.
		float Height = 200.f;
		switch (Sheet.Build)
		{
		case EKSBuild::Tiny:   Height = FMath::FRandRange(95.f, 125.f); break;
		case EKSBuild::Small:  Height = FMath::FRandRange(150.f, 172.f); break;
		case EKSBuild::Normal: Height = FMath::FRandRange(188.f, 214.f); break;
		case EKSBuild::Tall:   Height = FMath::FRandRange(216.f, 240.f); break;
		case EKSBuild::Giant:  Height = FMath::FRandRange(285.f, 350.f); break;
		case EKSBuild::Flyer:  Height = FMath::FRandRange(110.f, 185.f); break;
		}
		if (Sheet.bWide && Sheet.Build != EKSBuild::Giant) Height *= 0.8f;       // a broad body at full height looks twice the size of its neighbours
		FSpectator S;
		S.Foot = Place.Foot; S.Across = FVector(-ToRing.Y, ToRing.X, 0.f); S.Height = Height; S.Width = Sheet.bWide ? Height : Height * 0.5f;
		S.Build = Sheet.Build; S.bBeast = Sheet.bWide && Sheet.Build == EKSBuild::Giant;
		// Depth: each row back is a little dimmer, as if seen through more of the hall's haze.
		const float Glow = (Place.Row == 4 ? 0.85f : 1.f - 0.16f * Place.Row) * FMath::FRandRange(0.9f, 1.f);
		UMaterialInstanceDynamic* MID = B.Image(*Tex, false, S.Foot + FVector(0, 0, Height * 0.5f), S.Across, FVector(0, 0, -1), S.Width, S.Height,
			FVector4(0.25f, 0.5f, 0.f, 0.f), Glow, TEXT("/Game/KS/Arena/M_KS_Sprite.M_KS_Sprite"));
		if (!MID) continue;
		// A dark patch under the feet: without it a flat picture seems to float in front of the stands.
		if (Place.Row != 4) B.Shape(B.Cylinder, S.Foot + ToRing * 6.f + FVector(0, 0, 1.2f), FVector(S.Width * 0.62f, S.Width * 0.2f + 10.f, 1.6f), FLinearColor(0.f, 0.f, 0.f), 0.f, S.Across.Rotation());
		S.MID = MID; S.Actor = Cast<AActor>(MID->GetOuter());
		S.Phase = FMath::FRandRange(0.f, 20.f); S.bFlip = FMath::RandBool(); S.bRival = FMath::FRand() < 0.18f;
		S.IdleStyle = FMath::RandRange(0, 3);
		// The big ones are slow and heavy, the little ones quick.
		const float Pace = S.Build == EKSBuild::Giant ? 0.55f : S.Build == EKSBuild::Tiny ? 1.4f : 1.f;
		// Rates are per second: looks about, breaths, jumps, sways.
		S.IdleRate = FMath::FRandRange(0.12f, 0.4f) * Pace; S.BobRate = FMath::FRandRange(0.22f, 0.45f); S.BobHeight = FMath::FRandRange(0.015f, 0.04f) * Height;
		S.JumpRate = FMath::FRandRange(1.5f, 2.5f) * Pace; S.JumpHeight = FMath::FRandRange(0.06f, 0.15f) * Height;
		S.SwayRate = FMath::FRandRange(0.55f, 1.2f) * Pace; S.SwayAngle = FMath::FRandRange(3.f, 8.f);
		if (S.Build == EKSBuild::Flyer)
		{
			S.HoverRate = FMath::FRandRange(0.8f, 2.2f); S.HoverHeight = FMath::FRandRange(12.f, 40.f);
			S.DriftRate = FMath::FRandRange(0.15f, 0.5f); S.DriftWidth = FMath::FRandRange(40.f, 160.f);
		}
		S.NextWhim = Now + FMath::FRandRange(1.f, 9.f);
		Spectators.Add(S);
	}
	NextWaveAt = Now + FMath::FRandRange(15.f, 30.f);
	// For tools/check.sh: how many sit on each side of the ring (far, near, left, right of the fighters), and whether anybody is there twice.
	int32 Sides[4] = { 0, 0, 0, 0 };
	for (const FSpectator& S : Spectators) { const float DX = S.Foot.X - 75.f; Sides[FMath::Abs(DX) >= FMath::Abs(S.Foot.Y) ? (DX > 0.f ? 0 : 1) : (S.Foot.Y < 0.f ? 2 : 3)]++; }
	UE_LOG(LogTemp, Display, TEXT("KSTEST crowd seated=%d characters=%d far=%d near=%d left=%d right=%d twice=%d places=%d"), Spectators.Num(), (int32)UE_ARRAY_COUNT(KSCrowdSheets), Sides[0], Sides[1], Sides[2], Sides[3], CrowdTwice, Places.Num());
	// The flash pool.
	for (int32 i = 0; i < 10; i++)
	{
		UMaterialInstanceDynamic* MID = B.Shape(B.Sphere, FVector(0, 0, -500), FVector(20.f), FLinearColor::White, 90.f);
		FCrowdFlash F; F.MID = MID; F.Actor = MID ? Cast<AActor>(MID->GetOuter()) : nullptr;
		if (AActor* Actor = F.Actor.Get()) Actor->SetActorHiddenInGame(true);
		Flashes.Add(F);
	}
	for (int32 i = 0; i < 3; i++) FlashLights.Add(B.Light(FVector(0, 0, -500), FLinearColor(0.9f, 0.95f, 1.f), 0.f, 900.f));
	// Confetti, kept out of sight until somebody wins by knockout.
	static const FLinearColor Paper[] = { KSColors::Cyan, KSColors::Magenta, KSColors::Yellow, KSColors::Green, FLinearColor::White };
	for (int32 i = 0; i < 110; i++)
	{
		UMaterialInstanceDynamic* MID = B.Box(FVector(0, 0, -500), FVector(9.f, 5.f, 0.6f), Paper[i % UE_ARRAY_COUNT(Paper)], 2.5f);
		FConfetti C; C.Actor = MID ? Cast<AActor>(MID->GetOuter()) : nullptr;
		if (AActor* Actor = C.Actor.Get()) Actor->SetActorHiddenInGame(true);
		Confetti.Add(C);
	}
}

// Poses on a sheet: 0 idle, 1 cheer, 2 wince, 3 shout, 4 fist pump, 5 boo, 6 clap, 7 dance.
void AKSGameMode::CrowdAct(FSpectator& S, bool bHappy, float Seconds)
{
	const float Now = GetWorld()->GetTimeSeconds(), Roll = FMath::FRand();
	S.AltPose = -1;
	if (bHappy)
	{
		if (S.Build == EKSBuild::Flyer && Roll < 0.5f) { S.Pose = 1; S.Move = ECrowdMove::Loop; }          // loops the loop
		else if (Roll < 0.20f) { S.Pose = 1; S.Move = ECrowdMove::Jump; }                                   // jumping, arms up
		else if (Roll < 0.33f) { S.Pose = 4; S.Move = ECrowdMove::Jump; }                                   // jumping, fist up
		else if (Roll < 0.46f) { S.Pose = 1; S.AltPose = 4; S.Move = ECrowdMove::Hop; }                     // arms up, fist up, arms up
		else if (Roll < 0.56f) { S.Pose = 3; S.Move = ECrowdMove::Pulse; }                                  // bellowing
		else if (Roll < 0.64f) { S.Pose = 1; S.Move = ECrowdMove::Sway; }                                   // arms up, swaying
		else if (Roll < 0.80f) { S.Pose = 6; S.Move = ECrowdMove::Clap; }                                   // applauds
		else if (Roll < 0.90f) { S.Pose = 7; S.AltPose = 4; S.Move = ECrowdMove::Dance; }                   // his victory dance
		else { S.Pose = 7; S.AltPose = 1; S.Move = ECrowdMove::Dance; }                                     // loses it completely
	}
	else
	{
		if (Roll < 0.35f) { S.Pose = 2; S.Move = ECrowdMove::Shake; }                                       // shaking, hands on cheeks
		else if (Roll < 0.50f) { S.Pose = 2; S.Move = ECrowdMove::Cower; }                                  // sinks down and cannot look
		else if (Roll < 0.72f) { S.Pose = 5; S.Move = ECrowdMove::Sway; }                                   // thumbs down
		else if (Roll < 0.88f) { S.Pose = 5; S.AltPose = 3; S.Move = ECrowdMove::Pulse; }                   // boos and yells by turns
		else { S.Pose = 2; S.AltPose = 5; S.Move = ECrowdMove::Shake; }
	}
	S.BusyUntil = Now + Seconds;
	// A beast that is pleased lets the whole hall know.
	if (bHappy && S.bBeast && Phase == EKSPhase::Fight && Now >= NextBeastAt && FMath::FRand() < 0.35f) { Sfx(EKSSfx::Beast, 0.4f); NextBeastAt = Now + FMath::FRandRange(7.f, 16.f); }
}

void AKSGameMode::CrowdReact(bool bCheer, float Strength)
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (FSpectator& S : Spectators)
	{
		if (FMath::FRand() > Strength) continue;              // a light hit moves only part of the crowd
		S.CueAt = Now + FMath::FRandRange(0.03f, 0.55f);      // nobody reacts in the same instant
		S.CueSeconds = Strength >= 1.f ? FMath::FRandRange(3.5f, 7.f) : FMath::FRandRange(0.6f, 2.0f);   // a knockout is celebrated, or mourned, for a good while
		S.bCueCheer = bCheer != S.bRival;                     // rivals wince when the player scores and cheer when he is hit
	}
	CrowdFlashes(2.f + 10.f * Strength);
	// The hall is heard as well as seen, but not on every single jab.
	if (Spectators.Num() && Now >= NextCrowdVoiceAt && (Strength >= 0.5f || FMath::FRand() < 0.5f))
	{
		Sfx(bCheer ? EKSSfx::Cheer : (FMath::FRand() < 0.75f ? EKSSfx::Gasp : EKSSfx::Boo), FMath::Lerp(0.22f, 0.55f, Strength));
		NextCrowdVoiceAt = Now + FMath::FRandRange(0.9f, 1.6f);
	}
}

void AKSGameMode::TickCrowd()
{
	if (!GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	// Now and then a wave goes round the stands: everyone throws his arms up as it passes him.
	if (Phase == EKSPhase::Fight && Spectators.Num() && Now >= NextWaveAt)
	{
		const float Dir = FMath::RandBool() ? 1.f : -1.f, Turn = FMath::FRandRange(1.6f, 2.6f);
		for (FSpectator& S : Spectators)
		{
			const float Angle = FMath::Atan2(S.Foot.Y, S.Foot.X - 75.f) / PI;      // -1..1 round the ring
			S.WaveAt = Now + 0.2f + (Dir * Angle * 0.5f + 0.5f) * Turn;
		}
		NextWaveAt = Now + FMath::FRandRange(22.f, 45.f);
	}
	const float CrowdDt = GetWorld()->GetDeltaSeconds();
	const bool bHaveCam = PC && PC->PlayerCameraManager;
	const FVector CamAt = bHaveCam ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	for (FSpectator& S : Spectators)
	{
		AActor* Actor = S.Actor.Get();
		if (!Actor || !S.MID) continue;
		if (S.CueAt > 0.f && Now >= S.CueAt) { S.CueAt = 0.f; S.WaveAt = 0.f; CrowdAct(S, S.bCueCheer, S.CueSeconds); }
		else if (S.WaveAt > 0.f && Now >= S.WaveAt)
		{
			S.WaveAt = 0.f;
			if (S.CueAt <= 0.f) { S.Pose = 1; S.AltPose = -1; S.Move = ECrowdMove::Cower; S.BusyUntil = Now + 0.5f; }   // Cower with arms up: up on his toes, see below
		}
		else if (S.BusyUntil > 0.f && Now >= S.BusyUntil) { S.Pose = 0; S.AltPose = -1; S.Move = ECrowdMove::Idle; S.BusyUntil = 0.f; S.NextWhim = Now + FMath::FRandRange(1.5f, 8.f); }
		else if (S.BusyUntil <= 0.f && S.CueAt <= 0.f && Now >= S.NextWhim)
		{
			// Nobody stands still for a whole fight: now and then each one does something of his own.
			const float Roll = FMath::FRand();
			if (Roll < 0.24f) CrowdAct(S, true, FMath::FRandRange(0.4f, 1.3f));                              // a cheer out of nowhere
			else if (Roll < 0.34f) CrowdAct(S, false, FMath::FRandRange(0.3f, 0.9f));                        // a gasp or a jeer
			else if (Roll < 0.48f)                                                                            // yells something at the ring
			{
				S.Pose = 3; S.AltPose = 0; S.Move = ECrowdMove::Pulse; S.BusyUntil = Now + FMath::FRandRange(0.7f, 1.4f);
				if (Phase == EKSPhase::Fight && Now >= NextShoutAt) { Sfx(EKSSfx::Shout, FMath::FRandRange(0.28f, 0.45f)); NextShoutAt = Now + FMath::FRandRange(2.5f, 6.f); }
			}
			else if (Roll < 0.54f) { S.Pose = 7; S.AltPose = 0; S.Move = ECrowdMove::Dance; S.BusyUntil = Now + FMath::FRandRange(1.5f, 4.f); }   // a little dance on the spot
			else if (Roll < 0.60f) { S.Pose = 6; S.AltPose = -1; S.Move = ECrowdMove::Clap; S.BusyUntil = Now + FMath::FRandRange(1.f, 2.5f); }     // claps along
			else if (Roll < 0.66f) { S.Pose = 0; S.AltPose = -1; S.Move = ECrowdMove::Hop; S.BusyUntil = Now + FMath::FRandRange(0.3f, 0.7f); }    // hops to see better
			else if (Roll < 0.80f) { S.IdleStyle = FMath::RandRange(0, 3); S.NextWhim = Now + FMath::FRandRange(2.f, 7.f); }       // shifts his weight
			else { S.bFlip = !S.bFlip; S.NextWhim = Now + FMath::FRandRange(1.f, 6.f); }                     // turns to look the other way
		}
		// The pose is a whole picture and changes at once; everything else (jumping, leaning, breathing) moves smoothly.
		const float T = Now + S.Phase, Turn = 2.f * PI;
		auto Beat = [&](float Rate) { return FMath::FloorToInt(T * Rate) % 2 != 0; };
		int32 Frame = S.Pose;
		bool bFlip = S.bFlip;
		const float Breath = 0.012f * FMath::Sin(T * S.BobRate * Turn);
		float Lift = 0.f, Side = 0.f, Tilt = 0.f, High = 1.f + Breath, Wide = 1.f - Breath * 0.5f;
		// One jump: up and down for most of the beat, then a short squat on landing.
		auto Jump = [&](float HeightScale)
		{
			const float U = FMath::Frac(T * S.JumpRate);
			if (U < 0.7f) { const float V = U / 0.7f; Lift = S.JumpHeight * HeightScale * 4.f * V * (1.f - V); High += 0.035f; Wide -= 0.025f; }
			else { const float Squat = FMath::Sin((U - 0.7f) / 0.3f * PI); High -= 0.07f * Squat; Wide += 0.05f * Squat; }
		};
		switch (S.Move)
		{
		case ECrowdMove::Idle:
			if (S.IdleStyle == 0) { bFlip = S.bFlip != Beat(S.IdleRate); Tilt = S.SwayAngle * 0.25f * FMath::Sin(T * S.SwayRate * 0.4f * Turn); }
			else if (S.IdleStyle == 1) { Tilt = S.SwayAngle * 0.5f * FMath::Sin(T * S.SwayRate * 0.5f * Turn); Side = Tilt * 0.004f * S.Height; }
			else if (S.IdleStyle == 2) Lift = S.BobHeight * FMath::Abs(FMath::Sin(T * S.JumpRate * 0.5f * Turn));
			break;
		case ECrowdMove::Jump:  Jump(1.f); break;
		case ECrowdMove::Hop:   Jump(0.55f); if (S.AltPose >= 0 && Beat(S.JumpRate)) Frame = S.AltPose; break;
		case ECrowdMove::Shake: Side = 0.016f * S.Height * FMath::Sin(T * 44.f); Tilt = 1.5f * FMath::Sin(T * 31.f); if (S.AltPose >= 0 && Beat(1.2f)) Frame = S.AltPose; break;
		case ECrowdMove::Sway:  Tilt = S.SwayAngle * FMath::Sin(T * S.SwayRate * Turn); Side = Tilt * 0.005f * S.Height; break;
		case ECrowdMove::Pulse: { const float P = FMath::Sin(T * S.SwayRate * 1.6f * Turn); High += 0.045f * P; Wide -= 0.03f * P; if (S.AltPose >= 0 && P > 0.f) Frame = S.AltPose; break; }
		case ECrowdMove::Cower: if (S.Pose == 1) { Lift = S.JumpHeight * 0.5f; High += 0.07f; } else { High -= 0.13f; Wide += 0.05f; } break;
		case ECrowdMove::Dance:
		{
			const float Swing = FMath::Sin(T * S.JumpRate * PI);
			bFlip = Beat(S.JumpRate); Lift = S.JumpHeight * 0.35f * FMath::Abs(Swing); Tilt = 6.f * Swing; Side = 0.03f * S.Height * FMath::Sin(T * S.JumpRate * PI * 0.5f);
			if (S.AltPose >= 0 && FMath::FloorToInt(T * S.JumpRate * 0.5f) % 2 != 0) Frame = S.AltPose;
			break;
		}
		case ECrowdMove::Clap:  { const float P = FMath::Sin(T * 3.6f * Turn); High += 0.014f * P; Lift = S.BobHeight * 0.5f * FMath::Abs(FMath::Sin(T * 1.8f * Turn)); Tilt = 1.5f * FMath::Sin(T * 0.9f * Turn); break; }
		case ECrowdMove::Loop:  Lift = FMath::Sin(Now * 7.f + S.Phase) * 45.f; Side = FMath::Cos(Now * 7.f + S.Phase) * 45.f; Tilt = FMath::Cos(Now * 7.f + S.Phase) * 20.f; break;
		}
		if (S.Build == EKSBuild::Flyer)
		{
			// The ones in the air never touch down: they bob and wander above the stands.
			Lift += FMath::Sin(Now * S.HoverRate + S.Phase) * S.HoverHeight;
			Side += FMath::Sin(Now * S.DriftRate + S.Phase * 2.f) * S.DriftWidth;
		}
		// Going from one move to the next is eased too, so nobody snaps from a lean into a jump.
		S.Lift = FMath::FInterpTo(S.Lift, Lift, CrowdDt, 16.f); S.Side = FMath::FInterpTo(S.Side, Side, CrowdDt, 16.f); S.Tilt = FMath::FInterpTo(S.Tilt, Tilt, CrowdDt, 16.f);
		S.High = FMath::FInterpTo(S.High, High, CrowdDt, 16.f); S.Wide = FMath::FInterpTo(S.Wide, Wide, CrowdDt, 16.f);
		CrowdMovesSeen |= 1 << (int32)S.Move; CrowdPosesSeen |= 1 << Frame;
		if (Frame != S.Shown || bFlip != S.bShownFlip)
		{
			S.Shown = Frame; S.bShownFlip = bFlip;
			// Flipped: the picture is read right to left, so the cell starts at its right-hand edge.
			S.MID->SetVectorParameterValue(TEXT("UV"), FLinearColor(bFlip ? -0.25f : 0.25f, 0.5f, 0.f, 0.f));
			S.MID->SetVectorParameterValue(TEXT("Offset"), FLinearColor(0.25f * (Frame % 4 + (bFlip ? 1 : 0)), 0.5f * (Frame / 4), 0.f, 0.f));
		}
		// A flat picture seen from the side is a line, and the cameraman now walks all round the ring: each picture
		// turns on its feet to face the camera (the flyers and the back rows as well).
		FVector Across = S.Across;
		if (bHaveCam)
		{
			const FVector ToCam = (CamAt - S.Foot).GetSafeNormal2D();
			if (!ToCam.IsNearlyZero()) Across = FVector(-ToCam.Y, ToCam.X, 0.f);
		}
		// Leaning turns the picture about its feet, not its middle.
		const float Rad = FMath::DegreesToRadians(S.Tilt);
		const FVector Up = FVector::UpVector * FMath::Cos(Rad) + Across * FMath::Sin(Rad), Along = Across * FMath::Cos(Rad) - FVector::UpVector * FMath::Sin(Rad);
		Actor->SetActorTransform(FTransform(FRotationMatrix::MakeFromXY(Along, -Up).Rotator(),
			S.Foot + Across * S.Side + FVector(0.f, 0.f, S.Lift) + Up * (S.Height * S.High * 0.5f), FVector(S.Width * S.Wide / 100.f, S.Height * S.High / 100.f, 1.f)));
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
			const FVector ToRing = (FVector(75.f, 0.f, 0.f) - FVector(S.Foot.X, S.Foot.Y, 0.f)).GetSafeNormal();
			const FVector At = S.Foot + ToRing * 20.f + FVector(FMath::FRandRange(-25.f, 25.f), FMath::FRandRange(-25.f, 25.f), S.Height * FMath::FRandRange(0.65f, 0.9f));
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
	TickArenaFx();
}

void AKSGameMode::ConfettiBurst()
{
	for (FConfetti& C : Confetti)
	{
		AActor* Actor = C.Actor.Get();
		if (!Actor) continue;
		// Dropped from the lighting rig over the whole ring, a few at a time for a couple of seconds.
		C.Pos = FVector(FMath::FRandRange(-330.f, 420.f), FMath::FRandRange(-380.f, 380.f), FMath::FRandRange(600.f, 1100.f));
		C.Vel = FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-60.f, 0.f));
		C.Spin = FRotator(FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f), FMath::FRandRange(-400.f, 400.f));
		C.Phase = FMath::FRandRange(0.f, 6.f);
		C.bLive = true;
		Actor->SetActorHiddenInGame(false);
	}
	ArenaShowUntil = GetWorld()->GetTimeSeconds() + 6.f;
	UE_LOG(LogTemp, Display, TEXT("KSTEST arena confetti=%d"), Confetti.Num());
}

// The light show: stage lamps sweep the stands, lasers fan over the hall, and both go wild for a few seconds after a knockout.
void AKSGameMode::TickArenaFx()
{
	const float Now = GetWorld()->GetTimeSeconds(), Dt = GetWorld()->GetDeltaSeconds();
	const bool bShow = Now < ArenaShowUntil;
	int32 Moving = 0;
	for (int32 i = 0; i < Arena.Beams.Num(); i++)
	{
		const FKSArena::FBeam& Beam = Arena.Beams[i];
		ASpotLight* Lamp = Beam.Lamp.Get();
		if (!Lamp) continue;
		const float Rate = (0.35f + 0.07f * i) * (bShow ? 3.5f : 1.f), T = Now * Rate + i * 1.9f;
		const FVector Aim = Beam.Aim + FVector(FMath::Sin(T * 0.7f) * 160.f, FMath::Sin(T) * 330.f, FMath::Cos(T * 1.3f) * 70.f);
		Lamp->SpotLightComponent->SetWorldRotation((Aim - Lamp->GetActorLocation()).Rotation());
		KSWorld::AimBeam(Beam.Cone, Lamp->GetActorLocation(), Aim, Beam.Length, Beam.Radius);
		// During the show the lamps flicker on and off by turns.
		const float Level = bShow ? (FMath::Sin(Now * 17.f + i * 2.4f) > -0.2f ? 1.6f : 0.15f) : 1.f;
		Lamp->SpotLightComponent->SetIntensity(Beam.Candelas * 0.22f * Level);
		if (Beam.Cone) Beam.Cone->SetScalarParameterValue(TEXT("Glow"), Beam.Glow * Level);
		Moving++;
	}
	for (int32 i = 0; i < Arena.Lasers.Num(); i++)
	{
		AActor* Laser = Arena.Lasers[i].Get();
		if (!Laser) continue;
		const float Rate = bShow ? 2.6f : 0.45f;
		const float Yaw = (i - 3.5f) * 15.f + FMath::Sin(Now * Rate + i * 0.8f) * 18.f, Pitch = 48.f + FMath::Sin(Now * Rate * 0.73f + i * 1.7f) * 16.f;
		const FRotator Rot(Pitch, Yaw, 0.f);
		Laser->SetActorLocationAndRotation(Arena.LaserFrom + Rot.Vector() * FKSArena::LaserLength * 0.5f, Rot);
		Moving++;
	}
	if (Moving && !bArenaFxLogged) { bArenaFxLogged = true; int32 Cones = 0; for (const FKSArena::FBeam& Beam : Arena.Beams) Cones += Beam.Cone ? 1 : 0;
		UE_LOG(LogTemp, Display, TEXT("KSTEST arena beams=%d lasers=%d cones=%d"), Arena.Beams.Num(), Arena.Lasers.Num(), Cones);
	}
	for (FConfetti& C : Confetti)
	{
		AActor* Actor = C.Actor.Get();
		if (!C.bLive || !Actor) continue;
		// Paper falls slowly and slides from side to side as it turns.
		C.Vel.Z = FMath::Max(C.Vel.Z - 260.f * Dt, -130.f);
		C.Pos += (C.Vel + FVector(FMath::Sin(Now * 3.f + C.Phase) * 70.f, FMath::Cos(Now * 2.3f + C.Phase) * 70.f, 0.f)) * Dt;
		if (C.Pos.Z < 3.f) { C.bLive = false; C.Pos.Z = 3.f; Actor->SetActorLocationAndRotation(C.Pos, FRotator(0.f, C.Phase * 60.f, 0.f)); continue; }   // lies where it landed
		Actor->SetActorLocationAndRotation(C.Pos, Actor->GetActorRotation() + C.Spin * Dt);
	}
	TickMotes(Now, Dt);
	// The place's own light show: lightning lights the painted wall and the hall in two strokes; lava breathes.
	const EKSPulse Pulse = ArenaIndex >= 0 ? KSArenas[ArenaIndex].Pulse : EKSPulse::None;
	if (Pulse != EKSPulse::None)
	{
		float Wall = 1.f, Candelas = 0.f;
		if (Pulse == EKSPulse::Lightning)
		{
			if (Now >= NextBoltAt) { BoltUntil = Now + 0.34f; NextBoltAt = Now + FMath::FRandRange(5.f, 13.f); }
			const float Left = BoltUntil - Now;
			const float Level = Left <= 0.f ? 0.f : Left > 0.26f ? 1.f : Left > 0.18f ? 0.f : Left / 0.18f;
			Wall = 1.f + 1.6f * Level; Candelas = 90000.f * Level;
		}
		else
		{
			const float Level = 0.5f + 0.35f * FMath::Sin(Now * 1.4f) + 0.15f * FMath::Sin(Now * 5.3f);
			Wall = 0.85f + 0.35f * Level; Candelas = 2200.f + 2600.f * Level;
		}
		for (UMaterialInstanceDynamic* Panel : Arena.Wall) if (Panel) Panel->SetScalarParameterValue(TEXT("Glow"), Arena.WallGlow * Wall);
		for (const TWeakObjectPtr<APointLight>& Light : Arena.PulseLights) if (APointLight* L = Light.Get()) L->PointLightComponent->SetIntensity(Candelas * 0.22f);
	}
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
		else if (bSpecialOn && !bSpecialByPlayer) Key = TEXT("special");
		else if (Clock < OppHurtUntil) Key = TEXT("hurt");
		else if (OppState == EKSOppState::Windup) Key = FString::Printf(TEXT("wind %.3f"), OppT);
		else if (OppState == EKSOppState::Strike) Key = TEXT("strike");
		else if (OppState == EKSOppState::Open) Key = Clock < OppStaggerUntil ? TEXT("stagger") : TEXT("open");
		else Key = (Clock < OppGuardUntil || (bOnline && RemoteDefense == EKSDefense::Block)) ? FString(TEXT("block")) : FString::Printf(TEXT("idle %d"), OppIdleCount);
		// Online, the other player's leans and ducks are shown on his fighter (his own left is his -Y, as for the player's).
		if (bOnline)
		{
			const bool bFree = Phase == EKSPhase::Fight && OppState != EKSOppState::KO && OppState != EKSOppState::Open;
			OppDummy->SetBodyShift(!bFree ? 0.f : RemoteDefense == EKSDefense::EvadeLeft ? -42.f : RemoteDefense == EKSDefense::EvadeRight ? 42.f : 0.f,
				bFree && RemoteDefense == EKSDefense::Duck ? 0.68f : 1.f);
		}

		// A punch that connected is allowed to pull back before he returns to his stance.
		if (OppClipKey == TEXT("strike") && Key.StartsWith(TEXT("idle")))
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
			else if (Key == TEXT("win")) OppDummy->PlayClip(OppDummy->HasClip(TEXT("Win")) ? TEXT("Win") : TEXT("victory"), true);
			else if (Key == TEXT("hurt")) OppDummy->PlayClip(TEXT("Face_Punch_Reaction"), false, 1.8f, 0.15f);
			else if (Key == TEXT("special")) {}   // StartSpecial has put the clip on
			else if (Key == TEXT("stagger"))
			{
				// His punch met a guard: he is thrown back off it, squeezed into the time he reels for.
				const TCHAR* Clip = OppDummy->HasClip(TEXT("Stagger")) ? TEXT("Stagger") : TEXT("Hit_Reaction_to_Waist");
				OppDummy->PlayClip(Clip, false, FMath::Clamp(OppDummy->ClipSeconds(Clip) / 0.8f, 1.f, 3.f));
			}
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
			else
			{
				// Between attacks he does not stand like everybody else: one of his own two moves, picked up somewhere
				// in the middle, and every third time the plain stance. PlayClip eases into it from wherever he is.
				const TCHAR* Own = OppIdleCount % 3 == 0 ? TEXT("Fun_A") : OppIdleCount % 3 == 2 ? TEXT("Fun_B") : TEXT("Combat_Stance");
				if (!OppDummy->HasClip(Own)) Own = TEXT("Combat_Stance");
				OppDummy->PlayClip(Own, true, 1.f, Own[0] == 'F' ? FMath::FRandRange(0.f, FMath::Max(0.f, OppDummy->ClipSeconds(Own) - 1.5f)) : 0.f);
			}
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
			if (Clock - PunchStart[i] < PlayerPunchSeconds + OnlinePunchLead(i) && (PunchArm < 0 || PunchStart[i] > PunchStart[PunchArm])) PunchArm = i;
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
		else if (bSpecialOn && bSpecialByPlayer) Key = TEXT("special");
		else if (Clock < PlayerHurtUntil && !bMirror) Key = TEXT("hurt");
		else if (PunchArm >= 0 && !bMirror) Key = FString::Printf(TEXT("punch %d %.3f"), PunchArm, PunchStart[PunchArm]);
		else if (D == EKSDefense::Block && !bMirror) Key = TEXT("block");
		// Left alone for a moment in the stance, the player's fighter starts doing his own thing too.
		else if (PlayerClipKey.StartsWith(TEXT("idle")) && Clock - PlayerIdleSince > 1.3f && Phase == EKSPhase::Fight && !bMirror) Key = FString::Printf(TEXT("idle own %d"), PlayerIdleCount);
		else Key = TEXT("idle");
		if (Key == TEXT("idle") && !PlayerClipKey.StartsWith(TEXT("idle"))) { PlayerIdleSince = Clock; PlayerIdleCount++; }

		if (Key != PlayerClipKey)
		{
			PlayerClipKey = Key;
			if (Key == TEXT("ko")) PlayerDummy->PlayClip(TEXT("Knock_Down"), false);
			else if (Key == TEXT("win")) PlayerDummy->PlayClip(PlayerDummy->HasClip(TEXT("Win")) ? TEXT("Win") : TEXT("victory"), true);
			else if (Key == TEXT("hurt")) PlayerDummy->PlayClip(TEXT("Face_Punch_Reaction"), false, 1.8f, 0.15f);
			else if (Key == TEXT("special")) {}   // StartSpecial has put the clip on
			else if (PunchArm >= 0 && Key.StartsWith(TEXT("punch")))
			{
				// The hit lands 0.09 s after the input, so the clip starts just before its impact frame.
				const FPunchClip& Punch = PunchClips[(int32)PunchType[PunchArm]][PunchArm];
				// Online the punch is in the air for a while (the other player must be able to answer it), so the clip is
				// played from its start and stretched to have the fist out when the punch arrives over there.
				const float Lead = OnlinePunchLead(PunchArm);
				if (Lead > 0.f) PlayerDummy->PlayClip(Punch.Name, false, Punch.Impact / (Lead + 0.05f));
				else PlayerDummy->PlayClip(Punch.Name, false, 1.25f, FMath::Max(0.f, Punch.Impact - 0.11f));
			}
			else if (Key == TEXT("block")) PlayerDummy->PlayClip(TEXT("Block1"), true, 1.f, 0.6f);
			else if (Key.StartsWith(TEXT("idle own")))
			{
				const TCHAR* Own = PlayerIdleCount % 2 ? TEXT("Fun_A") : TEXT("Fun_B");
				if (PlayerDummy->HasClip(Own)) PlayerDummy->PlayClip(Own, true, 1.f, FMath::FRandRange(0.f, FMath::Max(0.f, PlayerDummy->ClipSeconds(Own) - 1.5f)));
			}
			else PlayerDummy->PlayClip(TEXT("Combat_Stance"), true);
		}
		// Aim the punch at the opponent's face when he is open, or at his guard when he is not.
		if (PunchArm >= 0 && Clock - PunchStart[PunchArm] < 0.24f + OnlinePunchLead(PunchArm) && !bLost && OppState != EKSOppState::KO)
		{
			const FVector Away = (PlayerDummy->GetActorLocation() - OppDummy->GetActorLocation()).GetSafeNormal2D();
			const bool bOpen = OppState == EKSOppState::Open || OppState == EKSOppState::KO || Clock < OppHurtUntil || (bOnline && RemoteDefense != EKSDefense::Block);
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
	for (int32 i = LaterVoices.Num() - 1; i >= 0; i--)
	{
		if (Clock < LaterVoices[i].When) continue;
		const FLaterVoice Due = LaterVoices[i]; LaterVoices.RemoveAt(i);
		const float Seconds = Due.Who.IsValid() ? Speak(Due.Who.Get(), Due.Prefix, true) : 0.f;
		if (Due.Then.IsValid()) { FLaterVoice Next; Next.Who = Due.Then; Next.Prefix = Due.ThenPrefix; Next.When = Clock + Seconds + 0.35f; LaterVoices.Add(Next); }
	}
	// The subtitle: whoever is speaking.
	AKSDummy* Talker = OppDummy && OppDummy->IsTalking() ? OppDummy.Get() : PlayerDummy && PlayerDummy->IsTalking() ? PlayerDummy.Get() : nullptr;
	SpokenLine = Talker ? Talker->SpokenText() : FString();
	if (Talker) { SpokenBy = Talker == OppDummy ? FString(Fighter().Name) : FString(PlayerFighter().Name); bSpokenByOpp = Talker == OppDummy; }
	// Where the heads are, for the speech bubbles and the damage badges.
	if (OppDummy) { OppHead = OppDummy->FaceWorld(false); OppDummy->BodyPoints(OppBody); } else OppBody.Reset();
	if (PlayerDummy) { PlayerHead = PlayerDummy->FaceWorld(false); PlayerDummy->BodyPoints(PlayerBody); } else PlayerBody.Reset();
}

/** One voice at a time. A taunt is dropped if somebody is still speaking or has only just stopped; bMust (the
 *  introduction, the last word after a knockout) cuts in. Returns the length of the line in seconds. */
float AKSGameMode::Speak(AKSDummy* Who, const TCHAR* Prefix, bool bMust)
{
	if (!Who || !Who->HasCharacter()) return 0.f;
	AKSDummy* Other = Who == OppDummy ? PlayerDummy.Get() : OppDummy.Get();
	if (!bMust && (Clock < VoiceQuietUntil || Clock < AnnouncerUntil || Who->IsTalking() || (Other && Other->IsTalking()))) return 0.f;
	if (Other) Other->StopTalking();
	const float Seconds = Who->Say(Prefix, Who == OppDummy ? 1.5f : 1.3f);   // voices are the loudest thing in the bout
	if (Seconds > 0.f) VoiceQuietUntil = Clock + Seconds + 2.2f;
	return Seconds;
}

/* ================= special abilities ================= */

FString AKSGameMode::PlayerCry() const { return Specials[PlayerChar % UE_ARRAY_COUNT(Specials)].Cry; }

void AKSGameMode::FxAdd(UStaticMesh* Mesh, const FVector& Pos, const FVector& Vel, const FVector& Size0, const FVector& Size1, const FLinearColor& Color, float Glow, float Life,
	float Gravity, const FRotator& Rot, const FRotator& Spin, float Delay)
{
	if (Fx.Num() > 600) return;
	FKSBuilder B = MakeBuilder();
	UMaterialInstanceDynamic* MID = B.Shape(Mesh, Pos, Size0, Color, Glow, Rot);
	AActor* Actor = MID ? Cast<AActor>(MID->GetOuter()) : nullptr;
	if (!Actor) return;
	if (Delay > 0.f) Actor->SetActorHiddenInGame(true);
	FFx F; F.Actor = Actor; F.Pos = Pos; F.Vel = Vel; F.Size0 = Size0; F.Size1 = Size1; F.Rot = Rot; F.Spin = Spin; F.Life = Life; F.Gravity = Gravity; F.Delay = Delay;
	Fx.Add(F); FxMade++;
}

void AKSGameMode::FxBeam(const FVector& From, const FVector& To, float Thick0, float Thick1, const FLinearColor& Color, float Glow, float Life, float Delay)
{
	// The engine cylinder stands along Z: laid from From to To.
	const FVector Along = To - From;
	const float Length = Along.Size();
	if (Length < 1.f) return;
	FxAdd(CylinderMesh, (From + To) * 0.5f, FVector::ZeroVector, FVector(Thick0, Thick0, Length), FVector(Thick1, Thick1, Length), Color, Glow, Life, 0.f,
		FRotationMatrix::MakeFromZ(Along / Length).Rotator(), FRotator::ZeroRotator, Delay);
}

void AKSGameMode::FxBurst(const FVector& At, int32 Count, float Speed, float Size, const FLinearColor& Color, float Glow, float Life, float Gravity)
{
	for (int32 i = 0; i < Count; i++)
	{
		const float Shade = FMath::FRandRange(0.6f, 1.f);
		FxAdd(i % 2 ? CubeMesh : SphereMesh, At, FMath::VRand() * Speed * FMath::FRandRange(0.4f, 1.f) + FVector(0, 0, Speed * 0.3f), FVector(Size * FMath::FRandRange(0.6f, 1.2f)), FVector(1.f),
			FLinearColor(Color.R * Shade, Color.G * Shade, Color.B * Shade), Glow, Life * FMath::FRandRange(0.6f, 1.f), Gravity, FRotator(FMath::FRandRange(0.f, 360.f)), FRotator(400.f, 300.f, 200.f));
	}
}

void AKSGameMode::TickFx(float Dt)
{
	for (int32 i = Fx.Num() - 1; i >= 0; i--)
	{
		FFx& F = Fx[i];
		AActor* Actor = F.Actor.Get();
		if (Actor && F.Delay > 0.f)
		{
			F.Delay -= Dt;
			if (F.Delay <= 0.f) Actor->SetActorHiddenInGame(false);
			continue;
		}
		F.Age += Dt;
		if (!Actor || F.Age >= F.Life)
		{
			if (Actor) { WorldActors.RemoveSingleSwap(Actor); Actor->Destroy(); }
			Fx.RemoveAtSwap(i);
			continue;
		}
		const float U = F.Age / F.Life;
		F.Vel.Z -= F.Gravity * Dt; F.Pos += F.Vel * Dt; F.Rot += F.Spin * Dt;
		Actor->SetActorLocationAndRotation(F.Pos, F.Rot);
		Actor->SetActorScale3D(FMath::Lerp(F.Size0, F.Size1, U) / 100.f);
	}
}

void AKSGameMode::StartSpecial(bool bByPlayer)
{
	AKSDummy* Who = bByPlayer ? PlayerDummy.Get() : OppDummy.Get();
	if (!Who || !Ladder.IsValidIndex(FighterIndex)) return;
	SpecialIndex = (bByPlayer ? PlayerChar : Ladder[FighterIndex]) % UE_ARRAY_COUNT(Specials);
	const FSpecialDef& Def = Specials[SpecialIndex];
	// His own move, fitted into two or three seconds; a fighter without one pounds his chest.
	const bool bOwn = Who->HasClip(TEXT("Special"));
	const TCHAR* Clip = bOwn ? TEXT("Special") : TEXT("Chest_Pound_Taunt");
	const float Length = FMath::Max(0.5f, Who->ClipSeconds(Clip)), Seconds = FMath::Clamp(Length, 1.5f, 2.8f);
	bSpecialOn = true; bSpecialByPlayer = bByPlayer; bSpecialLaunched = bSpecialHit = false;
	SpecialStart = Clock; SpecialImpact = Clock + Seconds * Def.ImpactFrac; SpecialEnd = Clock + Seconds;
	(bByPlayer ? PlayerMeter : OppMeter) = 0.f;
	if (bByPlayer) bMeterFullSaid = false;
	Who->StopAim();
	Who->PlayClip(Clip, false, Length / Seconds);
	(bByPlayer ? PlayerClipKey : OppClipKey) = TEXT("special");
	// Everything else waits: no punches in the air, and the opponent holds still until it is over.
	Pending.Reset(); PunchStart[0] = PunchStart[1] = -10.f;
	OppState = EKSOppState::Idle; OppT = Clock; OppUntil = SpecialEnd + 0.5f; OppWaitUntil = OppUntil + 1.5f;
	if (bByPlayer) { CueMain.Reset(); CueHint.Reset(); }
	else { Cue(TEXT("SPECIAL!"), TEXT("duck or lean away - a block only halves it")); Sfx(EKSSfx::Tell); }
	Announce(Def.Cry, 1.2f);
	Sfx(EKSSfx::Slam, 0.8f); Sfx(EKSSfx::Roar, 0.35f);
	Speak(Who, TEXT("hit"), true);
	CrowdReact(bByPlayer, 1.f); CrowdFlashes(8.f);
	if (bHitShots) BurstShots.Add({ Clock + 0.75f, FString::Printf(TEXT("special_%02d_hero.png"), HitShotIndex) });
	UE_LOG(LogTemp, Display, TEXT("KSTEST special by=%s fighter=%s own_clip=%d seconds=%.1f"), bByPlayer ? TEXT("player") : TEXT("opponent"), Roster()[SpecialIndex].Asset, bOwn ? 1 : 0, Seconds);
}

void AKSGameMode::TickSpecial(float Dt)
{
	AKSDummy* Who = bSpecialByPlayer ? PlayerDummy.Get() : OppDummy.Get();
	AKSDummy* Whom = bSpecialByPlayer ? OppDummy.Get() : PlayerDummy.Get();
	if (!Who || !Whom) { bSpecialOn = false; return; }
	const FSpecialDef& Def = Specials[SpecialIndex];
	const FVector From = Who->FaceWorld(false) - FVector(0, 0, 22.f), To = Whom->FaceWorld(false) - FVector(0, 0, 18.f);
	if (Clock < SpecialImpact - 0.25f)
	{
		// Charging up: sparks drawn in from all round him, more of them as it builds.
		const float Build = FMath::Clamp((Clock - SpecialStart) / FMath::Max(0.1f, SpecialImpact - SpecialStart), 0.f, 1.f);
		for (int32 i = 0; i < (FMath::FRand() < 0.4f + Build ? 2 : 1); i++)
		{
			const FVector Off = FMath::VRand() * FMath::FRandRange(70.f, 120.f);
			FxAdd(SphereMesh, From + Off, -Off / 0.3f, FVector(7.f + 6.f * Build), FVector(2.f), Def.Color, 5.f, 0.3f);
		}
	}
	if (!bSpecialLaunched && Clock >= SpecialImpact - 0.25f) { bSpecialLaunched = true; Sfx(EKSSfx::Whoosh); SpecialLaunch(From, To); }
	if (!bSpecialHit && Clock >= SpecialImpact) { bSpecialHit = true; SpecialLand(From, To); }
	if (bSpecialOn && Clock >= SpecialEnd)
	{
		bSpecialOn = false;
		OppClipKey.Reset(); PlayerClipKey.Reset();
	}
}

void AKSGameMode::SpecialLaunch(const FVector& From, const FVector& To)
{
	const FSpecialDef& Def = Specials[SpecialIndex];
	switch (Def.Kind)
	{
	case EFxKind::Blast:
		// A ball of fire thrown across the ring, with a tail.
		FxAdd(SphereMesh, From, (To - From) / 0.25f, FVector(34.f), FVector(60.f), Def.Color, 3.f, 0.25f);
		for (int32 i = 0; i < 10; i++) FxAdd(SphereMesh, From, (To - From) / 0.25f * FMath::FRandRange(0.6f, 0.95f) + FMath::VRand() * 60.f, FVector(18.f), FVector(2.f), FLinearColor(1.f, 0.8f, 0.2f), 5.f, 0.3f, 0.f, FRotator::ZeroRotator, FRotator::ZeroRotator, i * 0.012f);
		break;
	case EFxKind::Cloud:
		// A cloud of hairspray rolling over him.
		for (int32 i = 0; i < 44; i++)
		{
			const float Shade = FMath::FRandRange(0.5f, 1.f);
			FxAdd(SphereMesh, From + FMath::VRand() * 12.f, (To - From) / FMath::FRandRange(0.22f, 0.5f) * 0.9f + FMath::VRand() * 90.f, FVector(14.f), FVector(FMath::FRandRange(50.f, 85.f)),
				FLinearColor(Def.Color.R * Shade, Def.Color.G * Shade, Def.Color.B * Shade), 3.f, FMath::FRandRange(0.7f, 1.2f), -40.f, FRotator::ZeroRotator, FRotator::ZeroRotator, i * 0.006f);
		}
		break;
	case EFxKind::Slam:
		// The canvas breaks in a line from his feet to the other's.
		for (int32 i = 0; i < 9; i++)
		{
			const FVector At = FMath::Lerp(FVector(From.X, From.Y, 0.f), FVector(To.X, To.Y, 0.f), (i + 1) / 9.f) + FVector(0, FMath::FRandRange(-14.f, 14.f), 0);
			FxAdd(CubeMesh, At, FVector(0, 0, 260.f), FVector(26.f, 26.f, 6.f), FVector(20.f, 20.f, 90.f + 12.f * i), Def.Color, 4.f, 0.45f, 900.f, FRotator(0, FMath::FRandRange(0.f, 90.f), 0), FRotator::ZeroRotator, i * 0.026f);
		}
		break;
	case EFxKind::FireRing:
		// A ring of fire thrown outwards from her spin.
		for (int32 i = 0; i < 30; i++)
		{
			const float Angle = i / 30.f * 2.f * PI;
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
			FxAdd(SphereMesh, FVector(From.X, From.Y, 95.f) + Out * 35.f, Out * 620.f + FVector(0, 0, FMath::FRandRange(-30.f, 60.f)), FVector(26.f), FVector(8.f), i % 2 ? Def.Color : FLinearColor(1.f, 0.85f, 0.15f), 6.f, 0.5f);
		}
		break;
	default:
		// Laser, lightning and static are instant: a thin warning line first.
		FxBeam(Def.Kind == EFxKind::Bolt ? To + FVector(0, 0, 620.f) : From + FVector(0, 0, 28.f), To, 3.f, 1.f, Def.Color, 5.f, 0.25f);
		break;
	}
}

void AKSGameMode::SpecialLand(const FVector& From, const FVector& To)
{
	const FSpecialDef& Def = Specials[SpecialIndex];
	switch (Def.Kind)
	{
	case EFxKind::Laser:
	{
		// Six beams, one for every colour of the rainbow, from the horn.
		static const FLinearColor Rainbow[] = { FLinearColor(1, 0.05f, 0.05f), FLinearColor(1, 0.5f, 0), FLinearColor(1, 0.95f, 0.1f), FLinearColor(0.1f, 1, 0.2f), FLinearColor(0.1f, 0.5f, 1), FLinearColor(0.7f, 0.2f, 1) };
		const FVector Horn = From + FVector(0, 0, 52.f);
		for (int32 i = 0; i < 6; i++) FxBeam(Horn + FVector(0, 0, 3.f * (i - 2.5f)), To + FVector(0, 0, 13.f * (i - 2.5f)), 13.f, 2.f, Rainbow[i], 5.f, 0.6f, i * 0.02f);
		break;
	}
	case EFxKind::Bolt:
	{
		// Lightning from the roof: a crooked line, struck twice.
		for (int32 Strike = 0; Strike < 2; Strike++)
		{
			FVector At = To + FVector(FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-60.f, 60.f), 640.f);
			for (int32 i = 0; i < 8; i++)
			{
				const FVector Next = i == 7 ? To : FVector(To.X + FMath::FRandRange(-45.f, 45.f), To.Y + FMath::FRandRange(-45.f, 45.f), FMath::Lerp(At.Z, To.Z, 1.f / (8 - i)));
				FxBeam(At, Next, Strike ? 6.f : 11.f, 1.f, Strike ? FLinearColor::White : Def.Color, 12.f, 0.3f, Strike * 0.13f);
				At = Next;
			}
		}
		WhiteFade = FMath::Max(WhiteFade, 0.6f);
		break;
	}
	case EFxKind::Static:
	{
		// The picture breaks up all round him: snow, and a green line across it.
		FxBeam(From + FVector(0, 0, 28.f), To, 16.f, 1.f, Def.Color, 6.f, 0.4f);
		for (int32 i = 0; i < 70; i++)
		{
			const float Grey = FMath::RandBool() ? 1.f : FMath::FRandRange(0.f, 0.3f);
			FxAdd(CubeMesh, To + FVector(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-75.f, 75.f), FMath::FRandRange(-95.f, 55.f)), FMath::VRand() * 40.f,
				FVector(2.f, FMath::FRandRange(8.f, 34.f), FMath::FRandRange(3.f, 9.f)), FVector(2.f, 4.f, 2.f), i % 6 ? FLinearColor(Grey, Grey, Grey) : Def.Color, 8.f * Grey + 1.f, FMath::FRandRange(0.15f, 0.4f), 0.f,
				FRotator::ZeroRotator, FRotator::ZeroRotator, FMath::FRandRange(0.f, 0.5f));
		}
		break;
	}
	case EFxKind::Cloud:
		// Bats out of the cloud.
		for (int32 i = 0; i < 14; i++) FxAdd(CubeMesh, To, FMath::VRand() * 260.f + FVector(0, 0, 260.f), FVector(20.f, 5.f, 2.f), FVector(12.f, 3.f, 2.f), FLinearColor(0.02f, 0.f, 0.04f), 0.f, 1.1f, 120.f, FRotator(0, FMath::FRandRange(0.f, 360.f), 0), FRotator(0, 200.f, 900.f));
		break;
	case EFxKind::Slam:
		FxBurst(FVector(To.X, To.Y, 8.f), 26, 420.f, 16.f, FLinearColor(0.35f, 0.25f, 0.2f), 0.5f, 0.9f, 900.f);
		break;
	default: break;
	}
	// Whatever it was, it lands with a burst in his colour and one of white.
	FxBurst(To, 26, 520.f, 15.f, Def.Color, 6.f, 0.6f);
	FxBurst(To, 8, 300.f, 20.f, FLinearColor::White, 9.f, 0.25f, 0.f);
	Sfx(EKSSfx::Counter); Sfx(EKSSfx::Hit); Sfx(EKSSfx::KO, 0.5f);
	Shake = FMath::Max(Shake, 14.f); WhiteFade = FMath::Max(WhiteFade, 0.35f);
	CrowdFlashes(10.f);
	UE_LOG(LogTemp, Display, TEXT("KSTEST special landed by=%s pieces=%d"), bSpecialByPlayer ? TEXT("player") : TEXT("opponent"), FxMade);
	if (bHitShots) QueueHitShots(TEXT("special"));
	const float Rest = FMath::Max(0.f, SpecialEnd - Clock);

	if (bSpecialByPlayer)
	{
		OppHP = FMath::Max(0.f, OppHP - SpecialDamage);
		Stats.Landed++; Combo++; Stats.BestCombo = FMath::Max(Stats.BestCombo, Combo);
		Popups.Add({ FString::FromInt(FMath::RoundToInt(SpecialDamage * 10.f)), KSColors::Yellow, FVector2D(0.13f, -0.1f), Clock, 1.1f, true, 1 });
		Popups.Add({ TEXT("KRAK!"), KSColors::Yellow, FVector2D(-0.14f, 0.02f), Clock, 0.9f, false, 3 });
		OppHurtUntil = Clock + 0.45f;
		CamPunch(FVector(0.f, -50.f, 10.f), FMath::RandBool() ? 22.f : -22.f, -5.f);
		if (OppDummy) { OppDummy->Flash(Def.Color, 0.16f); OppDummy->TakeHit(EKSPunch::Uppercut, EKSArm::Right, 2.2f); OppDummy->HitStop(0.16f); }
		if (PlayerDummy) PlayerDummy->HitStop(0.1f);
		CrowdReact(true, 1.f);
		if (OppHP <= 0.f) { EndFight(true, false); return; }
		// He is left dizzy: free punches once the player's fighter has finished.
		OpenUp(1.5f + Rest);
		return;
	}
	const EKSDefense D = Defense();
	if (D == EKSDefense::Duck || D == EKSDefense::EvadeLeft || D == EKSDefense::EvadeRight)
	{
		Stats.Dodges++; PlayerMeter = FMath::Min(1.f, PlayerMeter + 0.25f);
		Sfx(EKSSfx::Dodge); Popup(TEXT("DODGE!"), KSColors::Green, 0.38f, 0.45f, true);
		CrowdReact(true, 0.8f);
		OpenUp(1.6f + Rest);
		return;
	}
	Attack.Type = EKSPunch::Uppercut; Attack.Side = EKSArm::Right;
	PlayerHurt(D == EKSDefense::Block ? OppSpecialDamage * 0.5f : OppSpecialDamage, D == EKSDefense::Block);
	if (PlayerHP > 0.f) OppIdle(1.0f + Rest);
}

/* ================= fight rules ================= */


void AKSGameMode::OppIdle(float Seconds)
{
	OppState = EKSOppState::Idle; OppT = Clock; OppUntil = Clock + Seconds; OppWaitUntil = Clock + Seconds + 1.5f;
	Combo = 0; OppIdleCount++;
	if (Phase == EKSPhase::Fight) { CueMain.Reset(); CueHint.Reset(); }
}

void AKSGameMode::StartWindup(EKSPunch Type, float QuickSeconds, int32 Side)
{
	const FKSFighterDef& F = Fighter();
	const int32 T = (int32)Type;
	Attack.Type = Type;
	Attack.Side = Side >= 0 ? (EKSArm)Side : FMath::RandBool() ? EKSArm::Left : EKSArm::Right;   // online the other player chose the hand
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
		if (bOnline) return;   // another player decides when he punches (OnlineIncoming); the rest of his turn runs as below
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
	if (bOnline) { OnlineStruck((int32)O, D1 == EKSDefense::Block || D2 == EKSDefense::Block); return; }
	if (O == EOutcome::Dodged) { Stats.Dodges++; PlayerMeter = FMath::Min(1.f, PlayerMeter + 0.12f); Sfx(EKSSfx::Dodge); Popup(TEXT("DODGE!"), KSColors::Green, 0.38f, 0.45f); OpenUp(1.2f); return; }
	if (O == EOutcome::Blocked)
	{
		// A block does more than stop the punch: he is thrown back off the guard and reels, his attack is over and the
		// player has the next word.
		Stats.Blocks++; PlayerMeter = FMath::Min(1.f, PlayerMeter + 0.10f);
		Sfx(EKSSfx::Blocked); Popup(TEXT("BLOCKED"), KSColors::Lilac, 0.38f, 0.45f);
		OppStaggerUntil = Clock + 0.8f;
		if (OppDummy) { OppDummy->Knock(26.f); OppDummy->Flash(FLinearColor::White, 0.06f); }
		CamPunch(FVector(45.f, 0.f, 0.f), FMath::RandBool() ? 7.f : -7.f, -2.f);
		Shake = FMath::Max(Shake, 3.f);
		OpenUp(1.0f);
		UE_LOG(LogTemp, Display, TEXT("KSTEST stagger clip=%d"), OppDummy && OppDummy->HasClip(TEXT("Stagger")) ? 1 : 0);
		return;
	}
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
	PlayerMeter = FMath::Min(1.f, PlayerMeter + 0.05f); OppMeter = FMath::Min(1.f, OppMeter + 0.22f);
	Shake = bLight ? 4.f : 10.f; RedFlash = bLight ? 0.4f : 1.f; Combo = 0;
	PlayerHurtUntil = Clock + 0.3f;
	// The camera takes the punch too: knocked back, tipped to one side.
	CamPunch(FVector(bLight ? -70.f : -170.f, FMath::RandBool() ? 60.f : -60.f, bLight ? 10.f : 30.f), (FMath::RandBool() ? 1.f : -1.f) * (bLight ? 22.f : 60.f), bLight ? 2.f : 5.f);
	Sfx(EKSSfx::Hurt);
	// What it cost, in points on a burst (ten to a point of health, as an arcade would count).
	Popups.Add({ FString::Printf(TEXT("-%d"), FMath::RoundToInt(Damage * 10.f)), KSColors::Red, FVector2D(FMath::FRandRange(0.30f, 0.42f), FMath::FRandRange(0.56f, 0.66f)), Clock, 0.95f, !bLight, 2 });
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
	if (bOnline && Phase == EKSPhase::Fight && !OnlineCanPunch()) return;   // still recovering from the last one
	PunchStart[(int32)Arm] = Clock;
	PunchType[(int32)Arm] = Type;
	if (Phase != EKSPhase::Fight) return;
	if (bOnline) { OnlinePunch(Arm, Type); return; }
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

void AKSGameMode::LandHit(const FPendingHit& Hit, float FixedDamage)
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
	// Online the other player's game works the damage out and reports it (OnlineResult).
	const float Damage = FixedDamage >= 0.f ? FixedDamage : PunchDamage[(int32)Hit.Type] * Hit.Mult;
	OppHP = FMath::Max(0.f, OppHP - Damage);
	// The comic side of a hit: the damage on a burst by his head, and a sound-effect word on the other side.
	{
		const float Side = Hit.Arm == EKSArm::Left ? 1.f : -1.f;
		Popups.Add({ FString::FromInt(FMath::RoundToInt(Damage * 10.f)), KSColors::Yellow, FVector2D(Side * FMath::FRandRange(0.11f, 0.16f), FMath::FRandRange(-0.14f, -0.04f)), Clock, 0.9f, Hit.bCounter, 1 });
		static const TCHAR* Words[] = { TEXT("POW!"), TEXT("BAM!"), TEXT("WHAM!"), TEXT("KRAK!") };
		if (!Hit.bCounter) Popups.Add({ Words[FMath::RandRange(0, 3)], KSColors::Yellow, FVector2D(-Side * FMath::FRandRange(0.12f, 0.17f), FMath::FRandRange(-0.02f, 0.08f)), Clock, 0.7f, false, 3 });
		Sfx(Hit.bCounter ? EKSSfx::Score : EKSSfx::Pop, Hit.bCounter ? 0.5f : 0.45f);
	}
	Stats.Landed++; Combo++; Stats.BestCombo = FMath::Max(Stats.BestCombo, Combo);
	PlayerMeter = FMath::Min(1.f, PlayerMeter + (Hit.bCounter ? 0.16f : 0.07f)); OppMeter = FMath::Min(1.f, OppMeter + 0.03f);
	OppHurtUntil = Clock + 0.24f;
	// The camera drives in behind the punch, from the side of the hand that threw it, with a quick zoom.
	CamPunch(FVector(Hit.bCounter ? 150.f : 85.f, Hit.Arm == EKSArm::Left ? 45.f : -45.f, 0.f), (Hit.Arm == EKSArm::Left ? -1.f : 1.f) * (Hit.bCounter ? 26.f : 10.f), Hit.bCounter ? -7.f : -3.5f);
	if (OppDummy) { OppDummy->Flash(Hit.bCounter ? KSColors::Yellow : FLinearColor::White, 0.08f); OppDummy->TakeHit(Hit.Type, Hit.Arm, Hit.bCounter ? 1.5f : 0.9f); OppDummy->HitStop(0.07f); }
	if (PlayerDummy) PlayerDummy->HitStop(0.07f);
	if (bHitShots) QueueHitShots(TEXT("landed"));
	Shake = FMath::Max(Shake, Hit.bCounter ? 7.f : 4.f);
	CrowdReact(true, Hit.bCounter ? 1.f : FMath::Min(1.f, 0.35f + 0.2f * Combo));
	if (Hit.bCounter) { Sfx(EKSSfx::Counter); Popup(TEXT("COUNTER!"), KSColors::Yellow, 0.56f, 0.3f, true); }
	else Sfx(EKSSfx::Hit);   // the running combo is on the HUD's combo badge
	if (OppHP > 0.f) { if (FMath::FRand() < (Hit.bCounter ? 0.7f : 0.4f)) Speak(OppDummy, TEXT("hurt")); else if (FMath::FRand() < 0.4f) Speak(PlayerDummy, TEXT("hit")); }
	if (OppHP <= 0.f) EndFight(true, false);
}

void AKSGameMode::EndFight(bool bWin, bool bDecision)
{
	bLastResultWin = bWin; bLastResultDecision = bDecision;
	if (bOnline) OnlineBoutLog(bWin, bDecision);
	bSpecialOn = false;
	// One line per bout for tools/check.sh. "travel" is how far the drawn hand moved: near zero means a frozen body.
	UE_LOG(LogTemp, Display, TEXT("KSTEST bout=%d opponent=%s result=%s opp_hand_travel=%.0f player_hand_travel=%.0f landed=%d taken=%d spectators=%d"), FighterIndex, Fighter().Name,
		bDecision ? TEXT("time") : bWin ? TEXT("win") : TEXT("loss"), TestOppHand.IsValid ? TestOppHand.GetSize().Size() : 0.0, TestPlayerHand.IsValid ? TestPlayerHand.GetSize().Size() : 0.0, Stats.Landed, Stats.Taken, Spectators.Num());
	TestOppHand = FBox(ForceInit); TestPlayerHand = FBox(ForceInit);
	// Faces for tools/check.sh: a fighter who showed one face all bout, or never opened the mouth, is the bug.
	int32 Faces = 0, Mouth = 0;
	if (OppDummy) OppDummy->TakeFaceStats(Faces, Mouth);
	UE_LOG(LogTemp, Display, TEXT("KSTEST faces bout=%d opponent=%s shown=%d mouth_opens=%d"), FighterIndex, Fighter().Name, Faces, Mouth);
	if (PlayerDummy) PlayerDummy->TakeFaceStats(Faces, Mouth);
	// The announcer calls it, then the loser has the first word and the winner the last.
	if (OppDummy) OppDummy->StopTalking();
	if (PlayerDummy) PlayerDummy->StopTalking();
	const float CallSeconds = Announcer(bDecision ? TEXT("time") : bWin ? TEXT("ko") : TEXT("down"));
	FLaterVoice After;
	After.Who = bWin ? OppDummy.Get() : PlayerDummy.Get(); After.Prefix = TEXT("lose"); After.When = Clock + CallSeconds + 0.1f;
	After.Then = bWin ? PlayerDummy.Get() : OppDummy.Get(); After.ThenPrefix = TEXT("win");
	LaterVoices.Reset(); LaterVoices.Add(After);
	CueMain.Reset(); CueHint.Reset(); Pending.Reset();
	CrowdReact(bWin, 1.f);
	if (bWin && !bDecision) ConfettiBurst();
	if (!bDecision) { Sfx(bWin ? EKSSfx::Roar : EKSSfx::Boo, 0.6f); NextCrowdVoiceAt = GetWorld()->GetTimeSeconds() + 3.f; }
	UE_LOG(LogTemp, Display, TEXT("KSTEST camera bout=%d shots=%d span=%d travel=%d frames=%d opp_lost=%d opp_hidden=%d"), FighterIndex, OpShots, FMath::RoundToInt(FMath::Max(0.f, OpMaxAngle - OpMinAngle)), FMath::RoundToInt(OpTravel), OpFrames, OpLost, OpHidden);
	OpShots = OpFrames = OpLost = OpHidden = 0; OpTravel = 0.f; OpMinAngle = 1000.f; OpMaxAngle = -1000.f;
	UE_LOG(LogTemp, Display, TEXT("KSTEST crowd bout=%d moves=%d poses=%d"), FighterIndex, FMath::CountBits((uint32)CrowdMovesSeen), FMath::CountBits((uint32)CrowdPosesSeen));
	CrowdMovesSeen = CrowdPosesSeen = 0;
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
			if (!bAutoNoGuard && (!bOnline || bOnlineBotGuard) && (OppState == EKSOppState::Windup || OppState == EKSOppState::Strike))
			{
				AutoDefense = Attack.Type == EKSPunch::Straight ? EKSDefense::Block
					: Attack.Type == EKSPunch::Hook ? (Attack.Side == EKSArm::Left ? EKSDefense::EvadeLeft : EKSDefense::EvadeRight) : EKSDefense::EvadeLeft;
			}
			if (OppState == EKSOppState::Open && Clock >= AutoNextPunch && !bGiveUp)
			{
				AutoNextPunch = Clock + 0.3f;
				FramePunches.Add({ FMath::RandBool() ? EKSArm::Left : EKSArm::Right, (EKSPunch)FMath::RandRange(0, 2) });
			}
			// Against another player nobody hands out openings: the bot also leads off.
			else if (bOnline && OppState == EKSOppState::Idle && Clock >= AutoNextPunch)
			{
				AutoNextPunch = Clock + FMath::FRandRange(0.8f, 1.8f);
				FramePunches.Add({ FMath::RandBool() ? EKSArm::Left : EKSArm::Right, (EKSPunch)FMath::RandRange(0, 2) });
			}
		}
		if (bSpecialOn) TickSpecial(Dt);
		else
		{
			for (const TPair<EKSArm, EKSPunch>& Punch : FramePunches) PlayerPunch(Punch.Key, Punch.Value);
			if (Phase == EKSPhase::Fight) OppAI();
			// The meters. The opponent's creeps up by itself too, so every bout sees his special at least once.
			OppMeter = FMath::Min(1.f, OppMeter + Dt * 0.012f);
			// No specials against another player yet: each game judges only its own health (KSGameModeOnline.cpp), and a
			// special takes health off the other side here. The meters stay empty and the HUD does not draw them.
			if (bOnline) PlayerMeter = OppMeter = 0.f;
			if (PlayerMeter >= 1.f && !bMeterFullSaid) { bMeterFullSaid = true; Sfx(EKSSfx::Score, 0.7f); }
			if (Phase == EKSPhase::Fight && PlayerMeter >= 1.f && (Pressed(EKeys::S) || bShoutNow || (bAuto && !bGiveUp))) StartSpecial(true);
			else if (Phase == EKSPhase::Fight && OppMeter >= 1.f && OppState == EKSOppState::Idle && PhaseTime > 4.f) StartSpecial(false);
		}
		for (int32 i = Pending.Num() - 1; i >= 0 && Phase == EKSPhase::Fight; i--)
		{
			if (Clock >= Pending[i].At) { const FPendingHit Hit = Pending[i]; Pending.RemoveAt(i); LandHit(Hit); }
		}
		if (Phase == EKSPhase::Fight)
		{
			TimeLeft -= Dt;
			if (TimeLeft <= 0.f) { if (bOnline) OnlineTimeUp(); else EndFight(PlayerHP > OppHP / OppMaxHP * 100.f, true); }
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
	if (bOnline) { OnlineAfterBout(); return; }
	const bool bLast = FighterIndex + 1 >= NumFights();
	// The whole ladder is won: once the fighters have had their say, the announcer crowns the player.
	const bool bQuiet = Clock >= AnnouncerUntil && !LaterVoices.Num() && !(OppDummy && OppDummy->IsTalking()) && !(PlayerDummy && PlayerDummy->IsTalking());
	if (bLast && !bChampionSaid && PhaseTime > 0.6f && bQuiet) { bChampionSaid = true; Announcer(TEXT("champion")); ConfettiBurst(); CrowdReact(true, 1.f); }
	if ((PhaseTime > 1.2f && ConfirmPressed()) || (bAuto && PhaseTime > 2.5f))
	{
		if (!bLast) StartCard(FighterIndex + 1);
		// It used to end on the dark street with the cabinet. The player asked for the menu instead.
		else { WhiteFade = 1.f; ToMenu(); }
	}
}

void AKSGameMode::TickContinue(float Dt)
{
	UpdateFighterPoses(Dt);
	UpdateFightCamera(Dt);
	if (bOnline) { OnlineAfterBout(); return; }
	ContinueLeft -= Dt;
	BlackFade = FMath::Clamp(1.f - ContinueLeft / 10.f, 0.f, 0.85f);
	if ((PhaseTime > 1.0f && ConfirmPressed()) || (bAuto && !bGiveUp && PhaseTime > 2.f)) { StartCard(FighterIndex); return; }
	// The count ran out. This used to drop the player on the old street to walk to the cabinet again.
	if (ContinueLeft <= 0.f) { ToMenu(); Announcer(TEXT("gameover")); }
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
	Prompt = PhaseTime > 6.f ? TEXT("Press ENTER for the menu") : TEXT("");
	if ((PhaseTime > 6.f && ConfirmPressed()) || (bAuto && PhaseTime > 16.f)) ToMenu();
}
