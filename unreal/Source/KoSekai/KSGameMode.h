// The whole game: walk the street, find the last cabinet, get pulled inside, fight the cut roster.
// The fight rules are a port of tracking-test/fight.html so both versions play the same.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KSTypes.h"
#include "KSWorld.h"
#include "KSAudio.h"
#include "KSGameMode.generated.h"

class AKSDummy;
class AKSPawn;
class UKSTrackingSubsystem;

enum class EKSPhase : uint8 { Street, PullIn, Card, Fight, FightEnd, Result, Continue, Ending, Calibrate };
enum class EKSOppState : uint8 { Idle, Windup, Strike, Open, KO };

struct FKSPopup
{
	FString Text;
	FLinearColor Color;
	FVector2D Pos;      // fraction of the screen
	float Start;
	float Life;
	bool bBig;
};

struct FKSStats
{
	int32 Landed = 0, Counters = 0, Dodges = 0, Blocks = 0, Taken = 0, BestCombo = 0;
};

UCLASS()
class AKSGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AKSGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static const TArray<FKSFighterDef>& Roster();

	/* ---- read by the HUD ---- */
	EKSPhase Phase = EKSPhase::Street;
	float PhaseTime = 0.f;        // seconds in the current phase
	// Calibration, the screen before the first bout (webcam players only): where to stand, then each move once.
	static constexpr int32 CalSteps = 9;
	int32 CalStep = 0;            // 0 framing, 1 guard, 2 left jabs, 3 right jabs, 4 block, 5 lean left, 6 lean right, 7 duck, 8 ready
	float CalStepTime = 0.f, CalProgress = 0.f;
	FString CalTitle, CalHint;
	int32 CalCountNow = 0, CalCountNeed = 0;   // repetitions done and wanted in this step (0 wanted = not a counted step)
	TArray<TPair<FString, bool>> CalChecks;   // the framing checklist: what is right and what is not
	float Clock = 0.f;            // game clock, stops while paused
	bool bPaused = false;
	int32 FighterIndex = 0;
	float PlayerHP = 100.f, OppHP = 100.f, OppMaxHP = 100.f, TimeLeft = 120.f;
	float PlayerTrail = 100.f, OppTrail = 100.f;   // the slower bar behind each health bar
	int32 Combo = 0;
	FKSStats Stats;
	FString CueMain, CueHint;
	bool bCueGood = false;
	FString Announcement;
	float AnnounceUntil = 0.f;
	TArray<FKSPopup> Popups;
	float RedFlash = 0.f, WhiteFade = 0.f, BlackFade = 0.f;
	FString Subtitle, Prompt;
	bool bLastResultWin = false, bLastResultDecision = false;
	float ContinueLeft = 0.f;
	/** The opponent of the current fight: one of the roster, with the difficulty of its place on the ladder. */
	const FKSFighterDef& Fighter() const { return CurrentDef; }
	/** The fighter the player was given for this run. */
	const FKSFighterDef& PlayerFighter() const { return Roster()[PlayerChar]; }
	int32 NumFights() const { return Ladder.Num(); }
	const FKSFighterDef& LadderFighter(int32 Position) const { return Roster()[Ladder[FMath::Clamp(Position, 0, Ladder.Num() - 1)]]; }
	UKSTrackingSubsystem* Tracking() const { return TrackingSubsystem; }

private:
	/* ---- flow ---- */
	void SetPhase(EKSPhase NewPhase);
	void EnterStreet(bool bDead);
	void EnterArena();
	void StartCard(int32 Index);
	void StartFight();
	void ClearWorld();
	void TickStreet(float Dt);
	void TickPullIn(float Dt);
	void TickCard(float Dt);
	void BeginLadder();                  // calibration first when the webcam is in use, then the first bout
	void TickCalibrate(float Dt);
	void SetCalStep(int32 Step);
	void FinishCalibration(bool bCompleted);
	void TickFight(float Dt);
	void TickFightEnd(float Dt);
	void TickResult(float Dt);
	void TickContinue(float Dt);
	void TickEnding(float Dt);
	void UpdateFightCamera(float Dt);
	void UpdateFighterPoses(float Dt);
	void DriveCharacters();
	void SeatCrowd();                                  // the fighters who are not in this bout watch from ringside
	void CrowdReact(bool bCheer, float Strength);      // cheer for the player's hit, wince when the player is hit
	void TickCrowd();
	void PickFighters();

	/* ---- fight rules (see fight.html) ---- */
	struct FAttack { EKSPunch Type = EKSPunch::Straight; EKSArm Side = EKSArm::Left; float Damage = 0.f, Duration = 0.f; bool bFeint = false; };
	struct FPendingHit { float At; EKSArm Arm; EKSPunch Type; float Mult; bool bCounter; };
	void OppIdle(float Seconds);
	void StartWindup(EKSPunch Type, float QuickSeconds = 0.f);
	void OpenUp(float Seconds);
	void OppAI();
	void ResolveStrike();
	void PlayerHurt(float Damage, bool bLight);
	void PlayerPunch(EKSArm Arm, EKSPunch Type);
	void LandHit(const FPendingHit& Hit);
	void EndFight(bool bWin, bool bDecision);
	float Rage() const { return 1.f - OppHP / OppMaxHP; }
	EKSDefense Defense() const;
	bool Stance(EKSDefense D) const;

	/* ---- helpers ---- */
	void Announce(const FString& Text, float Seconds) { Announcement = Text; AnnounceUntil = Clock + Seconds; }
	void Cue(const FString& Main, const FString& Hint, bool bGood = false) { CueMain = Main; CueHint = Hint; bCueGood = bGood; }
	void Popup(const FString& Text, const FLinearColor& Color, float X, float Y, bool bBig = false);
	void Sfx(EKSSfx Sound, float Volume = 1.f) { KSAudio::Play(this, Sound, Volume); }
	bool Pressed(const FKey& Key) const;
	bool Down(const FKey& Key) const;
	bool ConfirmPressed();
	FKSBuilder MakeBuilder();

	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> SolidMaterial;
	UPROPERTY() TObjectPtr<UTextureCube> SkyCube;
	UPROPERTY() TArray<TObjectPtr<AActor>> WorldActors;
	UPROPERTY() TObjectPtr<AKSDummy> PlayerDummy;
	UPROPERTY() TObjectPtr<AKSDummy> OppDummy;
	UPROPERTY() TObjectPtr<AKSPawn> Pawn;
	UPROPERTY() TObjectPtr<APlayerController> PC;
	UPROPERTY() TObjectPtr<UKSTrackingSubsystem> TrackingSubsystem;

	// Who is fighting: the player's fighter and the opponents, drawn at random each time the cabinet pulls you in.
	int32 PlayerChar = 0;
	TArray<int32> Ladder;
	FKSFighterDef CurrentDef = {};
	int32 ForcedOpponent = -1, ForcedPlayer = -1;   // test flags -KSFighter= and -KSPlayer=
	FString OppClipKey, PlayerClipKey;              // what each real fighter is currently animating
	float OppFollowThroughUntil = 0.f;               // lets a punch finish before the opponent drops back to his stance

	FKSStreet Street;
	FKSArena Arena;
	// A ringside spectator: a flat old-arcade sprite with four poses side by side (idle, cheer, wince, idle mirrored).
	struct FSpectator
	{
		TWeakObjectPtr<AActor> Actor; TObjectPtr<UMaterialInstanceDynamic> MID; FVector Home;
		float Phase = 0.f, CueAt = 0.f, BusyUntil = 0.f, NextWhim = 0.f;
		float IdleRate = 1.6f, BobRate = 3.2f, BobHeight = 3.f, JumpRate = 9.f, JumpHeight = 20.f;   // everyone moves a little differently
		int32 CuePose = 0, Pose = 0, Shown = -1;
		bool bFlip = false, bShownFlip = false, bRival = false;                                      // a rival cheers for the other fighter
	};
	// Camera flashes in the crowd: a small pool of glowing balls and lights, each shown for a few hundredths of a second.
	struct FCrowdFlash { TWeakObjectPtr<AActor> Actor; TObjectPtr<UMaterialInstanceDynamic> MID; float Until = 0.f; };
	TArray<FCrowdFlash> Flashes;
	TArray<TWeakObjectPtr<APointLight>> FlashLights;
	float FlashesOwed = 0.f, NextFlashAt = 0.f;
	void CrowdFlashes(float Count) { FlashesOwed += Count; }
	// The fight camera: springs that the action pushes on (see UpdateFightCamera).
	FVector CamOffset = FVector::ZeroVector, CamVel = FVector::ZeroVector, CamAimLag = FVector::ZeroVector;
	float CamRoll = 0.f, CamRollVel = 0.f, CamFovKick = 0.f, CamDolly = 0.f, CamSide = 0.f, CamDrop = 0.f;
	void CamPunch(const FVector& Impulse, float RollImpulse, float FovKick) { CamVel += Impulse; CamRollVel += RollImpulse; CamFovKick += FovKick; }
	TArray<FSpectator> Spectators;
	bool bCalibrationDone = false, bCalDemo = false;   // -KSCalDemo: enter calibration without a camera and step through it, for the self-check
	float CalHold = 0.f, CalPunchUntil = 0.f, CalDoneAt = -1.f;
	bool bCalRepCounted = false;
	int32 CalCount = 0;
	FString CalClipKey;
	TArray<float> CalPeaks;
	FBox TestOppHand = FBox(ForceInit), TestPlayerHand = FBox(ForceInit);   // self-test: how far each fighter's drawn hand travelled this bout
	bool bStreetDead = false;

	// street walking
	FVector WalkPos = FVector::ZeroVector;
	float WalkYaw = 0.f, WalkPitch = 0.f, StepPhase = 0.f;
	FVector PullFrom = FVector::ZeroVector;
	FRotator PullFromRot = FRotator::ZeroRotator;

	// fight
	EKSOppState OppState = EKSOppState::Idle;
	float OppT = 0.f, OppUntil = 0.f, OppWaitUntil = 0.f, OppGuardUntil = 0.f, OppHurtUntil = 0.f, PlayerHurtUntil = 0.f;
	int32 OpenHits = 0, BlockStreak = 0;
	bool bTellBeeped = false;
	EKSDefense Def0 = EKSDefense::None;
	FAttack Attack;
	TArray<FPendingHit> Pending;
	float PunchStart[2] = { -10.f, -10.f };
	EKSPunch PunchType[2] = { EKSPunch::Straight, EKSPunch::Straight };
	float Shake = 0.f;
	bool bFightStarted = false;
	TArray<TPair<EKSArm, EKSPunch>> FramePunches;   // keyboard and webcam punches that arrived this frame
	EKSDefense AutoDefense = EKSDefense::None;

	// test helpers: -KSAuto plays by itself, -KSShots=N saves a screenshot every N seconds, -KSStart=fight skips the street
	bool bAuto = false;
	bool bHitShots = false;      // -KSHitShots: save a screenshot at the moment each hit lands (hit_NN.png)
	int32 HitShotIndex = 0;
	TArray<TPair<float, FString>> BurstShots;   // (clock time, file name) of the follow-up frames after a hit
	void QueueHitShots(const TCHAR* Kind);
	bool bMirrorArms = false;    // -KSMirrorArms: the fighter's arms copy the webcam arms directly (experimental, off by default)
	bool bFakeArms = false;      // -KSFakeArms: pretend the webcam sees a fixed arm pose, to check the arm copying
	bool bAutoNoGuard = false;   // -KSNoGuard: the test bot never defends, so the opponent's punches land
	float ShotEvery = 0.f, NextShot = 0.f, AutoNextPunch = 0.f;
	int32 ShotIndex = 0;
};
