# KO-SEKAI — Unreal project

Unreal Engine 5.8 C++ project. Everything in the game is spawned from code (no level files).

The fighters are the seven rigged models in `Content/KoSekai/Characters` (Sergeant Mullet, Channel Zero, Sir Lasercorn,
Aerobics Annihilator, Grandma Thunder, Turbotron, Count Hairspray). Each time the cabinet pulls you in, you are given one
at random and fight a ladder of three other random fighters. Each fighter has 17 animation clips and a few swappable
faces (hurt, knocked out, and more for the two screen-faced ones). The old dummy built from cylinders and spheres is
still there as a fallback for a fighter whose assets are missing, and as the watching crowd.

## Play

Open `KoSekai.uproject` and press Play, or run it directly:

    UnrealEditor KoSekai.uproject -game -windowed

Street: `W A S D` walk, mouse look, `Shift` run, `E` at the cabinet.
Fight: `A/Q/Z` left straight/hook/uppercut, `D/E/C` right, `Space` block, arrows evade and duck, `P` pause.

## Package for Linux

Run `./package_linux.sh`. The build lands in `Packaged/Linux`; start it with `Packaged/Linux/KoSekai.sh`.

## Package for Windows (the jam build)

On a Windows PC with Unreal Engine 5.8 and Visual Studio 2022 installed, run `package_windows.bat`.
The build lands in `Packaged\Windows`; zip that folder as `TeamName_GameName.zip` (under 1 GB) for itch.io.
A Linux install of the engine cannot produce Windows builds.

## Play with the webcam

The tracker runs inside the game. Start the game with a webcam connected and stand so your head, shoulders and hands
are in view; the bottom-right corner goes from `WEBCAM: STEP INTO VIEW` to `HOLD STILL` (stand in guard for a second
while it calibrates) to `WEBCAM TRACKING`. The keyboard keeps working alongside it.

How it works: `KSCameraTracker` reads the camera with the engine's OpenCV plugin, runs MediaPipe's pose model
(`Content/KS/Tracker/pose_lite.onnx`, run by the engine's ONNX Runtime plugin) on a background thread, and
`KSDetector` (a C++ port of `tracking-test/tracker.js`) turns the joints into punches and stances.

Flags: `-KSCamera=1` picks a camera (default: the first of cameras 0 to 3 that gives a picture), `-KSNoCamera` turns the
built-in tracker off.

The browser tracker still works as a fallback when the game has no camera of its own (for example because another
program holds it): run `python3 ../tracking-test/server.py`, open http://127.0.0.1:8765/unreal-bridge.html and press
**Start camera**. Do not use both at once: only one program can hold the camera.

The first time the editor opens this project after the OpenCV plugin was enabled, it downloads the Python packages
that plugin asks for (about 500 MB, into `Intermediate/PipInstall`). That happens once and does not affect packaged builds.

## Where things are

| File | What it does |
| --- | --- |
| `Source/KoSekai/KSGameMode.*` | Game flow, the roster, and the fight rules (a port of `tracking-test/fight.html`) |
| `Source/KoSekai/KSDummy.*` | A fighter: loads the real model, plays its clips and swaps its face (`UseCharacter`, `PlayClip`, `SetFace`); also the fallback dummy |
| `Source/KoSekai/KSTracking.*` | The game's view of the tracker: punches, stances, joints (built-in tracker or browser page) |
| `Source/KoSekai/KSCameraTracker.*` | The built-in tracker: camera, pose model, background thread |
| `Source/KoSekai/KSDetector.*` | Turns poses into punches, blocks, dodges and ducks |
| `Source/KoSekai/KSWorld.*` | Builds the street and the arena |
| `Source/KoSekai/KSHUD.*` | Health bars, cues, subtitles |
| `Source/KoSekai/KSAudio.*` | Synthesised sound effects |
| `Tools/make_materials.py` | Regenerates the two materials in `Content/KS/Materials` |

Test flags: `-KSAuto` plays by itself, `-KSStart=fight` skips to the fights, `-KSFighter=2` forces the first opponent and
`-KSPlayer=0` forces your own fighter (both are roster indices, 0 to 6), `-KSNoGuard` makes the test bot stop defending,
`-KSHitShots` saves a screenshot at the moment each hit lands, `-KSStart=ending`
shows the ending, `-KSShots=3` saves a screenshot every 3 seconds to `Saved/Screenshots`.

If the editor is open while you build from the command line, add `-NoHotReloadFromIDE` to the build command.
