// Online play: the lobby, and a bout against another player instead of the cabinet's own fighters.
//
// How a bout between two players works. Each game shows its own player from behind and the other player as the
// opponent, exactly as in a fight against the cabinet, and the same rules decide what beats what (Outcome() in
// KSGameMode.cpp). The difference is who throws the opponent's punches: the other player.
//
//   - A punch is sent the moment it is thrown ("P"). On the other side it appears as the opponent winding up, with
//     the usual cue, and arrives OnlineWindup() seconds later. That time in the air is what lets a person answer a
//     person, and it hides the network's delay.
//   - The game of the player being punched settles it with that player's own stance at that moment, takes the
//     damage off its own health and reports back ("R"). Each side is the only judge of its own health, so the two
//     screens cannot disagree about a hit.
//   - Dodging a punch leaves the puncher open: he cannot punch for a moment, and punches thrown at him meanwhile
//     are counters (half as much damage again, and no defence against them).
//   - Throwing a punch drops your own guard for a moment, so blocking and punching at once does not work.
//   - Each player picks a fighter on the select screen before the lobby. The host settles the pairing and the
//     arena, rings the bell and calls time for both.
//
// The lines that travel are listed in OnlineLine(). The link itself is KSNet.
#include "KSGameMode.h"
#include "KSDummy.h"
#include "KSPawn.h"
#include "KSTracking.h"
#include "KSNet.h"
#include "KSArenaData.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const float Travel[] = { 0.45f, 0.58f, 0.70f };        // seconds a jab, hook and uppercut are in the air
	const float OnlineDamage[] = { 3.f, 4.5f, 6.f };       // out of 100 health. Half the first numbers (bouts were over too soon); a third made two bots take 72 s, up from 15
	const float OnlineGrace = 0.14f;                       // the same grace as against the cabinet (Grace in KSGameMode.cpp)
	const float Recovery = 0.20f;                          // after a punch has arrived, before the next can be thrown
	const float GuardDown = 0.45f;                         // a punch drops the puncher's own guard for this long
	const float OpenSeconds = 1.1f;                        // how long a dodged puncher stands open
	const float CounterBonus = 1.5f;
	const double QuietLimit = 20.0;                        // seconds without a word before the link is given up
	const int32 MaxAddress = 60;
	const TCHAR* const MissedCue = TEXT("YOU MISSED");

	// Outcome() in KSGameMode.cpp, as numbers on the wire.
	enum { HitPlus = 0, Hit = 1, Half = 2, Blocked = 3, Dodged = 4 };

	bool AddressChar(TCHAR C)
	{
		return FChar::IsAlnum(C) || C == TEXT('.') || C == TEXT(':') || C == TEXT('-') || C == TEXT('#') || C == TEXT('_');
	}
}

/* ================= the lobby ================= */

void AKSGameMode::EnterLobby(const FString& Note)
{
	LobbyNote = Note;
	Lobby = EKSLobby::Choose;
	LobbyStatus.Reset();
	SetPhase(EKSPhase::Online);
}

void AKSGameMode::LobbyHost()
{
	Net = MakeShared<FKSNet>();
	bLinked = false;
	if (!Net->Host(HostPort)) { LobbyNote = Net->Error(); Net.Reset(); Lobby = EKSLobby::Choose; return; }
	HostAddresses = FKSNet::LocalAddresses();
	LobbyNote.Reset();
	Lobby = EKSLobby::Hosting;
}

void AKSGameMode::LobbyJoin()
{
	JoinAddress.TrimStartAndEndInline();
	if (JoinAddress.IsEmpty()) { LobbyNote = TEXT("Type the address first."); Lobby = EKSLobby::Typing; return; }
	// Remembered for next time: nobody wants to type an address twice.
	GConfig->SetString(TEXT("KoSekai"), TEXT("LastAddress"), *JoinAddress, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	Net = MakeShared<FKSNet>();
	bLinked = false;
	Net->Join(JoinAddress);
	LobbyNote.Reset();
	Lobby = EKSLobby::Joining;
}

void AKSGameMode::TickLobby(float Dt)
{
	// The street is behind the lobby picture, as it is behind the start menu.
	Pawn->SetView(WalkPos + FVector(0, 0, 165.f), FRotator::ZeroRotator, 90.f);
	const bool bBack = Pressed(EKeys::Escape) && PhaseTime > 0.2f;   // not the same Esc that just left a match

	if (Lobby == EKSLobby::Choose)
	{
		const int32 Lines = 3;
		const bool bTracked = TrackingSubsystem && TrackingSubsystem->IsTracking() && PhaseTime > 1.5f;
		bool bNext = Pressed(EKeys::Down) || Pressed(EKeys::S) || Pressed(EKeys::Gamepad_DPad_Down), bChoose = false;
		// The webcam as on the start menu: a hand swept down or up moves, a jab with one hand and then the other chooses
		// (not "any punch": a player shifting his weight threw several a minute).
		bool bUp = Pressed(EKeys::Up) || Pressed(EKeys::W) || Pressed(EKeys::Gamepad_DPad_Up);
		if (bTracked) { const int32 Step = WebStep(LobbyIndex, Gesture.StepY, bChoose); bNext |= Step > 0; bUp |= Step < 0; }
		if (bUp) { LobbyIndex = (LobbyIndex + Lines - 1) % Lines; Sfx(EKSSfx::Menu, 0.7f); }
		if (bNext) { LobbyIndex = (LobbyIndex + 1) % Lines; Sfx(EKSSfx::Menu, 0.7f); }
		bChoose |= Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::Gamepad_FaceButton_Bottom);
		if (bBack || (PhaseTime > 0.4f && bChoose && LobbyIndex == 2)) { Sfx(EKSSfx::Menu, 0.7f); SetPhase(EKSPhase::Menu); return; }
		if (PhaseTime > 0.4f && bChoose)
		{
			Sfx(EKSSfx::Select, 0.8f);
			if (LobbyIndex == 0) LobbyHost();
			else
			{
				if (JoinAddress.IsEmpty()) GConfig->GetString(TEXT("KoSekai"), TEXT("LastAddress"), JoinAddress, GGameUserSettingsIni);
				// A build can name the team's relay server in DefaultGame.ini ([KoSekai] OnlineServer=...), so players only press ENTER.
				if (JoinAddress.IsEmpty()) GConfig->GetString(TEXT("KoSekai"), TEXT("OnlineServer"), JoinAddress, GGameIni);
				LobbyNote.Reset();
				Lobby = EKSLobby::Typing;
			}
		}
		return;
	}

	if (Lobby == EKSLobby::Typing)
	{
		if (bBack) { Lobby = EKSLobby::Choose; return; }
		// The address: letters, digits and the few marks an address can hold. CTRL+V pastes one.
		static const TArray<TPair<FKey, TCHAR>> Keys = []
		{
			TArray<TPair<FKey, TCHAR>> K;
			const FKey Digits[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
			const FKey Pad[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
			const FKey Letters[] = { EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M,
				EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z };
			for (int32 i = 0; i < 10; i++) { K.Add({ Digits[i], TCHAR('0' + i) }); K.Add({ Pad[i], TCHAR('0' + i) }); }
			for (int32 i = 0; i < 26; i++) K.Add({ Letters[i], TCHAR('a' + i) });
			K.Add({ EKeys::Period, TEXT('.') }); K.Add({ EKeys::Decimal, TEXT('.') });
			K.Add({ EKeys::Hyphen, TEXT('-') }); K.Add({ EKeys::Subtract, TEXT('-') });
			K.Add({ EKeys::Semicolon, TEXT(':') }); K.Add({ EKeys::Colon, TEXT(':') });
			return K;
		}();
		const bool bCtrl = Down(EKeys::LeftControl) || Down(EKeys::RightControl), bShift = Down(EKeys::LeftShift) || Down(EKeys::RightShift);
		if (bCtrl && Pressed(EKeys::V))
		{
			FString Clip;
			FPlatformApplicationMisc::ClipboardPaste(Clip);
			for (const TCHAR C : Clip) if (AddressChar(C) && JoinAddress.Len() < MaxAddress) JoinAddress.AppendChar(FChar::ToLower(C));
		}
		else if (!bCtrl)
		{
			for (const TPair<FKey, TCHAR>& Key : Keys)
			{
				if (!Pressed(Key.Key) || JoinAddress.Len() >= MaxAddress) continue;
				// SHIFT+3 is the room mark, SHIFT+minus the underscore; everything else is itself.
				JoinAddress.AppendChar(bShift && Key.Key == EKeys::Three ? TEXT('#') : bShift && Key.Key == EKeys::Hyphen ? TEXT('_') : Key.Value);
			}
		}
		if (Pressed(EKeys::BackSpace) && !JoinAddress.IsEmpty()) JoinAddress.LeftChopInline(1);
		if (Pressed(EKeys::Delete)) JoinAddress.Reset();
		if (Pressed(EKeys::Enter)) { Sfx(EKSSfx::Select, 0.8f); LobbyJoin(); }
		return;
	}

	// Hosting or dialling: nothing to do but wait (TickOnline starts the match), or give up.
	if (Net)
	{
		const FKSNet::EState State = Net->State();
		LobbyStatus = State == FKSNet::EState::Listening ? TEXT("WAITING FOR AN OPPONENT")
			: State == FKSNet::EState::Connecting ? TEXT("CONNECTING")
			: State == FKSNet::EState::Waiting ? TEXT("ON THE SERVER - WAITING FOR AN OPPONENT") : TEXT("OPPONENT FOUND");
	}
	if (bBack || !Net)
	{
		OnlineHangUp();
		Sfx(EKSSfx::Menu, 0.7f);
		Lobby = EKSLobby::Choose;
	}
}

/* ================= the link ================= */

void AKSGameMode::TickOnline()
{
	const TSharedPtr<FKSNet> Link = Net;   // kept alive while its lines are read, whatever they lead to
	TArray<FString> Lines;
	Link->Tick(Lines);
	if (Link->State() == FKSNet::EState::Failed) { OnlineLeave(Link->Error()); return; }
	if (Link->State() != FKSNet::EState::Connected) return;

	if (!bLinked)
	{
		bLinked = true;
		OnlinePingAt = 0.0;
		UE_LOG(LogTemp, Display, TEXT("KSTEST online linked role=%s"), Link->IsHost() ? TEXT("host") : TEXT("guest"));   // for tools/check.sh
		// Each side says which fighter its player picked (-1: none, as with -KSHost and -KSJoin); the host waits for the
		// guest's word before it settles the match (OnlineLine).
		RemoteChoice = -2;
		Link->Send(FString::Printf(TEXT("ME %d"), ChosenPlayer));
	}
	for (const FString& Line : Lines)
	{
		OnlineLine(Line);
		if (Net != Link) return;   // that line ended it
	}

	// A word every second: it measures the delay, and silence is how a dead link shows.
	const double Now = FPlatformTime::Seconds();
	if (Now - OnlinePingAt >= 1.0) { OnlinePingAt = Now; Link->Send(FString::Printf(TEXT("PING %.3f"), Now)); }
	if (Link->Silence() > QuietLimit) { OnlineLeave(TEXT("The connection went quiet.")); return; }

	if (!bOnline) return;
	if (Pressed(EKeys::Escape)) { Link->Send(TEXT("BYE")); OnlineLeave(TEXT("You left the match.")); return; }
	// The other side draws this player's fighter leaning, ducking and covering up.
	const EKSDefense Stance = Phase == EKSPhase::Fight ? Defense() : EKSDefense::None;
	if (Stance != SentDefense) { SentDefense = Stance; Link->Send(FString::Printf(TEXT("D %d"), (int32)Stance)); }
	if (OnlineCueUntil > 0.f && Clock >= OnlineCueUntil)
	{
		OnlineCueUntil = 0.f;
		if (CueMain == MissedCue) { CueMain.Reset(); CueHint.Reset(); }
	}
}

void AKSGameMode::OnlineLine(const FString& Line)
{
	TArray<FString> Words;
	Line.ParseIntoArray(Words, TEXT(" "), true);
	if (Words.Num() == 0) return;
	const FString& What = Words[0];
	auto Num = [&Words](int32 i) { return Words.IsValidIndex(i) ? FCString::Atoi(*Words[i]) : 0; };

	if (What == TEXT("ME"))                                                                                      // ME <fighter picked, or -1>
	{
		RemoteChoice = FMath::Max(-1, Num(1));
		if (Net && Net->IsHost() && !bOnline) OnlinePickMatch();
	}
	else if (What == TEXT("MATCH")) { if (Net && !Net->IsHost()) OnlineBeginMatch(Num(2), Num(1), Num(3)); }     // MATCH <host's fighter> <guest's fighter> <arena>
	else if (What == TEXT("GO")) { if (bOnline && Phase == EKSPhase::Card) StartFight(); }                       // the bell
	else if (What == TEXT("P"))                                                                                  // P <punch> <arm> <counter>: a punch is on its way
	{
		if (bOnline && Phase == EKSPhase::Fight) OnlineIncoming((EKSPunch)FMath::Clamp(Num(1), 0, 2), (EKSArm)FMath::Clamp(Num(2), 0, 1), Num(3) != 0);
	}
	else if (What == TEXT("D")) RemoteDefense = (EKSDefense)FMath::Clamp(Num(1), 0, 4);                           // D <stance>: what the other player is holding
	else if (What == TEXT("R"))                                                                                  // R <outcome> <damage x10> <health left x10> <punch> <arm> <counter>
	{
		if (bOnline) OnlineResult(Num(1), Num(2) / 10.f, Num(3) / 10.f, (EKSPunch)FMath::Clamp(Num(4), 0, 2), (EKSArm)FMath::Clamp(Num(5), 0, 1), Num(6) != 0);
	}
	else if (What == TEXT("TIME")) { if (bOnline && Phase == EKSPhase::Fight && Net && !Net->IsHost()) EndFight(Num(1) == 0, true); }   // TIME <1 if the host won>
	else if (What == TEXT("AGAIN")) bOnlineOppReady = true;                                                      // the other player wants another bout
	else if (What == TEXT("PING")) { if (Net && Words.Num() > 1) Net->Send(TEXT("PONG ") + Words[1]); }
	else if (What == TEXT("PONG")) { if (Words.Num() > 1) OnlinePingMs = (float)((FPlatformTime::Seconds() - FCString::Atod(*Words[1])) * 1000.0); }
	else if (What == TEXT("BYE")) OnlineLeave(TEXT("The opponent left."));
}

void AKSGameMode::OnlineHangUp()
{
	if (Net) { Net->Close(); Net.Reset(); }
	bLinked = false; bOnline = false; bOnlineReady = bOnlineOppReady = false; bSelectForOnline = false;
	OnlinePingMs = -1.f;
	RemoteChoice = -2;
}

void AKSGameMode::OnlineLeave(const FString& Note)
{
	const bool bInRing = bOnline;
	OnlineHangUp();
	UE_LOG(LogTemp, Display, TEXT("KSTEST online left note=%s"), *Note);
	if (bInRing) ToMenu();   // clears the ring away
	EnterLobby(Note);
}

/* ================= the match ================= */

void AKSGameMode::OnlinePickMatch()
{
	// Each player fights as the fighter picked on the select screen. Whoever picked none gets a random one, never the
	// other player's; two players who both picked the same fighter get their mirror match.
	const int32 Count = Roster().Num();
	int32 Mine = ChosenPlayer >= 0 ? FMath::Clamp(ChosenPlayer, 0, Count - 1) : -1;
	int32 Theirs = RemoteChoice >= 0 ? FMath::Clamp(RemoteChoice, 0, Count - 1) : -1;
	if (Mine < 0 && Theirs < 0) Mine = FMath::RandRange(0, Count - 1);
	if (Mine < 0) Mine = (Theirs + FMath::RandRange(1, Count - 1)) % Count;
	if (Theirs < 0) Theirs = (Mine + FMath::RandRange(1, Count - 1)) % Count;
	const int32 Where = ForcedArena >= 0 ? FMath::Clamp(ForcedArena, 0, KSArenaCount - 1) : FMath::RandRange(0, KSArenaCount - 1);
	Net->Send(FString::Printf(TEXT("MATCH %d %d %d"), Mine, Theirs, Where));
	OnlineBeginMatch(Mine, Theirs, Where);
}

void AKSGameMode::OnlineBeginMatch(int32 Mine, int32 Theirs, int32 Where)
{
	const int32 Count = Roster().Num();
	OnlineMine = FMath::Clamp(Mine, 0, Count - 1);
	OnlineTheirs = FMath::Clamp(Theirs, 0, Count - 1);
	OnlineArena = FMath::Clamp(Where, 0, KSArenaCount - 1);
	bOnline = true;
	bOnlineReady = bOnlineOppReady = false;
	bRemoteCounter = false;
	RemoteDefense = SentDefense = EKSDefense::None;
	OnlineReadyAt = OnlineLockedUntil = OnlineGuardDownUntil = OnlineCueUntil = 0.f;
	OnlineBout++;
	UE_LOG(LogTemp, Display, TEXT("KSTEST online match=%d me=%s opponent=%s arena=%d"), OnlineBout, Roster()[OnlineMine].Name, Roster()[OnlineTheirs].Name, OnlineArena);
	WhiteFade = 1.f;
	EnterArena();
	StartCard(0);
}

float AKSGameMode::OnlinePunchLead(int32 Arm) const
{
	return bOnline && Phase == EKSPhase::Fight ? Travel[(int32)PunchType[Arm]] + OnlineGrace : 0.f;
}

bool AKSGameMode::OnlineCanPunch() const
{
	return Clock >= OnlineReadyAt && Clock >= OnlineLockedUntil;
}

void AKSGameMode::OnlinePunch(EKSArm Arm, EKSPunch Type)
{
	// A punch thrown at a player who has just missed is a counter: his game will not let him defend it.
	const bool bCounter = OppState == EKSOppState::Open;
	OnlineReadyAt = Clock + Travel[(int32)Type] + OnlineGrace + Recovery;
	OnlineGuardDownUntil = Clock + GuardDown;
	if (bCounter) Stats.Counters++;
	if (Net) Net->Send(FString::Printf(TEXT("P %d %d %d"), (int32)Type, (int32)Arm, bCounter ? 1 : 0));
	Sfx(EKSSfx::Whoosh, 0.5f);
}

void AKSGameMode::OnlineIncoming(EKSPunch Type, EKSArm Arm, bool bCounter)
{
	// One punch at a time: if his last one is still in the air it lands first.
	if (OppState == EKSOppState::Windup || OppState == EKSOppState::Strike) ResolveStrike();
	if (Phase != EKSPhase::Fight) return;
	bRemoteCounter = bCounter;
	StartWindup(Type, Travel[(int32)Type], (int32)Arm);
	Attack.Damage = OnlineDamage[(int32)Type];
	// The test bot gets some punches right and walks into others, so a bout between two bots ends.
	if (bAuto) bOnlineBotGuard = FMath::FRand() < 0.55f;
}

void AKSGameMode::OnlineStruck(int32 Outcome, bool bBlockHeld)
{
	// No defence against a counter, nor with the guard still down from a punch of one's own.
	const bool bDefenceless = bRemoteCounter || Clock < OnlineGuardDownUntil || Clock < OnlineLockedUntil;
	if (bDefenceless && Outcome > Hit) Outcome = Hit;
	const int32 Type = (int32)Attack.Type, Arm = (int32)Attack.Side, Counter = bRemoteCounter ? 1 : 0;
	if (Outcome == Dodged)
	{
		Stats.Dodges++; Sfx(EKSSfx::Dodge); Popup(TEXT("DODGE!"), KSColors::Green, 0.38f, 0.45f);
		OpenUp(OpenSeconds);   // he stands open on this screen, and his own game holds him still (OnlineResult over there)
	}
	else if (Outcome == Blocked)
	{
		Stats.Blocks++; Sfx(EKSSfx::Blocked); Popup(TEXT("BLOCKED"), KSColors::Lilac, 0.38f, 0.45f);
		OppIdle(0.2f);
	}
	float Damage = 0.f;
	if (Outcome <= Half)
	{
		if (Attack.Type == EKSPunch::Uppercut && bBlockHeld && !bDefenceless) Popup(TEXT("GUARD BREAK!"), KSColors::Red, 0.5f, 0.5f, true);
		Damage = OnlineDamage[Type] * (Outcome == Half ? 0.5f : Outcome == HitPlus ? 1.25f : 1.f) * (bRemoteCounter ? CounterBonus : 1.f);
		PlayerHurt(Damage, Outcome == Half);   // ends the bout if that was the last of the health
		if (PlayerHP > 0.f) OppIdle(0.2f);
	}
	if (Net) Net->Send(FString::Printf(TEXT("R %d %d %d %d %d %d"), Outcome, FMath::RoundToInt(Damage * 10.f), FMath::RoundToInt(PlayerHP * 10.f), Type, Arm, Counter));
}

void AKSGameMode::OnlineResult(int32 Outcome, float Damage, float HealthLeft, EKSPunch Type, EKSArm Arm, bool bCounter)
{
	if (Phase != EKSPhase::Fight)
	{
		// The bout ended while this punch was in the air; the number is still the truth about his health.
		if (Outcome <= Half) { OppHP = FMath::Clamp(HealthLeft, 0.f, OppMaxHP); OppTrail = FMath::Max(OppTrail, OppHP); }
		return;
	}
	if (Outcome >= Dodged)
	{
		// Missed: this player stands open, and the other may punch at will for a moment.
		OnlineLockedUntil = Clock + OpenSeconds;
		OnlineCueUntil = Clock + OpenSeconds;
		Combo = 0;
		Sfx(EKSSfx::Dodge);
		Cue(MissedCue, TEXT("you are wide open"));
		return;
	}
	if (Outcome == Blocked) { LandHit({ Clock, Arm, Type, 0.f, false }); return; }
	// His health is whatever his own game says it is; the punch is shown with the damage it did there.
	OppHP = FMath::Clamp(HealthLeft + Damage, 0.f, OppMaxHP);
	LandHit({ Clock, Arm, Type, 1.f, bCounter }, Damage);
	if (Phase == EKSPhase::Fight) OppHP = FMath::Clamp(HealthLeft, 0.f, OppMaxHP);
}

void AKSGameMode::OnlineTimeUp()
{
	TimeLeft = 0.f;
	if (!Net || !Net->IsHost()) return;   // the guest waits to be told ("TIME")
	const bool bHostWon = PlayerHP >= OppHP;
	Net->Send(FString::Printf(TEXT("TIME %d"), bHostWon ? 1 : 0));
	EndFight(bHostWon, true);
}

void AKSGameMode::OnlineBoutLog(bool bWin, bool bDecision)
{
	// One line per bout for tools/check.sh: the two games must tell the same story from opposite sides.
	UE_LOG(LogTemp, Display, TEXT("KSTEST online bout=%d role=%s result=%s my_hp=%d opp_hp=%d landed=%d taken=%d dodges=%d blocks=%d ping_ms=%d"),
		OnlineBout, Net && Net->IsHost() ? TEXT("host") : TEXT("guest"), bDecision ? (bWin ? TEXT("time_win") : TEXT("time_loss")) : bWin ? TEXT("win") : TEXT("loss"),
		FMath::RoundToInt(PlayerHP), FMath::RoundToInt(OppHP), Stats.Landed, Stats.Taken, Stats.Dodges, Stats.Blocks, FMath::RoundToInt(OnlinePingMs));
}

void AKSGameMode::OnlineAfterBout()
{
	// The result is on the screen (YOU WIN, or KNOCKED OUT without the cabinet's countdown). Another bout starts when
	// both players have asked for one; ESC leaves (TickOnline).
	BlackFade = 0.f;
	if (!bOnlineReady && ((PhaseTime > 1.2f && ConfirmPressed()) || (bAuto && PhaseTime > 2.5f)))
	{
		bOnlineReady = true;
		Sfx(EKSSfx::Select, 0.8f);
		if (Net) Net->Send(TEXT("AGAIN"));
	}
	if (bOnlineReady && bOnlineOppReady && Net && Net->IsHost()) OnlinePickMatch();
}
