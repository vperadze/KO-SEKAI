# KO-SEKAI — Claude Code instructions

Webcam boxing game for the 2026 Epic AI MegaJam (theme "Director's Cut"). Deadline: **2026-11-16 02:59 CET**.
Packaged build under 1 GB, plus a 30–60 s gameplay video. Windows packaging needs a Windows PC.

## Before starting work

1. Read `docs/RESUME.md`: what works, what is unverified, what is next.
2. Read `docs/LESSONS.md` before touching the area it covers. Every entry there is a bug that already cost time.
3. For webcam or recognition work, read `docs/TRACKING.md`.

## Before building or packaging (always, and first on a fresh clone)

**Run `tools/packs.sh`.** The particle effects come from four free Fab packs that are NOT in the repository (Fab's
licence does not allow them in a public one), so each person adds them to his own copy of the project. If the script
lists a pack as MISSING:

1. **Stop and tell the person, before building anything, exactly which packs are missing**: give each pack's name,
   its publisher and the link, as the script prints them. Do not just mention "some assets are missing".
2. Tell him how to add one: open `unreal/KoSekai.uproject` in the Unreal editor, Window > Fab, sign in, find the pack
   (or add it to the library from the link first), press **Add to Project**, and accept the newest version offered
   even when Fab says it is older than the engine (they load in 5.8; `art/tools/vfx_loadtest.py` checks that).
3. When he says they are in, run `tools/packs.sh` again and go on only when it says "All effect packs are in the project".

Never build a package for anyone to play while a pack is missing: the game builds and runs without them, so nothing
fails, but the special abilities and the hit marks then have no effects at all. Never commit the packs
(`.gitignore` keeps them out). `docs/VFX.md` has the list, what each is used for, and what went wrong with them.

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
| `unreal/Source/KoSekai/` | `KSGameMode` (rules, phases, crowd, special abilities, where the fighters stand), `KSVfx` (particle effects), `KSMic` (hears a shout), `KSDummy` (a fighter), `KSWorld` (street and arena), `KSHUD`, `KSAudio`, `KSTracking` + `KSCameraTracker` + `KSDetector` (webcam). |
| `unreal/Content/KoSekai/Characters/` | Seven fighters, 17 shared clips each plus six of their own (`Fun_A`, `Fun_B`, `Picked`, `Special`, `Win`, `Stagger`), face materials. |
| `unreal/Content/KS/` | Materials, arena pictures and spectator pictures (`Arena/T_KS_Crowd_*`, eight poses each), sounds (`Audio/`), the pose model (`Tracker/`). |
| `unreal/Content/KS/Props/` | The arena's Meshy models, `<Name>/SM_<Name>`: the kit every arena is built from and one landmark per arena. |
| `unreal/Content/KS/UI/` | Interface pictures `T_UI_<name>` and the two fonts (`F_RussoOne`, `F_Bangers`); `KSHUD` falls back to plain boxes without them. |
| `unreal/Content/KS/Voice/` | Spoken lines `V_<Fighter>_<line>`; text and mouth timing are in the generated `KSVoiceData.h`. The ring announcer is `V_Announcer_<line>` (`KSAnnouncerData.h`). |
| `art/` | Source art and the tools that make it (`art/tools`, `art/arena`, `art/tracker-model`). Mostly untracked. |
| `unreal/Source/KoSekai/KSNet.*`, `KSGameModeOnline.cpp` | Online play: the link between two players, the lobby, and the rules of a bout against a person (read the comment at the top of `KSGameModeOnline.cpp` first). |
| `tools/ko_relay.py` | The relay server two players can both join when neither can host (plain Python 3). |
| `tools/check.sh` | The self-check. Runs on Linux and on Windows (Git Bash); it finds the engine itself or takes `UE_ROOT`. |
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
| `-KSStart=select` | Open on the fighter-select screen. |
| `-KSAuto` | The game plays itself. `-KSNoGuard` makes the player take hits. |
| `-KSFighter=N` `-KSPlayer=N` | Force the first opponent / the player's fighter (roster index 0–6). |
| `-KSShots=N` `-KSHitShots` | Save screenshots to `unreal/Saved/Screenshots` (timed / bursts around each hit). |
| `-KSNoCamera` | Do not open the webcam. |
| `-KSNoMic` | Do not open the microphone (it is also left shut with `-nosound`). |
| `-KSMic` | Open the microphone even with `-nosound` (the self-check's recorder test). |
| `-KSCamera=N` | Use camera index N. |
| `-KSReplay=<file>` `-KSReplayExit` | Run the move detector over a recording and log its decisions. |
| `-KSStart=menu` | Skip the opening film. No `-KSStart`: film, then menu. `-KSStart=street`: the old street walk, which nothing in the game leads to any more. |
| `-KSSelectTour` | With `-KSStart=select`: shows every fighter's dance and "picked" move in turn, logs whether he stayed in his part of the picture, and quits. |
| `-KSPortraits` | Photograph every fighter's head to `unreal/Saved/Screenshots/avatar_<Folder>.png` and quit. |
| `-KSGiveUp` | With `-KSAuto`: the bot neither defends, punches nor continues, so the count runs out. |
| `-KSSpam` | With `-KSAuto`: the bot also punches when there is no opening, so the opponent's answers and fakes are run. |
| `-KSEscAt=N` | As if Esc were pressed N seconds into the first bout. |
| `-KSFakeJabSwipe` | On the menu: a fake left jab that is also read as a swipe down, then a right jab (the first line must be chosen). |
| `-KSFakePunch` | During the film: fake webcam hooks and uppercuts for five seconds, then one jab. |
| `-KSArena=N` `-KSArenaTour` | Fight every bout in arena N (0-10, `KSArenaData.h`) / build every arena once at the start. |
| `-KSCalDemo` | Enter calibration without a camera and step through it automatically. |
| `-KSCalJitter` | With `-KSCalDemo`: one framing reading flips every frame, to test that the text stays steady. |
| `-KSVfxTour` | Plays 123 effects of the Fab packs between the fighters in turn, photographs each twice, and quits (`docs/VFX.md`). |
| `-KSSpecials` | The player's special two seconds into the bout, the opponent's three seconds later, then quit. With `-KSHitShots`: five stills of each. |
| `-KSNoRing` | The fighters stay on their two marks, as before they could move about the ring: tells a bug of the movement from any other. |
| `-KSBadgesStay` | Damage badges stay where they were put, as before they slid off a fighter who moves under them (the check's comparison). |
| `-KSMirrorArms` | Experimental: fighter's arms copy the webcam arms directly. Off by default, it looks bad. |
| `-KSFakeArms` | Feed a fixed arm pose into the arm copying, to test it without a person. |
| `-KSHost` `-KSPort=N` | Online: skip the menus and wait for another player (port 7777 unless given). |
| `-KSJoin=<address>` | Online: skip the menus and join a host or a relay server (`name`, `name:port`, `name:port#room`). With `-KSAuto` on both copies, two bots fight each other. |

Always pass `-ResX=1280 -ResY=720` in test runs and restore the window size afterwards: Unreal remembers the
last size and opens the next real session with it.

## Assets added without the editor

The KoSekai editor has no MCP. Import textures and create materials with a headless script:

```
UnrealEditor-Cmd KoSekai.uproject -run=pythonscript -script=<file.py>
```

New music: edit and run `python3 art/tools/music.py` (the fight loop; numpy only, 12 s) or
`python3 art/tools/music_arenas.py` (the ten arena loops, planned in `docs/ARENAS.md`), then `art/tools/import_voice_faces.py`
headless; no rebuild needed.

New spectators or crowd sounds: `art/tracker-model/venv/bin/python art/arena/gen_crowd_real.py quote|run <ceiling> [name]`
(the realistic eight-pose sheets, 2 credits each; `gen_crowd.py` made the old pixel ones) and `gen_crowd_sound.py` (Dreamina, credits), then
`art/tracker-model/venv/bin/python art/arena/slice_crowd.py` (rewrites `KSCrowdData.h`: rebuild) and
`~/ielts-listening/.venv/bin/python art/tools/prep_crowd.py`, then `art/tools/import_crowd.py` headless.

New interface art (menu, bars, badges, bubbles): `python3 art/ui/gen_ui.py quote|run <ceiling> [name]` (Dreamina,
2 credits a picture), `art/tracker-model/venv/bin/python art/ui/cut_ui.py` (rewrites `KSUIData.h`: rebuild), then
`art/ui/import_ui.py` headless. It also imports the fonts (`art/ui/fonts`) and the interface sounds from `prep_audio.py`.

New arena set pieces (stands, truss, lamps, barriers, landmarks; Meshy text-to-3D, 30 credits a model): add a prompt to
`PROPS` in `art/arena/gen_props.py`, `python3 art/arena/gen_props.py quote|run <ceiling> [Name]` (`redo <ceiling> <Name>`
buys one again), look at `art/models/_props/<Name>/thumbnail.png`, `art/tracker-model/venv/bin/python art/tools/slim_props.py [Name]`
(cuts a million triangles down to a few thousand), then `art/tools/import_props.py` headless (log in
`unreal/Saved/import_props.txt`). `KSWorld.cpp` places them by name with `FKSBuilder::Prop`; a
model that faces the wrong way gets a turn in `PropFix`. No rebuild for a model bought again under the same name.

Particle effects for the specials come from free Fab packs under `unreal/Content` (untracked; `docs/VFX.md` lists
them, which are used and what went wrong). A new effect is named in `namespace Vx` in `KSGameMode.cpp` and added to
`Vx::All`; look at it with `-KSVfxTour` or `-KSSpecials` first.

The beam material `M_KS_Beam` (stage lamps and lasers) is made by `art/tools/make_beam_material.py` (headless; no rebuild
needed; it writes `unreal/Saved/beam_material.txt` when it has succeeded). Whatever aims a beam sets its `Tip` and `Axis`.

New announcer lines: `art/voice-model/venv/bin/python art/tools/announcer.py [key ...]` (local models, about two
minutes a line; rewrites `KSAnnouncerData.h`: rebuild), then `art/tools/import_voice_faces.py -KSReimport` headless.

New fighter portraits (select screen): run the game with `-KSPortraits -KSNoCamera -nosound` (photographs of the
models), `art/tracker-model/venv/bin/python art/ui/gen_avatars.py quote|run <ceiling> [Folder]` (Dreamina paints them
from the photographs, 2 credits each), `art/tracker-model/venv/bin/python art/ui/make_avatars.py`, then
`art/ui/import_ui.py` headless.

New fighter moves from Meshy's library: edit the table in `art/tools/add_moves.py`, `python3 art/tools/add_moves.py
quote|run <ceiling>` (3 credits a clip, bought once on Turbotron's rig and copied onto every fighter), then
`art/tools/import_moves.py -KSReimport` headless. It needs no staging editor: the clips go onto each fighter's
existing skeleton. Its log is `unreal/Saved/import_moves.txt`. A new clip name must be added to `ClipNames` in `KSDummy.cpp`.

New faces or lines: `art/tools/bake_faces.py` (faces) and `art/voice-model/venv/bin/python art/tools/voices.py`
(lines; rebuild the game after it, it rewrites `KSVoiceData.h`), then `art/tools/import_voice_faces.py` headless.

Fighters are built in the staging project `~/UnrealProjects/MegaJam2026` (the only editor with MCP, port 8123)
and copied over with `rsync -a --delete` of `Content/KoSekai/Characters`.
