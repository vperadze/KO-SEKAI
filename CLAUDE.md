# KO-SEKAI — Claude Code instructions

Webcam boxing game for the 2026 Epic AI MegaJam (theme "Director's Cut"). Deadline: **2026-11-16 02:59 CET**.
Packaged build under 1 GB, plus a 30–60 s gameplay video. Windows packaging needs a Windows PC.

## Before starting work

1. Read `docs/RESUME.md`: what works, what is unverified, what is next.
2. Read `docs/LESSONS.md` before touching the area it covers. Every entry there is a bug that already cost time.
3. For webcam or recognition work, read `docs/TRACKING.md`.

## After every code change (always)

1. **Run `tools/check.sh`** and report its real output. It builds, plays two bouts by itself and replays the
   recorded move sessions. Do not report a change as done on a build alone.
2. **A new bug gets a check.** If the player found a bug the check did not catch, add a check for it in the same
   change as the fix, and see it fail before the fix where that is practical.
3. **A new lesson gets a line** in `docs/LESSONS.md` (what happened, the cause, the rule).
4. **Update `docs/RESUME.md`** when the state changes: something now works, something is newly unverified.

## Hard rules

- **Never run the game or the check while the player is playing.** A second copy halves the tracker's frame
  rate and the player experiences it as lag. `tools/check.sh` refuses to start if the game is open; do the same
  for ad-hoc test runs (`pgrep -x UnrealEditor`; a `pgrep -f` pattern matches the shell that runs it).
- **The editor only loads rebuilt code after a restart.** After a build, say so. A player testing in an editor
  that was opened before the build is testing the old code.
- **Say what was not verified.** Stills from a self-playing fight show poses, not feel. Webcam behaviour with a
  real person cannot be checked without the person.
- **Ask before spending credits** (Meshy, Dreamina), and quote the estimate first. Both CLIs have a dry run.
- **Do not commit `unreal/Config/DefaultEngine.ini` wholesale.** It carries someone else's uncommitted Android
  token change; commit only the hunks that are ours.
- **Recordings of a person stay local.** `tests/tracking/*.csv` and `unreal/Saved/KSTrack/` are joint positions of
  the player's body; they are git-ignored on purpose.

## Layout

| Path | What |
|---|---|
| `unreal/` | The game. C++ only, everything spawned from code, no level files. |
| `unreal/Source/KoSekai/` | `KSGameMode` (rules, phases, crowd), `KSDummy` (a fighter), `KSWorld` (street and arena), `KSHUD`, `KSAudio`, `KSTracking` + `KSCameraTracker` + `KSDetector` (webcam). |
| `unreal/Content/KoSekai/Characters/` | Seven fighters, 17 shared clips each, face materials. |
| `unreal/Content/KS/` | Materials, arena pictures and spectator sprites (`Arena/`), the pose model (`Tracker/`). |
| `unreal/Content/KS/Voice/` | Spoken lines `V_<Fighter>_<line>`; text and mouth timing are in the generated `KSVoiceData.h`. |
| `art/` | Source art and the tools that make it (`art/tools`, `art/arena`, `art/tracker-model`). Mostly untracked. |
| `tools/check.sh` | The self-check. |
| `tests/tracking/` | Recorded move sessions with expectations. |
| `tracking-test/` | The older browser tracker. The game no longer needs it. |

## Build and run

```
~/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh KoSekaiEditor Linux Development \
    -project=$HOME/ko-sekai/unreal/KoSekai.uproject -NoHotReloadFromIDE
~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor ~/ko-sekai/unreal/KoSekai.uproject -game -windowed -KSStart=fight
```

Test flags (all optional):

| Flag | Effect |
|---|---|
| `-KSStart=fight` | Skip the street and start in the ring. |
| `-KSAuto` | The game plays itself. `-KSNoGuard` makes the player take hits. |
| `-KSFighter=N` `-KSPlayer=N` | Force the first opponent / the player's fighter (roster index 0–6). |
| `-KSShots=N` `-KSHitShots` | Save screenshots to `unreal/Saved/Screenshots` (timed / bursts around each hit). |
| `-KSNoCamera` | Do not open the webcam. |
| `-KSCamera=N` | Use camera index N. |
| `-KSReplay=<file>` `-KSReplayExit` | Run the move detector over a recording and log its decisions. |
| `-KSNoCal` | Skip the calibration screen (webcam players get it before the first bout). |
| `-KSCalDemo` | Enter calibration without a camera and step through it automatically. |
| `-KSMirrorArms` | Experimental: fighter's arms copy the webcam arms directly. Off by default, it looks bad. |
| `-KSFakeArms` | Feed a fixed arm pose into the arm copying, to test it without a person. |

Always pass `-ResX=1280 -ResY=720` in test runs and restore the window size afterwards: Unreal remembers the
last size and opens the next real session with it.

## Assets added without the editor

The KoSekai editor has no MCP. Import textures and create materials with a headless script:

```
UnrealEditor-Cmd KoSekai.uproject -run=pythonscript -script=<file.py>
```

New faces or lines: `art/tools/bake_faces.py` (faces) and `~/ielts-listening/.venv/bin/python art/tools/voices.py`
(lines; rebuild the game after it, it rewrites `KSVoiceData.h`), then `art/tools/import_voice_faces.py` headless.

Fighters are built in the staging project `~/UnrealProjects/MegaJam2026` (the only editor with MCP, port 8123)
and copied over with `rsync -a --delete` of `Content/KoSekai/Characters`.
