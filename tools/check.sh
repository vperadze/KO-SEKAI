#!/usr/bin/env bash
# KO-SEKAI self-check. Builds the game, lets it play itself, and fails on the bugs that have already bitten us.
#
#   tools/check.sh              build + self-playing ladder + recorded-move replays
#   tools/check.sh --no-build   skip the build (the binaries are already current)
#   tools/check.sh --camera     also open the webcam for a few seconds and check frame rate and brightness
#
# It will not start while the game or the KoSekai editor is open: a second copy halves the tracker's frame
# rate for whoever is playing, and the build replaces the library the running game has loaded.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="$ROOT/unreal/KoSekai.uproject"
UE="${UE_ROOT:-$HOME/UnrealEngine}"
EDITOR="$UE/Engine/Binaries/Linux/UnrealEditor"
OUT="$ROOT/unreal/Saved/Check"
SETTINGS="$ROOT/unreal/Saved/Config/LinuxEditor/GameUserSettings.ini"
BUILD=1; CAMERA=0
for a in "$@"; do case "$a" in --no-build) BUILD=0;; --camera) CAMERA=1;; *) echo "unknown option $a"; exit 2;; esac; done

FAILS=0
pass() { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; FAILS=$((FAILS + 1)); }

if pgrep -x UnrealEditor > /dev/null; then
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
	"$EDITOR" "$PROJECT" -game -windowed -ResX=1280 -ResY=720 -nosound -unattended -abslog="$log" "$@" > /dev/null 2>&1 &
	local pid=$! waited=0
	while kill -0 "$pid" 2> /dev/null && [ "$waited" -lt "$limit" ]; do
		sleep 2; waited=$((waited + 2))
		[ -f "$log" ] && [ "$(grep -c "$pattern" "$log")" -ge "$want" ] && break
	done
	kill "$pid" 2> /dev/null; wait "$pid" 2> /dev/null
}

echo "== build"
if [ "$BUILD" = 1 ]; then
	if "$UE/Engine/Build/BatchFiles/Linux/Build.sh" KoSekaiEditor Linux Development -project="$PROJECT" -NoHotReloadFromIDE > "$OUT/build.log" 2>&1 \
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
	[ "$(field "$line" spectators)" -ge 55 ] && pass "bout $bout: $(field "$line" spectators) spectators at ringside" || fail "bout $bout: $(field "$line" spectators) spectators, expected 55 or more"
done <<< "$(grep "KSTEST bout=" "$LOG" 2> /dev/null)"
# The crowd. The bugs this guards: a sheet that did not import leaves a hole in the stands, and a crowd that only
# ever shows one pose or one movement is the old stiff crowd again.
line=$(grep "KSTEST crowd seated=" "$LOG" 2> /dev/null | head -1)
[ "$(field "$line" characters)" -ge 45 ] && pass "$(field "$line" characters) different spectators to draw from" || fail "only $(field "$line" characters) spectator characters (${line:-no KSTEST crowd line}); run art/arena/slice_crowd.py, then art/tools/import_crowd.py"
[ "$(field "$line" seated)" -ge 55 ] && pass "$(field "$line" seated) seats filled, giants and flyers included" || fail "only $(field "$line" seated) seats filled: a spectator picture is missing from /Game/KS/Arena"
while read -r line; do
	[ -z "$line" ] && continue
	bout=$(field "$line" bout)
	[ "$(field "$line" poses)" -ge 6 ] && pass "bout $bout: the crowd showed all $(field "$line" poses) poses" || fail "bout $bout: the crowd showed only $(field "$line" poses) of 6 poses"
	[ "$(field "$line" moves)" -ge 7 ] && pass "bout $bout: the crowd moved in $(field "$line" moves) different ways" || fail "bout $bout: the crowd moved in only $(field "$line" moves) ways"
done <<< "$(grep "KSTEST crowd bout=" "$LOG" 2> /dev/null)"
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
done <<< "$(grep "KSTEST fighter=" "$LOG" 2> /dev/null | sed 's/.*KSTEST/KSTEST/' | sort -u)"
while read -r line; do
	[ -z "$line" ] && continue
	bout=$(field "$line" bout)
	[ "$(field "$line" shown)" -ge 3 ] && pass "bout $bout: the opponent's face changed ($(field "$line" shown) expressions shown)" || fail "bout $bout: the opponent showed only $(field "$line" shown) expression(s)"
	[ "$(field "$line" mouth_opens)" -ge 1 ] && pass "bout $bout: the opponent's mouth moved while speaking ($(field "$line" mouth_opens) times)" || fail "bout $bout: the opponent's mouth never moved"
done <<< "$(grep "KSTEST faces bout=" "$LOG" 2> /dev/null)"
n=$(grep -c "KSTEST voice .*line=intro sound=1" "$LOG" 2> /dev/null); n=${n:-0}
[ "$n" -ge 2 ] && pass "each opponent introduced himself aloud" || fail "only $n spoken introduction(s) in two bouts"
if grep -qE "Fatal error|Assertion failed|Ensure condition failed|Unhandled Exception" "$LOG" 2> /dev/null; then
	fail "the engine reported a crash or failed check"; grep -E "Fatal error|Assertion failed|Ensure condition failed" "$LOG" | head -3
else pass "no crash and no failed engine check"; fi

# The interface. The bug this guards: a picture or font that is not in the build makes the HUD fall back to plain
# boxes and the engine font without a word.
line=$(grep "KSTEST ui pictures=" "$LOG" 2> /dev/null | head -1)
if [ "$(field "$line" missing)" = 0 ] && [ "$(field "$line" pictures)" -ge 29 ] && [ "$(field "$line" fonts)" = 2 ]; then pass "interface art loaded ($(field "$line" pictures) pictures, 2 fonts)"
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

echo "== opening film and start menu (skips and chooses by itself, no camera)"
LOG="$OUT/menu.log"
run_until 1 "KSTEST menu chosen" 90 "$LOG" -KSAuto -KSNoCamera
grep -q "KSTEST ui menu shown background=1 logo=1" "$LOG" 2> /dev/null && pass "the start menu is drawn with its background and logo" || fail "the start menu was not drawn, or its pictures are missing"
line=$(grep "KSTEST intro shown " "$LOG" 2> /dev/null | head -1 | tr -d "\r")
[ -n "$line" ] && [ "$(field "$line" shots)" -ge 1 ] && [ "$(field "$line" shots)" = "$(field "$line" loaded)" ] && pass "the opening film plays: $(field "$line" shots) shots, every sheet loaded" || fail "the opening film is missing pictures (${line:-it was never shown})"
grep -q "KSTEST intro ended" "$LOG" 2> /dev/null && pass "the opening film hands over to the start menu" || fail "the opening film never ended"
grep -q "KSTEST menu chosen=0" "$LOG" 2> /dev/null && pass "the start menu's first line starts the fights" || fail "the start menu did not start the game"

echo "== calibration screen (stepped through automatically, no camera)"
LOG="$OUT/calibration.log"
# -KSCalJitter makes one framing reading flip every frame, like a player standing right on a limit. The bug this
# guards: every line of text on the calibration screen flickered (1594 changes of the hint in one calibration).
run_until 1 "KSTEST calibration done" 120 "$LOG" -KSStart=fight -KSCalDemo -KSCalJitter -KSNoCamera
n=$(grep "KSTEST calibration hint_changes=" "$LOG" 2> /dev/null | head -1 | tr -d "\r" | sed -n 's/.*hint_changes=\([0-9]*\).*/\1/p')
[ -n "$n" ] && [ "$n" -le 40 ] && pass "calibration text is steady with a jittering camera reading ($n changes of the hint in nine steps)" || fail "calibration text flickers: the hint changed ${n:-?} times in nine steps"
grep -q "KSTEST ui calibration text_outside_box=0" "$LOG" 2> /dev/null && pass "calibration writing stays inside its panels" || fail "calibration writing runs out of its panel ($(grep "KSTEST ui calibration" "$LOG" 2> /dev/null | head -1 | sed 's/.*KSTEST/KSTEST/'))"
grep -q "KSTEST calibration done completed=1" "$LOG" 2> /dev/null && pass "all nine calibration steps run and hand over to the first bout" || fail "calibration did not reach its end"

echo "== recorded moves (tests/tracking/*.csv with a .expect file)"
CASES=0
for rec in "$ROOT"/tests/tracking/*.csv; do
	[ -f "$rec" ] || continue
	exp="${rec%.csv}.expect"; [ -f "$exp" ] || continue
	CASES=$((CASES + 1)); name="$(basename "$rec" .csv)"; LOG="$OUT/replay_$name.log"
	run_until 1 "REPLAY done" 90 "$LOG" -KSStart=fight -KSReplay="$rec" -KSReplayExit
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
