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

enum class EKSPhase : uint8 { Street, PullIn, Card, Fight, FightEnd, Result, Continue, Ending };
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
	const FKSFighterDef& Fighter() const { return Roster()[FighterIndex]; }
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
	void TickFight(float Dt);
	void TickFightEnd(float Dt);
	void TickResult(float Dt);
	void TickContinue(float Dt);
	void TickEnding(float Dt);
	void UpdateFightCamera(float Dt);
	void UpdateFighterPoses(float Dt);

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

	FKSStreet Street;
	FKSArena Arena;
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
	float ShotEvery = 0.f, NextShot = 0.f, AutoNextPunch = 0.f;
	int32 ShotIndex = 0;
};
