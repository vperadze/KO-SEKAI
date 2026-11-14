#!/bin/bash
# Stills for the jam page, saved by the game itself (lossless, drawn at twice the window's size and scaled down by
# the engine, so no video coding and no stretching): bursts of four around every hit of a self-played bout.
#     art/trailer/stills.sh        -> unreal/Saved/Screenshots/<arena>_hit_*.png; pick from them into art/trailer/shots
# Refuses to start while the game is open. Puts the remembered window size back afterwards.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EDITOR="${UE_ROOT:-$HOME/UnrealEngine}/Engine/Binaries/Linux/UnrealEditor"
SETTINGS="$ROOT/unreal/Saved/Config/LinuxEditor/GameUserSettings.ini"
SHOTS="$ROOT/unreal/Saved/Screenshots"
if pgrep -x UnrealEditor > /dev/null; then echo "the game or the editor is open"; exit 1; fi
[ -f "$SETTINGS" ] && cp "$SETTINGS" /tmp/ks_stills_settings.keep
trap '[ -f /tmp/ks_stills_settings.keep ] && cp /tmp/ks_stills_settings.keep "$SETTINGS"' EXIT
bout() {   # name, seconds, flags
	local name="$1" sec="$2"; shift 2
	rm -f "$SHOTS"/hit_*.png
	timeout "$sec" "$EDITOR" "$ROOT/unreal/KoSekai.uproject" -game -windowed -ResX=1600 -ResY=900 -WinX=0 -WinY=0 -unattended -nosound \
		-KSNoCamera -KSNoMic -KSStart=fight -KSAuto -KSHitShots -ExecCmds="r.ScreenPercentage 200" "$@" > /dev/null 2>&1
	sleep 2; pkill -9 -x UnrealEditor 2>/dev/null
	for f in "$SHOTS"/hit_*.png; do [ -f "$f" ] && mv "$f" "$SHOTS/${name}_$(basename "$f")"; done
	echo "$name: $(ls "$SHOTS" | grep -c "^${name}_hit_") stills"
}
bout space   75 -KSArena=1 -KSPlayer=5 -KSFighter=6
bout castle  75 -KSArena=6 -KSPlayer=3 -KSFighter=2
bout rooftop 75 -KSArena=4 -KSPlayer=0 -KSFighter=1
bout beach   75 -KSArena=2 -KSPlayer=4 -KSFighter=5
