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
run_until 2 "KSTEST bout=" 240 "$LOG" -KSStart=fight -KSAuto -KSNoCamera
BOUTS=$(grep -c "KSTEST bout=" "$LOG" 2> /dev/null); BOUTS=${BOUTS:-0}
[ "$BOUTS" -ge 2 ] && pass "two bouts were fought to the end" || fail "only $BOUTS bout(s) finished in 240 s"
field() { echo "$1" | tr -d "\r" | sed -n "s/.*$2=\([^ ]*\).*/\1/p"; }
while read -r line; do
	[ -z "$line" ] && continue
	bout=$(field "$line" bout); name=$(echo "$line" | sed -n 's/.*opponent=\(.*\) result=.*/\1/p')
	[ "$(field "$line" opp_hand_travel)" -ge 20 ] && pass "bout $bout: the opponent ($name) moves" || fail "bout $bout: the opponent ($name) is frozen in one pose"
	[ "$(field "$line" player_hand_travel)" -ge 20 ] && pass "bout $bout: the player's fighter moves" || fail "bout $bout: the player's fighter is frozen in one pose"
	[ "$(field "$line" spectators)" -eq 8 ] && pass "bout $bout: eight spectators at ringside" || fail "bout $bout: $(field "$line" spectators) spectators, expected 8"
done <<< "$(grep "KSTEST bout=" "$LOG" 2> /dev/null)"
LAST=$(grep "KSTEST bout=" "$LOG" 2> /dev/null | tail -1)
[ -n "$LAST" ] && [ "$(field "$LAST" landed)" -gt 0 ] && pass "punches land" || fail "no punch landed"
if grep -qE "Fatal error|Assertion failed|Ensure condition failed|Unhandled Exception" "$LOG" 2> /dev/null; then
	fail "the engine reported a crash or failed check"; grep -E "Fatal error|Assertion failed|Ensure condition failed" "$LOG" | head -3
else pass "no crash and no failed engine check"; fi

echo "== calibration screen (stepped through automatically, no camera)"
LOG="$OUT/calibration.log"
run_until 1 "KSTEST calibration done" 120 "$LOG" -KSStart=fight -KSCalDemo -KSNoCamera
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
