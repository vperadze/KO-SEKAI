#!/bin/bash
# Records the game's window with its sound into art/trailer/raw/<name>.mkv while the game plays itself.
#
#     art/trailer/record.sh <name> <seconds> [game flags ...]
#     e.g. art/trailer/record.sh bout_volcano 80 -KSStart=fight -KSAuto -KSArena=9 -KSPlayer=0 -KSFighter=4
#
# The picture is taken from the game's window alone (gstreamer's ximagesrc), the sound from the loudspeaker's
# monitor, so anything else that plays on the machine during a take is in it too. Refuses to start while the game
# is open (CLAUDE.md: a second copy halves the tracker's frame rate). Puts the remembered window size back afterwards.
# KS_RES=1280x720 records smaller (default 1600x900: the desktop here leaves 1854x1168 for a window). The webcam and the microphone are kept shut.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NAME="$1"; SECONDS_="$2"; shift 2
RES="${KS_RES:-1600x900}"; W="${RES%x*}"; H="${RES#*x}"
EDITOR="${UE_ROOT:-$HOME/UnrealEngine}/Engine/Binaries/Linux/UnrealEditor"
SETTINGS="$ROOT/unreal/Saved/Config/LinuxEditor/GameUserSettings.ini"
OUT="$ROOT/art/trailer/raw"; mkdir -p "$OUT"
LOG="$OUT/$NAME.log"

if pgrep -x UnrealEditor > /dev/null; then echo "the game or the editor is open: not recording"; exit 1; fi
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$OUT/.GameUserSettings.keep"
GAME=""
finish() { [ -n "$GAME" ] && { kill "$GAME" 2>/dev/null; sleep 2; kill -9 "$GAME" 2>/dev/null; }
           [ -f "$OUT/.GameUserSettings.keep" ] && cp "$OUT/.GameUserSettings.keep" "$SETTINGS"; }
trap finish EXIT

"$EDITOR" "$ROOT/unreal/KoSekai.uproject" -game -windowed -ResX="$W" -ResY="$H" -WinX=0 -WinY=0 -unattended \
	-KSNoCamera -KSNoMic -abslog="$LOG" "$@" > /dev/null 2>&1 &
GAME=$!

XID=""
for _ in $(seq 1 240); do      # the splash window comes first and goes again: wait for one of the game's size
	for x in $(xdotool search --onlyvisible --pid "$GAME" 2>/dev/null) $(xdotool search --onlyvisible --name 'KoSekai' 2>/dev/null); do
		xdotool getwindowgeometry "$x" 2>/dev/null | grep -q "Geometry: ${W}x${H}" && XID="$x"
	done
	[ -n "$XID" ] && break
	sleep 0.5
done
if [ -z "$XID" ]; then echo "no game window of $RES"; exit 1; fi
sleep "${KS_LEAD:-4}"

SINK="$(wpctl inspect @DEFAULT_AUDIO_SINK@ | sed -n 's/.*node.name = "\(.*\)"/\1/p')"
xdotool windowactivate "$XID" 2>/dev/null
timeout -s INT "$SECONDS_" gst-launch-1.0 -q -e \
	ximagesrc xid="$XID" use-damage=false show-pointer=false ! video/x-raw,framerate=30/1 ! videoconvert ! video/x-raw,format=I420 ! queue max-size-buffers=120 \
	! x264enc speed-preset=veryfast pass=quant quantizer=19 key-int-max=30 ! queue ! mux. \
	pulsesrc device="$SINK.monitor" ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2 ! avenc_aac bitrate=256000 ! queue ! mux. \
	matroskamux name=mux ! filesink location="$OUT/$NAME.mkv" &
REC=$!
# A game that quits by itself (-KSSpecials, -KSSelectTour) ends the take: the recorder films that part of the
# screen, and would go on to film whatever lies under the window that has gone.
while kill -0 "$REC" 2>/dev/null; do
	kill -0 "$GAME" 2>/dev/null || { pkill -INT -P "$REC" gst-launch-1.0; kill -INT "$REC" 2>/dev/null; break; }
	sleep 0.2
done
wait "$REC" 2>/dev/null
ls -la "$OUT/$NAME.mkv"
