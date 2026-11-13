#!/usr/bin/env bash
# KO-SEKAI self-check. Builds the game, lets it play itself, and fails on the bugs that have already bitten us.
#
#   tools/check.sh              build + self-playing ladder + online match + recorded-move replays
#   tools/check.sh --no-build   skip the build (the binaries are already current)
#   tools/check.sh --camera     also open the webcam for a few seconds and check frame rate and brightness
#
# It will not start while the game or the KoSekai editor is open: a second copy halves the tracker's frame
# rate for whoever is playing, and the build replaces the library the running game has loaded.
#
# Runs on Linux and on Windows (in Git Bash). Set UE_ROOT if the engine is not where the script looks for it.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/unreal/Saved/Check"
# What differs between the two systems: where the engine is and what its programs are called, how to ask whether
# the game is open, and how a path handed to the game is spelled.
case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*)
	UE="${UE_ROOT:-}"
	if [ -z "$UE" ]; then
		# The Epic launcher writes down where it installed each engine.
		while read -r dir; do
			[ -n "$dir" ] || continue
			dir="$(cygpath -u "$dir")"
			[ -x "$dir/Engine/Binaries/Win64/UnrealEditor.exe" ] && { UE="$dir"; break; }
		done <<< "$(sed -n 's/.*"InstallLocation": *"\(.*\)".*/\1/p' "/c/ProgramData/Epic/UnrealEngineLauncher/LauncherInstalled.dat" 2> /dev/null | sed 's/\\\\/\\/g')"
	fi
	EDITOR="$UE/Engine/Binaries/Win64/UnrealEditor.exe"
	SETTINGS="$ROOT/unreal/Saved/Config/WindowsEditor/GameUserSettings.ini"
	native() { cygpath -w "$1"; }
	game_open() { tasklist //FI "IMAGENAME eq UnrealEditor.exe" 2> /dev/null | grep -qi "UnrealEditor.exe"; }
	build() { "$UE/Engine/Build/BatchFiles/Build.bat" KoSekaiEditor Win64 Development "-project=$PROJECT" -NoHotReloadFromIDE; }
	;;
*)
	UE="${UE_ROOT:-$HOME/UnrealEngine}"
	EDITOR="$UE/Engine/Binaries/Linux/UnrealEditor"
	SETTINGS="$ROOT/unreal/Saved/Config/LinuxEditor/GameUserSettings.ini"
	native() { echo "$1"; }
	game_open() { pgrep -x UnrealEditor > /dev/null; }
	build() { "$UE/Engine/Build/BatchFiles/Linux/Build.sh" KoSekaiEditor Linux Development -project="$PROJECT" -NoHotReloadFromIDE; }
	;;
esac
PROJECT="$(native "$ROOT/unreal/KoSekai.uproject")"
[ -x "$EDITOR" ] || { echo "No Unreal editor at '$EDITOR'. Set UE_ROOT to the engine's folder."; exit 2; }
BUILD=1; CAMERA=0
for a in "$@"; do case "$a" in --no-build) BUILD=0;; --camera) CAMERA=1;; *) echo "unknown option $a"; exit 2;; esac; done

FAILS=0
pass() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; FAILS=$((FAILS + 1)); }

if game_open; then
	echo "The game or the KoSekai editor is running. Close it first, then run the check again."; exit 2
fi
mkdir -p "$OUT"
# Test runs use their own window size; Unreal would remember it and open the next real session that small.
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$OUT/GameUserSettings.keep"
restore() { [ -f "$OUT/GameUserSettings.keep" ] && cp "$OUT/GameUserSettings.keep" "$SETTINGS"; }
trap restore EXIT

# Runs the game with the given flags until the log shows $1 lines matching $2, or $3 seconds pass.
run_until() {
	local want="$1" pattern="$2" limit="$3" log="$4"; shift 4
	rm -f "$log"
	"$EDITOR" "$PROJECT" -game -windowed -ResX=1280 -ResY=720 -nosound -unattended -abslog="$(native "$log")" "$@" > /dev/null 2>&1 &
	local pid=$! waited=0
	while kill -0 "$pid" 2> /dev/null && [ "$waited" -lt "$limit" ]; do
		sleep 2; waited=$((waited + 2))
		[ -f "$log" ] && [ "$(grep -c "$pattern" "$log")" -ge "$want" ] && break
	done
	kill "$pid" 2> /dev/null; wait "$pid" 2> /dev/null
}

# Two copies of the game on this machine, playing themselves against each other, until both logs show $1 online
# bouts or $2 seconds pass. $3 and $4 are the two logs, $5 and $6 each copy's own flags; the second copy starts
# when the first one's log shows $7 (the first has to be listening, or on the relay, before the second dials).
run_pair() {
	local want="$1" limit="$2" log_a="$3" log_b="$4" flags_a="$5" flags_b="$6" ready="$7" waited=0
	rm -f "$log_a" "$log_b"
	local common="-game -windowed -ResX=1280 -ResY=720 -nosound -unattended -KSAuto -KSNoCamera"
	"$EDITOR" "$PROJECT" $common -abslog="$(native "$log_a")" $flags_a > /dev/null 2>&1 &
	local pid_a=$!
	while kill -0 "$pid_a" 2> /dev/null && [ "$waited" -lt 180 ] && ! grep -q "$ready" "$log_a" 2> /dev/null; do sleep 2; waited=$((waited + 2)); done
	"$EDITOR" "$PROJECT" $common -abslog="$(native "$log_b")" $flags_b > /dev/null 2>&1 &
	local pid_b=$!
	waited=0
	while kill -0 "$pid_a" 2> /dev/null && kill -0 "$pid_b" 2> /dev/null && [ "$waited" -lt "$limit" ]; do
		sleep 2; waited=$((waited + 2))
		[ "$(grep -c "KSTEST online bout=" "$log_a" 2> /dev/null)" -ge "$want" ] && [ "$(grep -c "KSTEST online bout=" "$log_b" 2> /dev/null)" -ge "$want" ] && break
	done
	sleep 6   # long enough for both to ask for a rematch and for it to start, so the logs show it
	kill "$pid_a" "$pid_b" 2> /dev/null; wait "$pid_a" "$pid_b" 2> /dev/null
}

# What two logs of the same online bouts must agree on. $1 is a name for the messages, $2 the host's log, $3 the guest's.
check_pair() {
	local what="$1" host="$2" guest="$3" n a b ra rb i
	grep -q "KSTEST online linked role=host" "$host" 2> /dev/null && grep -q "KSTEST online linked role=guest" "$guest" 2> /dev/null \
		&& pass "$what: the two games found each other, one as host and one as guest" || fail "$what: the two games did not link up"
	n=$(grep -c "KSTEST online bout=" "$host" 2> /dev/null); n=${n:-0}
	[ "$n" -ge 1 ] && pass "$what: $n bout(s) fought to the end" || fail "$what: no bout finished"
	for i in $(seq 1 "$n"); do
		a=$(grep "KSTEST online bout=$i " "$host" | head -1); b=$(grep "KSTEST online bout=$i " "$guest" | head -1)
		ra=$(field "$a" result); rb=$(field "$b" result)
		# The bug this guards: two games that each think they won, or that disagree about how much health is left.
		case "$ra/$rb" in
			win/loss|loss/win|time_win/time_loss|time_loss/time_win) pass "$what bout $i: both games agree on the winner (host $ra, guest $rb)";;
			loss/loss) pass "$what bout $i: a double knockout";;
			*) fail "$what bout $i: the games disagree (host says '$ra', guest says '$rb')";;
		esac
		if [ "$ra/$rb" = win/loss ]; then
			[ "$(field "$a" opp_hp)" = 0 ] && [ "$(field "$b" my_hp)" = 0 ] && pass "$what bout $i: both show the loser at zero health" || fail "$what bout $i: health differs between the two games"
		elif [ "$ra/$rb" = loss/win ]; then
			[ "$(field "$b" opp_hp)" = 0 ] && [ "$(field "$a" my_hp)" = 0 ] && pass "$what bout $i: both show the loser at zero health" || fail "$what bout $i: health differs between the two games"
		fi
		if [ "$(field "$a" landed)" = "$(field "$b" taken)" ] && [ "$(field "$b" landed)" = "$(field "$a" taken)" ]; then
			pass "$what bout $i: every punch one game landed, the other took ($(field "$a" landed) and $(field "$b" landed))"
		else
			fail "$what bout $i: punches do not match (host landed $(field "$a" landed) took $(field "$a" taken); guest landed $(field "$b" landed) took $(field "$b" taken))"
		fi
	done
	if grep -qE "Fatal error|Assertion failed|Ensure condition failed|Unhandled Exception" "$host" "$guest" 2> /dev/null; then fail "$what: a game crashed or failed an engine check"
	else pass "$what: no crash in either game"; fi
}

echo "== build"
if [ "$BUILD" = 1 ]; then
	if build > "$OUT/build.log" 2>&1 \
		&& grep -q "Result: Succeeded" "$OUT/build.log"; then pass "the game compiles"
	else fail "the game does not compile (see $OUT/build.log)"; grep -E "error" "$OUT/build.log" | head -5; exit 1; fi
else echo "  skipped"; fi

echo "== self-playing ladder (two bouts, no camera)"
LOG="$OUT/ladder.log"
run_until 2 "KSTEST bout=" 240 "$LOG" -KSStart=fight -KSAuto -KSNoCamera -KSArenaTour
BOUTS=$(grep -c "KSTEST bout=" "$LOG" 2> /dev/null); BOUTS=${BOUTS:-0}
[ "$BOUTS" -ge 2 ] && pass "two bouts were fought to the end" || fail "only $BOUTS bout(s) finished in 240 s"
field() { echo "$1" | tr -d "\r" | sed -n "s/.*$2=\([^ ]*\).*/\1/p"; }
while read -r line; do
	[ -z "$line" ] && continue
	bout=$(field "$line" bout); name=$(echo "$line" | sed -n 's/.*opponent=\(.*\) result=.*/\1/p')
	[ "$(field "$line" opp_hand_travel)" -ge 20 ] && pass "bout $bout: the opponent ($name) moves" || fail "bout $bout: the opponent ($name) is frozen in one pose"
	[ "$(field "$line" player_hand_travel)" -ge 20 ] && pass "bout $bout: the player's fighter moves" || fail "bout $bout: the player's fighter is frozen in one pose"
	[ "$(field "$line" spectators)" -ge 150 ] && pass "bout $bout: $(field "$line" spectators) spectators at ringside" || fail "bout $bout: $(field "$line" spectators) spectators, expected 150 or more"
done <<< "$(grep "KSTEST bout=" "$LOG" 2> /dev/null)"
# The crowd. The bugs this guards: a sheet that did not import leaves a hole in the stands, and a crowd that only
# ever shows one pose or one movement is the old stiff crowd again.
line=$(grep "KSTEST crowd seated=" "$LOG" 2> /dev/null | head -1)
[ "$(field "$line" characters)" -ge 165 ] && pass "$(field "$line" characters) different spectators to draw from" || fail "only $(field "$line" characters) spectator characters (${line:-no KSTEST crowd line}); run art/arena/slice_crowd.py, then art/tools/import_crowd.py"
# The player asked for a house that goes all the way round and for nobody to be in it twice (2026-11-13).
[ "$(field "$line" twice)" = 0 ] && pass "no spectator is in the hall twice" || fail "a spectator was dealt twice (${line:-no KSTEST crowd line})"
for side in far near left right; do
	[ "$(field "$line" $side)" -ge 25 ] 2> /dev/null && pass "$(field "$line" $side) spectators on the $side side of the ring" || fail "only $(field "$line" $side) spectators on the $side side of the ring"
done
[ "$(field "$line" seated)" -ge 55 ] && pass "$(field "$line" seated) seats filled, giants and flyers included" || fail "only $(field "$line" seated) seats filled: a spectator picture is missing from /Game/KS/Arena"
while read -r line; do
	[ -z "$line" ] && continue
	bout=$(field "$line" bout)
	[ "$(field "$line" poses)" -ge 8 ] && pass "bout $bout: the crowd showed all $(field "$line" poses) poses" || fail "bout $bout: the crowd showed only $(field "$line" poses) of 8 poses"
	[ "$(field "$line" moves)" -ge 7 ] && pass "bout $bout: the crowd moved in $(field "$line" moves) different ways" || fail "bout $bout: the crowd moved in only $(field "$line" moves) ways"
done <<< "$(grep "KSTEST crowd bout=" "$LOG" 2> /dev/null)"
# The cameraman. He walks round the ring and picks his own shots; the two ways that goes wrong are that he stands
# still after all (the old fixed shoulder view) or that he wanders off and loses the opponent.
while read -r line; do
	[ -z "$line" ] && { fail "no KSTEST camera line: the cameraman reported nothing"; continue; }
	bout=$(field "$line" bout); frames=$(field "$line" frames); lost=$(field "$line" opp_lost); hidden=$(field "$line" opp_hidden)
	[ "$(field "$line" shots)" -ge 3 ] && [ "$(field "$line" span)" -ge 60 ] && pass "bout $bout: the cameraman took $(field "$line" shots) shots across $(field "$line" span) degrees of the ring" || fail "bout $bout: the cameraman hardly moved ($line)"
	[ "${frames:-0}" -gt 0 ] && [ $((lost * 100 / frames)) -le 7 ] && pass "bout $bout: the opponent's head stayed in the picture ($lost of $frames frames out)" || fail "bout $bout: the cameraman lost the opponent in $lost of ${frames:-0} frames"
	# Seen in stills of the first version: he stood straight behind the player, whose back covered the opponent.
	[ "${frames:-0}" -gt 0 ] && [ $((hidden * 100 / frames)) -le 6 ] && pass "bout $bout: the player's head was in front of the opponent's in $hidden of $frames frames" || fail "bout $bout: the player's own head hid the opponent in ${hidden:-?} of ${frames:-0} frames (cameraman straight behind his back)"
done <<< "$(grep "KSTEST camera bout=" "$LOG" 2> /dev/null)"
for snd in Cheer Gasp Shout; do
	n=$(grep "KSTEST sound=$snd " "$LOG" 2> /dev/null | head -1 | tr -d "\r" | sed -n 's/.*recorded_variants=\([0-9]*\).*/\1/p')
	# These have no synthesised fallback: without the recordings the crowd is silent.
	if [ -z "$n" ]; then echo "  --    crowd sound $snd: not played in this run"
	elif [ "$n" -ge 1 ]; then pass "crowd sound $snd: $n recorded take(s) loaded"
	else fail "crowd sound $snd: no recording loaded, the crowd is silent; run art/tools/prep_crowd.py, then art/tools/import_crowd.py"; fi
done
# The arena's light show: lamps and lasers that are built but never moved would be a dead rig.
line=$(grep "KSTEST arena beams=" "$LOG" 2> /dev/null | head -1)
[ "$(field "$line" beams)" -ge 6 ] && [ "$(field "$line" lasers)" -ge 6 ] && pass "arena light show running: $(field "$line" beams) stage lamps, $(field "$line" lasers) lasers" || fail "arena light show not running (${line:-no KSTEST arena line})"
grep -q "KSTEST arena confetti=" "$LOG" 2> /dev/null && pass "confetti fell after a knockout win" || echo "  --    confetti: no knockout win in this run"
line=$(grep "KSTEST crowd_murmur " "$LOG" 2> /dev/null | head -1)
if [ "$(field "$line" loaded)" = 1 ] && [ "$(field "$line" looping)" = 1 ]; then pass "crowd murmur loaded and set to loop ($(field "$line" seconds) s)"
else fail "crowd murmur missing or not looping (${line:-no KSTEST crowd_murmur line})"; fi
LAST=$(grep "KSTEST bout=" "$LOG" 2> /dev/null | tail -1)
[ -n "$LAST" ] && [ "$(field "$LAST" landed)" -gt 0 ] && pass "punches land" || fail "no punch landed"
for snd in Hit Blocked Whoosh Hurt Footstep; do
	n=$(grep "KSTEST sound=$snd " "$LOG" 2> /dev/null | head -1 | tr -d "\r" | sed -n 's/.*recorded_variants=\([0-9]*\).*/\1/p')
	# A sound the self-playing bout never happened to play (the bot rarely gets hit) is not a failure.
	if [ -z "$n" ]; then echo "  --    sound $snd: not played in this run"
	elif [ "$n" -ge 1 ]; then pass "sound $snd: $n recorded take(s) loaded"
	else fail "sound $snd: no recorded take loaded, the synthesised fallback is in use"; fi
done
line=$(grep "KSTEST music " "$LOG" 2> /dev/null | head -1)
if [ "$(field "$line" loaded)" = 1 ] && [ "$(field "$line" looping)" = 1 ]; then pass "fight music loaded and set to loop ($(field "$line" seconds) s)"
else fail "fight music missing or not looping (${line:-no KSTEST music line}); run art/tools/music.py, then art/tools/import_voice_faces.py"; fi
# A static pointer to an asset is not seen by the garbage collector. The player won the ladder, walked back to the
# cabinet and the game crashed: the beam cone cached in a static had been freed while he was on the street.
bad=$(grep -nE "static U[A-Za-z]+\* *[A-Za-z]+ *= *LoadObject" "$ROOT"/unreal/Source/KoSekai/*.cpp | sed "s|$ROOT/||")
[ -z "$bad" ] && pass "no asset is cached in a static without being kept alive" || fail "asset cached in a static and never rooted (wrap it in Keep() or call AddToRoot): $bad"
# Arenas: -KSArenaTour builds every arena once before the first bout. One whose backdrop, ring canvas or music is
# missing would show a bare wall or fall silent, and only on the day that arena comes up.
n=$(grep "KSTEST arena_built " "$LOG" 2> /dev/null | tr -d "\r" | grep "backdrop=1 mat=1 music=1" | sed 's/.*key=\([^ ]*\).*/\1/' | sort -u | wc -l)
[ "$n" -ge 11 ] && pass "all $n arenas build with their backdrop, ring canvas and music" || fail "only $n of 11 arenas built complete: $(grep "KSTEST arena_built " "$LOG" 2> /dev/null | grep -v "backdrop=1 mat=1 music=1" | sed 's/.*KSTEST //' | head -3 | tr "\n\r" "; ")"
# Faces and voices. The bug this guards: fighters stood through a whole bout with one expression (the Count with his
# mouth open) and never spoke.
while read -r line; do
	[ -z "$line" ] && continue
	who=$(field "$line" fighter)
	[ "$(field "$line" faces)" -ge 4 ] && pass "$who: $(field "$line" faces) face expressions loaded" || fail "$who: only $(field "$line" faces) face expressions loaded"
	[ "$(field "$line" voice_lines)" -ge 8 ] && pass "$who: $(field "$line" voice_lines) voice lines loaded" || fail "$who: only $(field "$line" voice_lines) voice lines loaded"
	# The extra moves: without them the fighter stands in his stance on the select screen and pounds his chest for a special.
	[ "$(field "$line" moves)" -ge 6 ] && pass "$who: all $(field "$line" moves) extra moves loaded (idles, picked, special, win, stagger)" || fail "$who: only $(field "$line" moves) of 6 extra moves loaded; run art/tools/add_moves.py build, then art/tools/import_moves.py"
done <<< "$(grep "KSTEST fighter=" "$LOG" 2> /dev/null | sed 's/.*KSTEST/KSTEST/' | sort -u)"
while read -r line; do
	[ -z "$line" ] && continue
	bout=$(field "$line" bout)
	[ "$(field "$line" shown)" -ge 3 ] && pass "bout $bout: the opponent's face changed ($(field "$line" shown) expressions shown)" || fail "bout $bout: the opponent showed only $(field "$line" shown) expression(s)"
	[ "$(field "$line" mouth_opens)" -ge 1 ] && pass "bout $bout: the opponent's mouth moved while speaking ($(field "$line" mouth_opens) times)" || fail "bout $bout: the opponent's mouth never moved"
done <<< "$(grep "KSTEST faces bout=" "$LOG" 2> /dev/null)"
# Special abilities: the bot sets its special off as soon as the meter is full. The bugs this guards: a special that
# never starts, one played with the stand-in chest pound, and one whose effect has no pieces.
line=$(grep "KSTEST special by=player" "$LOG" 2> /dev/null | head -1 | tr -d "\r")
[ -n "$line" ] && [ "$(field "$line" own_clip)" = 1 ] && pass "the player's special ability went off with its own move ($(field "$line" fighter))" || fail "no special ability with its own move (${line:-the meter never filled or it never started})"
pieces=$(field "$(grep "KSTEST special landed" "$LOG" 2> /dev/null | head -1)" pieces)
[ "${pieces:-0}" -ge 40 ] && pass "the special's effect was drawn ($pieces pieces by the time it landed)" || fail "the special's effect is missing (${pieces:-0} pieces)"
# A blocked punch throws the attacker back and ends his attack.
n=$(grep -c "KSTEST stagger" "$LOG" 2> /dev/null); n=${n:-0}; bad=$(grep "KSTEST stagger" "$LOG" 2> /dev/null | grep -c "clip=0")
if [ "$n" = 0 ]; then echo "  --    block stagger: no punch was blocked in this run"
elif [ "$bad" = 0 ]; then pass "a blocked punch made the attacker stagger ($n times, with the stagger move)"
else fail "the attacker staggered $bad time(s) without his stagger move"; fi
n=$(grep -c "KSTEST voice .*line=intro sound=1" "$LOG" 2> /dev/null); n=${n:-0}
[ "$n" -ge 2 ] && pass "each opponent introduced himself aloud" || fail "only $n spoken introduction(s) in two bouts"
if grep -qE "Fatal error|Assertion failed|Ensure condition failed|Unhandled Exception" "$LOG" 2> /dev/null; then
	fail "the engine reported a crash or failed check"; grep -E "Fatal error|Assertion failed|Ensure condition failed" "$LOG" | head -3
else pass "no crash and no failed engine check"; fi

# The interface. The bug this guards: a picture or font that is not in the build makes the HUD fall back to plain
# boxes and the engine font without a word.
line=$(grep "KSTEST ui pictures=" "$LOG" 2> /dev/null | head -1)
if [ "$(field "$line" missing)" = 0 ] && [ "$(field "$line" pictures)" -ge 36 ] && [ "$(field "$line" fonts)" = 2 ]; then pass "interface art loaded ($(field "$line" pictures) pictures, 2 fonts)"
else fail "interface art incomplete (${line:-no KSTEST ui line}); run art/ui/cut_ui.py, then art/ui/import_ui.py"; fi
grep -q "KSTEST ui speech bubble shown picture=1" "$LOG" 2> /dev/null && pass "a spoken line was shown in a speech bubble" || fail "no speech bubble was drawn"
grep -q "KSTEST ui damage badge shown" "$LOG" 2> /dev/null && pass "a hit showed its damage badge" || fail "no damage badge was drawn"
# Badges used to land on the fighters. The HUD now finds each one a place beside them and reports how the first ten did.
line=$(grep "KSTEST ui layout badges=" "$LOG" 2> /dev/null | head -1)
cover=$(field "$line" worst_body_cover)
if [ "$(field "$line" on_heads)" = 0 ] && awk "BEGIN{exit !(${cover:-9} <= 0.35)}"; then pass "badges keep off the fighters (none on a face, at most ${cover} of one over a body)"
else fail "badges cover the fighters (${line:-no KSTEST ui layout line})"; fi
n=$(grep "KSTEST sound=Pop " "$LOG" 2> /dev/null | head -1 | tr -d "\r" | sed -n 's/.*recorded_variants=\([0-9]*\).*/\1/p')
[ "${n:-0}" -ge 1 ] && pass "interface sounds loaded (Pop: $n takes)" || fail "interface sound Pop missing; run art/tools/prep_audio.py, then art/ui/import_ui.py"

echo "== opening film, start menu, choosing a fighter (skips and chooses by itself, no camera)"
LOG="$OUT/menu.log"
run_until 1 "KSTEST announcer line=round" 150 "$LOG" -KSAuto -KSNoCamera
grep -q "KSTEST ui menu shown background=1 logo=1" "$LOG" 2> /dev/null && pass "the start menu is drawn with its background and logo" || fail "the start menu was not drawn, or its pictures are missing"
line=$(grep "KSTEST intro shown " "$LOG" 2> /dev/null | head -1 | tr -d "\r")
[ -n "$line" ] && [ "$(field "$line" shots)" -ge 1 ] && [ "$(field "$line" shots)" = "$(field "$line" loaded)" ] && pass "the opening film plays: $(field "$line" shots) shots, every sheet loaded" || fail "the opening film is missing pictures (${line:-it was never shown})"
grep -q "KSTEST intro ended" "$LOG" 2> /dev/null && pass "the opening film hands over to the start menu" || fail "the opening film never ended"
grep -q "KSTEST menu chosen=0" "$LOG" 2> /dev/null && pass "the start menu's first line starts the fights" || fail "the start menu did not start the game"

# Choosing a fighter. The bugs this guards: a portrait missing from the build shows a coloured box, and a choice
# that the ladder ignores (the player's fighter used to be drawn at random).
line=$(grep "KSTEST ui select shown " "$LOG" 2> /dev/null | head -1 | tr -d "\r")
[ -n "$line" ] && [ "$(field "$line" fighters)" = 7 ] && [ "$(field "$line" avatars)" = 7 ] && pass "the select screen shows all 7 fighters with their portraits" || fail "the select screen is missing or lacks portraits (${line:-it was never shown}); run -KSPortraits, art/ui/make_avatars.py, art/ui/import_ui.py"
picked=$(field "$(grep "KSTEST select chosen=" "$LOG" 2> /dev/null | head -1)" fighter)
played=$(field "$(grep "KSTEST player fighter=" "$LOG" 2> /dev/null | tail -1)" fighter)
[ -n "$picked" ] && [ "$picked" = "$played" ] && pass "the fighter chosen ($picked) is the one the player fights as" || fail "chose '${picked:-nobody}' but the ladder started as '${played:-nobody}'"
grep -q "KSTEST select idle fighter=.* clip=" "$LOG" 2> /dev/null && pass "the fighter on the select screen dances instead of holding his stance" || fail "the fighter on the select screen only holds his stance (no dance played)"
# The ring announcer: every line he is asked for must be in the table and have its recording.
asked=$(grep -c "KSTEST announcer line=" "$LOG" 2> /dev/null); asked=${asked:-0}
silent=$(grep "KSTEST announcer line=" "$LOG" 2> /dev/null | grep -c "sound=0")
[ "$asked" -ge 3 ] && [ "$silent" = 0 ] && pass "the announcer spoke $asked times, every line with its recording" || fail "the announcer: $asked lines asked for, $silent without a recording; run art/tools/announcer.py, then art/tools/import_voice_faces.py"

echo "== the film is skipped by a jab only (fake webcam punches: hooks and uppercuts for five seconds, then a jab)"
# The bug this guards: any punch the webcam called skipped the film, so it stopped whenever the player moved.
LOG="$OUT/filmskip.log"
run_until 1 "KSTEST intro ended" 90 "$LOG" -KSFakePunch -KSNoCamera
line=$(grep "KSTEST intro ended" "$LOG" 2> /dev/null | head -1 | tr -d "\r")
secs=$(field "$line" seconds)
if [ "$(field "$line" by_jab)" = 1 ] && awk "BEGIN{exit !(${secs:-0} >= 5.5)}"; then pass "hooks and uppercuts did not stop the film; the jab at 6 s did (ended at ${secs} s)"
else fail "the film ended on the wrong punch (${line:-it never ended})"; fi

echo "== Esc returns to the menu; a lost bout whose count runs out returns to the menu"
LOG="$OUT/esc.log"
run_until 1 "KSTEST to_menu" 120 "$LOG" -KSStart=fight -KSAuto -KSNoCamera -KSEscAt=2
grep -q "KSTEST to_menu from_phase=3" "$LOG" 2> /dev/null && pass "Esc in the middle of a bout goes to the start menu" || fail "Esc in a bout did not reach the menu"
# The bug this guards: when the CONTINUE? count ran out the player was put on the old street with the cabinet.
LOG="$OUT/gameover.log"
run_until 1 "KSTEST ui menu shown" 240 "$LOG" -KSStart=fight -KSAuto -KSGiveUp -KSNoCamera
grep -q "KSTEST to_menu from_phase=6" "$LOG" 2> /dev/null && grep -q "KSTEST ui menu shown" "$LOG" 2> /dev/null && pass "game over (count run out) shows the start menu" || fail "game over did not reach the start menu ($(grep -c "KSTEST bout=" "$LOG" 2> /dev/null) bout(s) ended)"
calls=$(grep -c "EnterStreet(" "$ROOT/unreal/Source/KoSekai/KSGameMode.cpp")
[ "$calls" -le 2 ] && pass "nothing in the game leads back to the street (only -KSStart=street)" || fail "$((calls - 2)) place(s) in KSGameMode.cpp still send the player to the street: $(grep -n "EnterStreet(" "$ROOT/unreal/Source/KoSekai/KSGameMode.cpp" | cut -d: -f1 | tr "\n" " ")"

echo "== microphone recorder (opened without sound; the game is then killed)"
# The bug this guards (player report, 2026-11-13): the webcam's light stayed on after quitting. The recorder the game
# starts for the microphone inherited the game's open files, the webcam among them, and outlived the game.
if [ -x /usr/bin/arecord ]; then
	LOG="$OUT/mic.log"; rm -f "$LOG"; before=$(pgrep -x arecord | sort)
	"$EDITOR" "$PROJECT" -game -windowed -ResX=1280 -ResY=720 -nosound -unattended -abslog="$LOG" -KSStart=menu -KSMic -KSNoCamera > /dev/null 2>&1 &
	pid=$!; waited=0
	while kill -0 "$pid" 2> /dev/null && [ "$waited" -lt 90 ] && ! grep -q "KSTEST mic open=" "$LOG" 2> /dev/null; do sleep 2; waited=$((waited + 2)); done
	sleep 3
	rec=$(comm -13 <(echo "$before") <(pgrep -x arecord | sort) | head -1)
	if grep -q "KSTEST mic open=1" "$LOG" 2> /dev/null && [ -n "$rec" ]; then
		# The game has its own log open; a recorder that inherited the game's files has it too.
		files=$(ls -l /proc/"$rec"/fd 2> /dev/null | grep -c "mic.log\|/dev/video")
		[ "$files" = 0 ] && pass "the recorder holds none of the game's open files" || fail "the recorder inherited the game's open files: a webcam among them stays on after quitting"
		kill -9 "$pid" 2> /dev/null; wait "$pid" 2> /dev/null; sleep 3
		kill -0 "$rec" 2> /dev/null && { fail "the recorder outlived the game (pid $rec)"; kill "$rec"; } || pass "the recorder stopped when the game was killed"
	else
		if grep -q "KSTEST mic open=1" "$LOG" 2> /dev/null; then fail "the game says the microphone is open but no recorder is running"
		else echo "  --    microphone did not open here ($(grep -o "KSTEST mic open=.*" "$LOG" 2> /dev/null | head -1)): recorder not checked"; fi
		kill "$pid" 2> /dev/null; wait "$pid" 2> /dev/null
	fi
	restore
else echo "  --    no arecord on this machine: recorder not checked"; fi

echo "== calibration screen (stepped through automatically, no camera)"
LOG="$OUT/calibration.log"
# -KSCalJitter makes one framing reading flip every frame, like a player standing right on a limit. The bug this
# guards: every line of text on the calibration screen flickered (1594 changes of the hint in one calibration).
run_until 1 "KSTEST calibration done" 120 "$LOG" -KSStart=fight -KSCalDemo -KSCalJitter -KSNoCamera
n=$(grep "KSTEST calibration hint_changes=" "$LOG" 2> /dev/null | head -1 | tr -d "\r" | sed -n 's/.*hint_changes=\([0-9]*\).*/\1/p')
[ -n "$n" ] && [ "$n" -le 40 ] && pass "calibration text is steady with a jittering camera reading ($n changes of the hint in nine steps)" || fail "calibration text flickers: the hint changed ${n:-?} times in nine steps"
grep -q "KSTEST ui calibration text_outside_box=0" "$LOG" 2> /dev/null && pass "calibration writing stays inside its panels" || fail "calibration writing runs out of its panel ($(grep "KSTEST ui calibration" "$LOG" 2> /dev/null | head -1 | sed 's/.*KSTEST/KSTEST/'))"
grep -q "KSTEST calibration done completed=1" "$LOG" 2> /dev/null && pass "all nine calibration steps run and hand over to the first bout" || fail "calibration did not reach its end"

echo "== online match (two copies on this machine, one hosting and one joining)"
run_pair 2 300 "$OUT/online_host.log" "$OUT/online_guest.log" "-KSHost -KSPort=17777" "-KSJoin=127.0.0.1:17777" "Hosting on port"
check_pair "direct" "$OUT/online_host.log" "$OUT/online_guest.log"
grep -q "KSTEST online match=2 " "$OUT/online_host.log" 2> /dev/null && grep -q "KSTEST online match=2 " "$OUT/online_guest.log" 2> /dev/null \
	&& pass "direct: both asked for a rematch and it started" || fail "direct: the rematch did not start"

echo "== online match through the relay server (tools/ko_relay.py; both copies join it)"
PY=""
for c in python3 python "$HOME"/AppData/Local/Programs/Python/Python3*/python.exe; do
	"$c" -c "import asyncio" > /dev/null 2>&1 && { PY="$c"; break; }
done
if [ -z "$PY" ]; then echo "  --    no Python 3 here: the relay was not tested"
else
	"$PY" "$(native "$ROOT/tools/ko_relay.py")" --port 17778 > "$OUT/relay.log" 2>&1 &
	RELAY=$!
	sleep 2
	# Whoever reaches the relay first is made the host.
	run_pair 1 240 "$OUT/relay_first.log" "$OUT/relay_second.log" "-KSJoin=127.0.0.1:17778#check" "-KSJoin=127.0.0.1:17778#check" "waiting to be given a role"
	kill "$RELAY" 2> /dev/null; wait "$RELAY" 2> /dev/null
	grep -q "meets" "$OUT/relay.log" 2> /dev/null && pass "relay: it put the two players in one room" || fail "relay: it never paired the two players (see $OUT/relay.log)"
	check_pair "relay" "$OUT/relay_first.log" "$OUT/relay_second.log"
fi

echo "== recorded moves (tests/tracking/*.csv with a .expect file)"
CASES=0
for rec in "$ROOT"/tests/tracking/*.csv; do
	[ -f "$rec" ] || continue
	exp="${rec%.csv}.expect"; [ -f "$exp" ] || continue
	CASES=$((CASES + 1)); name="$(basename "$rec" .csv)"; LOG="$OUT/replay_$name.log"
	run_until 1 "REPLAY done" 90 "$LOG" -KSStart=fight -KSReplay="$(native "$rec")" -KSReplayExit
	if ! grep -q "REPLAY done" "$LOG" 2> /dev/null; then fail "$name: the replay did not finish"; continue; fi
	# .expect lines:  <what> <min> <max>   where <what> is: punches | straight | hook | uppercut | left | right | block
	while read -r what min max; do
		case "$what" in ""|\#*) continue;; esac
		case "$what" in
			punches) n=$(grep -c "REPLAY .* PUNCH " "$LOG");;
			straight|hook|uppercut) n=$(grep -c "REPLAY .* PUNCH $what " "$LOG");;
			left) n=$(grep -c "REPLAY .* arm 0 PUNCH " "$LOG");;
			right) n=$(grep -c "REPLAY .* arm 1 PUNCH " "$LOG");;
			block) n=$(grep -c "REPLAY .* stance 1 ON" "$LOG");;
			*) fail "$name: unknown expectation '$what'"; continue;;
		esac
		if [ "$n" -ge "$min" ] && [ "$n" -le "$max" ]; then pass "$name: $what = $n (wanted $min to $max)"
		else fail "$name: $what = $n (wanted $min to $max)"; fi
	done < "$exp"
done
[ "$CASES" -eq 0 ] && echo "  none yet (see docs/TRACKING.md, 'Adding a recorded test')"

if [ "$CAMERA" = 1 ]; then
	echo "== webcam"
	LOG="$OUT/camera.log"
	run_until 3 "LogKSCamera: .* frames/s" 40 "$LOG" -KSStart=fight
	if grep -q "camera .* open" "$LOG" 2> /dev/null; then
		pass "the camera opens"
		fps=$(grep "LogKSCamera: .* frames/s" "$LOG" | tail -1 | sed -n 's/.*LogKSCamera: \([0-9]*\) frames.*/\1/p')
		[ "${fps:-0}" -ge 25 ] && pass "tracker runs at $fps frames/s" || fail "tracker runs at ${fps:-0} frames/s, expected 25 or more"
		mean=$(grep "input mean" "$LOG" | tail -1 | sed -n 's/.*input mean \([0-9.]*\).*/\1/p')
		awk "BEGIN { exit !(${mean:-0} > 0.03) }" && pass "camera picture is not black (brightness $mean)" || fail "camera picture is black (brightness ${mean:-0})"
	else fail "the camera did not open"; fi
fi

echo
if [ "$FAILS" -eq 0 ]; then echo "ALL CHECKS PASSED"; exit 0; else echo "$FAILS CHECK(S) FAILED"; exit 1; fi
