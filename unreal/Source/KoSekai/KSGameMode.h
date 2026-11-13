// The whole game: walk the street, find the last cabinet, get pulled inside, fight the cut roster.
// The fight rules are a port of tracking-test/fight.html so both versions play the same.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KSTypes.h"
#include "KSWorld.h"
#include "KSAudio.h"
#include "KSCrowdData.h"
#include "KSGameMode.generated.h"

class AKSDummy;
class AKSPawn;
class UKSTrackingSubsystem;
class FKSNet;

enum class EKSPhase : uint8 { Street, PullIn, Card, Fight, FightEnd, Result, Continue, Ending, Calibrate, Menu, Intro, Select, Online };
/** The screens of the online lobby (EKSPhase::Online): the three choices, waiting as host, typing an address, dialling. */
enum class EKSLobby : uint8 { Choose, Hosting, Typing, Joining };
enum class EKSOppState : uint8 { Idle, Windup, Strike, Open, KO };

struct FKSPopup
{
	FString Text;
	FLinearColor Color;
	FVector2D Pos;      // fraction of the screen
	float Start;
	float Life;
	bool bBig;
	uint8 Kind = 0;     // 0 a word at Pos; 1 damage dealt and 3 a word, both pinned to the opponent's head (Pos is the offset); 2 damage taken
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
	bool bCalJitter = false;                  // -KSCalJitter: test flag, see TickCalibrate
	bool bCalSteady[5] = { false, false, false, false, false }; float CalUnsteady[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };   // debounced readings: body seen, shoulders, elbows, centred, near
	bool bCalLeftOfMiddle = false;
	FString CalHintWas; int32 CalHintChanges = 0;   // how often the hint line changed, for the self-check
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
	FString SpokenLine, SpokenBy;         // the line being spoken now, and by whom
	float AnnounceUntil = 0.f;
	TArray<FKSPopup> Popups;
	float RedFlash = 0.f, WhiteFade = 0.f, BlackFade = 0.f;
	FString Subtitle, Prompt;
	bool bLastResultWin = false, bLastResultDecision = false;
	// Choosing a fighter (after FIGHT on the menu): the one looked at stands in the hall, the seven portraits run along the bottom.
	int32 SelectIndex = 0;
	float SelectMovedAt = -10.f, SelectChosenAt = -1.f;   // game clock; SelectChosenAt < 0 until the player has chosen
	bool bPortraits = false;                  // -KSPortraits: photographs every fighter's head for the portraits and quits
	int32 MenuIndex = 0;                      // the chosen line of the start menu: fight, camera calibration, watch the intro, play online, quit
	/* Online play (KSGameModeOnline.cpp): a bout against another player over the network. */
	bool bOnline = false;                     // the opponent in the ring is another player
	bool bOnlineReady = false, bOnlineOppReady = false;   // after a bout: who has asked for another
	float OnlinePingMs = -1.f;                // there and back, in milliseconds; below zero when not measured yet
	bool bSelectForOnline = false;            // the select screen was opened by PLAY ONLINE: it leads to the lobby, not the ladder
	EKSLobby Lobby = EKSLobby::Choose;
	int32 LobbyIndex = 0, HostPort = 7777;    // the chosen line of the lobby: host, join, back
	FString JoinAddress, LobbyNote, LobbyStatus;   // what is being typed, what went wrong last time, what is happening now
	TArray<FString> HostAddresses;            // this machine's addresses, for the host to read out to a friend
	bool bSpokenByOpp = false;                // who the speech bubble belongs to
	FVector OppHead = FVector::ZeroVector, PlayerHead = FVector::ZeroVector;   // world space, for speech bubbles and damage badges
	TArray<FVector> OppBody, PlayerBody;      // points all over each fighter, world space: the HUD keeps its badges off them
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
	void TickMenu(float Dt);
	void ToMenu();                            // from anywhere (Esc, game over, the end of the ladder): clears the world, shows the menu
	void EnterSelect();
	void ShowSelectFighter();                 // a fresh actor for the fighter being looked at
	void TickSelect(float Dt);
	void TickIntro(float Dt);                 // the opening film (KSCineData.h); a confirm key or a jab at the webcam skips it to the menu
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
	void StartWindup(EKSPunch Type, float QuickSeconds = 0.f, int32 Side = -1);
	void OpenUp(float Seconds);
	void OppAI();
	void ResolveStrike();
	void PlayerHurt(float Damage, bool bLight);
	void PlayerPunch(EKSArm Arm, EKSPunch Type);
	void LandHit(const FPendingHit& Hit, float FixedDamage = -1.f);
	void EndFight(bool bWin, bool bDecision);
	float Rage() const { return 1.f - OppHP / OppMaxHP; }
	EKSDefense Defense() const;
	bool Stance(EKSDefense D) const;

	/* ---- online (KSGameModeOnline.cpp) ---- */
	void EnterLobby(const FString& Note = FString());
	void TickLobby(float Dt);
	void LobbyHost();
	void LobbyJoin();
	void TickOnline();                                   // every frame while there is a link: reads what the other side sent
	void OnlineLine(const FString& Line);
	void OnlinePickMatch();                              // the host settles the two fighters and the arena and tells the guest
	void OnlineBeginMatch(int32 Mine, int32 Theirs, int32 Where);
	void OnlineHangUp();                                 // drops the link and forgets the match; changes no screen
	void OnlineLeave(const FString& Note);               // hangs up and shows the lobby with a word about why
	bool OnlineCanPunch() const;
	void OnlinePunch(EKSArm Arm, EKSPunch Type);         // this player throws: the other side decides what it did
	void OnlineIncoming(EKSPunch Type, EKSArm Arm, bool bCounter);
	void OnlineStruck(int32 Outcome, bool bBlockHeld);   // the other player's punch arrives here: settle it and report back
	void OnlineResult(int32 Outcome, float Damage, float HealthLeft, EKSPunch Type, EKSArm Arm, bool bCounter);
	void OnlineTimeUp();
	void OnlineAfterBout();
	void OnlineBoutLog(bool bWin, bool bDecision);
	/** Online, a punch takes this long to arrive (0 offline): the other player has to be able to see it coming. */
	float OnlinePunchLead(int32 Arm) const;
	TSharedPtr<FKSNet> Net;
	bool bLinked = false, bRemoteCounter = false, bOnlineBotGuard = true;
	int32 OnlineMine = 0, OnlineTheirs = 1, OnlineArena = 0, OnlineBout = 0;
	int32 RemoteChoice = -2;                             // the fighter the other player picked: -1 none, -2 not heard yet
	EKSDefense RemoteDefense = EKSDefense::None, SentDefense = EKSDefense::None;   // the stance each side last reported
	float OnlineReadyAt = 0.f, OnlineLockedUntil = 0.f, OnlineGuardDownUntil = 0.f, OnlineCueUntil = 0.f;
	double OnlinePingAt = 0.0;

	/* ---- helpers ---- */
	float Speak(class AKSDummy* Who, const TCHAR* Prefix, bool bMust = false);
	// A line waiting for its moment, and who answers it (the loser speaks after the announcer, the winner after the loser).
	struct FLaterVoice { TWeakObjectPtr<class AKSDummy> Who; const TCHAR* Prefix = TEXT(""); float When = 0.f; TWeakObjectPtr<class AKSDummy> Then; const TCHAR* ThenPrefix = TEXT(""); };
	TArray<FLaterVoice> LaterVoices;
	/** The ring announcer says a line of KSAnnouncerData.h ("ko", "round1", "name_<Folder>"...), cutting off his last one.
	 *  Returns its length in seconds, 0 if there is no such line. */
	float Announcer(const FString& Key, float Volume = 1.5f);
	void HushAnnouncer();
	float AnnouncerUntil = 0.f;               // game clock
	bool bSelectNameSaid = false, bChampionSaid = false;
	float SelectVoiceSeconds = 0.f;
	int32 ChosenPlayer = -1;                  // the fighter picked on the select screen; -1: a random one (-KSStart=fight)
	float VoiceQuietUntil = 0.f;
	float MusicLevel = 0.f;        // the music's level for this phase, before the dip under a spoken line
	bool bMusicDucked = false;
	void PlayMusic(float FadeSeconds = 0.8f);          // the current arena's track at the current level

	// Arenas (KSArenaData.h): which one stands, which one each bout of the ladder is fought in, and what is in its air.
	int32 ArenaIndex = -1, ForcedArena = -1;           // -KSArena=N fights every bout in arena N
	bool bArenaTour = false;                           // -KSArenaTour builds every arena once (tools/check.sh)
	bool bLookDirty = true;                            // the camera's colour cast must follow the place
	TArray<int32> ArenaLadder;
	struct FMote { TWeakObjectPtr<AActor> Actor; FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector; float Phase = 0.f, Life = 0.f; };
	TArray<FMote> Motes;                               // stars, embers, rain, petals, sparks
	float NextBoltAt = 0.f, BoltUntil = 0.f;           // lightning
	void RaiseArena(int32 Index);                      // builds an arena into the cleared world
	void UseArena(int32 Index);                        // swaps the arena between bouts; the two fighters stay
	void ResetMote(FMote& M, bool bAnywhere);
	void TickMotes(float Now, float Dt);
	bool bIntroSaid = false;
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
	UPROPERTY() TObjectPtr<class UAudioComponent> MusicPlayer;
	UPROPERTY() TObjectPtr<class UAudioComponent> AnnouncerPlayer;

	// Who is fighting: the player's fighter and the opponents, drawn at random each time the cabinet pulls you in.
	int32 PlayerChar = 0;
	TArray<int32> Ladder;
	FKSFighterDef CurrentDef = {};
	int32 ForcedOpponent = -1, ForcedPlayer = -1;   // test flags -KSFighter= and -KSPlayer=
	FString OppClipKey, PlayerClipKey;              // what each real fighter is currently animating
	float OppFollowThroughUntil = 0.f;               // lets a punch finish before the opponent drops back to his stance

	FKSStreet Street;
	FKSArena Arena;
	// A ringside spectator: a flat old-arcade sprite with six poses (KSCrowdData.h), moved about in whole steps.
	enum class ECrowdMove : uint8 { Idle, Jump, Hop, Shake, Sway, Pulse, Cower, Dance, Loop };
	struct FSpectator
	{
		TWeakObjectPtr<AActor> Actor; TObjectPtr<UMaterialInstanceDynamic> MID;
		FVector Foot = FVector::ZeroVector, Across = FVector::ZeroVector;   // where it stands and which way its picture runs
		float Width = 112.f, Height = 224.f;
		EKSBuild Build = EKSBuild::Normal; bool bBeast = false;
		float Phase = 0.f, CueAt = 0.f, BusyUntil = 0.f, NextWhim = 0.f, WaveAt = 0.f;
		// Everyone moves a little differently.
		float IdleRate = 1.6f, BobRate = 3.2f, BobHeight = 3.f, JumpRate = 9.f, JumpHeight = 20.f, SwayRate = 3.f, SwayAngle = 6.f;
		float HoverRate = 1.f, HoverHeight = 0.f, DriftRate = 0.3f, DriftWidth = 0.f;                // flyers only
		int32 IdleStyle = 0;                                  // 0 looks about and bobs, 1 sways, 2 breathes, 3 stands like a post
		int32 Pose = 0, AltPose = -1, Shown = -1;             // AltPose: a second pose it flips to and fro with
		ECrowdMove Move = ECrowdMove::Idle;
		bool bCueCheer = false;
		bool bFlip = false, bShownFlip = false, bRival = false;                                      // a rival cheers for the other fighter
	};
	void CrowdAct(FSpectator& S, bool bHappy, float Seconds);     // picks one of several ways to cheer or to despair
	float NextCrowdVoiceAt = 0.f, NextShoutAt = 0.f, NextBeastAt = 0.f, NextWaveAt = 0.f;
	int32 CrowdMovesSeen = 0, CrowdPosesSeen = 0;                 // bit sets, for the self-check
	UPROPERTY() TObjectPtr<class UAudioComponent> CrowdPlayer;    // the murmur under everything
	// Arena effects (lamps and lasers live in FKSArena; see TickArenaFx).
	struct FConfetti { TWeakObjectPtr<AActor> Actor; FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector; FRotator Spin = FRotator::ZeroRotator; float Phase = 0.f; bool bLive = false; };
	TArray<FConfetti> Confetti;
	float ArenaShowUntil = 0.f;
	bool bArenaFxLogged = false;
	void ConfettiBurst();
	void TickArenaFx();
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
	bool bCalFromMenu = false;                // calibration was chosen in the menu: go back there when it is done
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
	bool bGiveUp = false;        // -KSGiveUp: the test bot neither defends nor punches nor continues, so the count runs out
	float TestEscAt = 0.f;       // -KSEscAt=N: as if Esc were pressed N seconds into the first bout
	bool bFakeFilmPunches = false;   // -KSFakePunch: during the film, webcam hooks and uppercuts for five seconds, then one jab
	float ShotEvery = 0.f, NextShot = 0.f, AutoNextPunch = 0.f, PortraitT = 0.f;
	int32 ShotIndex = 0;
};
