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

1. Start the tracker server: `python3 ../tracking-test/server.py`
2. Open http://127.0.0.1:8765/unreal-bridge.html, press **Start camera**, and keep that window visible.
3. Start the game. The bottom-right corner shows `WEBCAM TRACKING` once it has you.

The bridge page runs `tracker.js` and posts each frame to the game on port 8770: punches, held stances and the
body landmarks. The player's dummy copies your upper body; the keyboard keeps working alongside it.

## Where things are

| File | What it does |
| --- | --- |
| `Source/KoSekai/KSGameMode.*` | Game flow, the roster, and the fight rules (a port of `tracking-test/fight.html`) |
| `Source/KoSekai/KSDummy.*` | A fighter: loads the real model, plays its clips and swaps its face (`UseCharacter`, `PlayClip`, `SetFace`); also the fallback dummy |
| `Source/KoSekai/KSTracking.*` | Receives the webcam tracker over local HTTP |
| `Source/KoSekai/KSWorld.*` | Builds the street and the arena |
| `Source/KoSekai/KSHUD.*` | Health bars, cues, subtitles |
| `Source/KoSekai/KSAudio.*` | Synthesised sound effects |
| `Tools/make_materials.py` | Regenerates the two materials in `Content/KS/Materials` |

Test flags: `-KSAuto` plays by itself, `-KSStart=fight` skips to the fights, `-KSFighter=2` forces the first opponent and
`-KSPlayer=0` forces your own fighter (both are roster indices, 0 to 6), `-KSNoGuard` makes the test bot stop defending,
`-KSHitShots` saves a screenshot at the moment each hit lands, `-KSStart=ending`
shows the ending, `-KSShots=3` saves a screenshot every 3 seconds to `Saved/Screenshots`.

If the editor is open while you build from the command line, add `-NoHotReloadFromIDE` to the build command.
