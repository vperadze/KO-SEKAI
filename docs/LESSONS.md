# Lessons

One entry per bug that cost time: what happened, the cause, the rule. Add to it in the same change as the fix.

## Webcam and recognition

- **Player too close: recognition cannot work.** In a recorded session the shoulders sat at 65% of the picture
  height, the elbows on the bottom edge, the fists below the picture a third of the time. Rule: check framing
  before tuning anything. The game shows STEP BACK when shoulders are below 55% or elbows below 97%.
- **Punches called only when the arm stopped.** The detector judged a stroke at its end, so the fighter started
  after the player had finished. Rule: call the punch on the way out as soon as one kind clearly leads; keep the
  end-of-stroke verdict as the fallback.
- **Block judged against the calibration pose.** A player who calibrated with hands up could never block. Rule:
  stances that the player holds deliberately are judged absolutely (fists at face height), not relative to
  calibration.
- **Compressed camera frames arrive black.** Unreal's OpenCV has no JPEG decoder, so `CAP_PROP_FOURCC = MJPG`
  gives near-black frames (brightness 0.02) and `imread` of a JPEG fails. Rule: leave the format alone (YUYV,
  30 frames/s at 640×480); use BMP for test stills. `tools/check.sh --camera` checks brightness.
- **`CAP_PROP_BUFFERSIZE = 1` halves the camera rate** (15 frames/s). Two buffers keep 30.
- **The pose model needs a hip-centred view.** A whole-frame first look scored 0.004 on a player whose hips were
  below the picture; re-centred, 0.99. Rule: when searching, cycle starting views and follow the model's own hip
  guess.
- **The ONNX runtime's default thread pool fights the engine.** In the editor target it shares one pool across
  all cores: 30–70 ms per run instead of about 5. Rule: two dedicated threads, set in `DefaultEngine.ini`
  (`[/Script/NNERuntimeORT.NNERuntimeORTSettings]`).
- **Copying the player's arms onto the fighter looks bad and never hits.** One webcam cannot measure depth, and
  copied arms have no aim. Rule: the webcam recognises the move; the fighter plays the authored, aimed clip.

## Fighters and animation

- **Second opponent frozen in one pose.** A second fighter's mesh on the same actor left the drawn (posed) copy
  not following the animation, while the fight logic ran on. Rule: spawn a fresh actor per bout. The check
  measures how far each fighter's drawn hand travels per bout.
- **Tracked fighter invisible.** A poseable mesh takes its bounds from the unscaled asset; the rigs are in metres
  with a 0.01 bone scale, so the bounds were 1 cm and the mesh was culled. Rule: `SetBoundsScale(400)` on it.
- **Clip changes snapped.** Every punch clip starts from guard. Rule: ease from the pose on screen into the new
  clip and carry each bone's speed for a few hundredths of a second (`AKSDummy::PlayClip`, `BlendFrom`).
- **Legs frozen, feet skating.** The clips are standing punches and the body is slid by the punch aim and recoil.
  Rule: legs are driven in code on the drawn body (`Footwork`), not by the clips; switch it off for clips that
  move the legs themselves (knock-down, victory).
- **Winner squashed.** The loser kept aiming a punch after the knock-out. Rule: aim only during the Fight phase.
- **Smeared textures.** The 0.01 bone scale makes texture streaming pick tiny mips. Rule: `never_stream` on every
  fighter texture.
- **Faceted shading.** Meshy's FBX has flat normals. Rule: import the GLB.
- **Rubber-hose arms.** 100 passes of skin-weight smoothing was far too many. Rule: 5 passes.
- **Gloves lost.** `image-to-3d --pose-mode t-pose` drops them. Rule: `--pose-mode auto`.
- **Faces never changed, and the Count's mouth hung open all fight.** Most fighters had only a `hurt` and a `ko`
  face, shown below half health or on a knockout, and their painted default face was whatever expression Meshy
  gave the model (a shout, for the Count). Rule: every painted fighter has `calm`, `talk`, `angry`, `blink` and
  `smug`; the face is chosen every frame in `AKSDummy::UpdateFace`; the check counts the expressions shown per bout.
- **The mouth must follow the dry voice, not the finished sound.** An echo or hall effect keeps the level up after
  the word has ended, so a mouth driven by the final file flaps in the silence. `voices.py` keeps the voice before
  its effect (`.npy` beside the `.wav`) and reads the mouth from that.

## Process

- **My test runs slowed the player's session** (tracker at 9–25 frames/s instead of 30). Rule: never run the game
  or a build while the player is playing.
- **The player tested old code.** The editor had been open since before the build. Rule: after every build, say
  that the game or editor must be restarted.
- **A test run shrank the player's window.** A 640×360 replay run was remembered in `GameUserSettings.ini`. Rule:
  the check saves and restores that file.
- **"The game lacks sound and shake" was wrong.** Both existed; every test had run with `-nosound`. Rule: read the
  code before saying a feature is missing.
- **Meshy cannot make custom transition clips.** It animates from a fixed library only.
- **A Meshy image edit cost 12 credits, not the 9 that older tasks in `meshy image-to-image list` show.** The
  estimate given to the player (225) was 75 short. Rule: quote from a dry run or one paid test, not from history.
- **A guard that only prints is not a guard.** `pgrep -x UnrealEditor; <run>` ran while a game was open, and
  `pgrep -f "Build.sh KoSekai"` matched the shell that ran it. Rule: `if pgrep -x UnrealEditor >/dev/null; then
  exit; fi` before any run, and write `-f` patterns as `"[B]uild.sh"`.
- **Two Claude sessions worked in this folder at once.** Uncommitted changes appeared in files already read.
  Rule: check `git status` against the start of the session before editing, message the other session
  (ListAgents / SendMessage), and commit only your own hunks.

- **`tools/check.sh` could not run on the Windows PC.** Git for Windows checked every text file out with CRLF line
  endings, which bash refuses in a script, and the check itself knew only Linux paths and `pgrep`. Rule:
  `.gitattributes` keeps `*.sh` on LF; the check picks its engine path, build script and "is the game open" test by
  system, and hands the game paths through `native()`.

- **The first voices were "too robotic" and the lines "too much text".** Ring modulation, bit crushing and 15%
  pitch shifts on a speech model sound like a machine, and a 15-word taunt does not fit between punches. Rule:
  natural voices, pitch within 6%, lines of six words or fewer; the player wants hard swearing, not mild.

- **The natural voices were still "speech to text, no emotions".** Kokoro reads every line in the same even tone;
  punctuation, capitals and speed do not make it act. Rule: a speech model without an emotion control cannot shout.
  The lines are now acted by Chatterbox (emotion setting per kind of line), copying each fighter's Kokoro voice.
- **An acting speech model babbles on very short lines.** Chatterbox said "Your tretch, Magit?" for "You're trash,
  maggot!" in one take of three, and never managed the two-word "Error! Fuck!". Nobody listens to 168 takes. Rule:
  record several takes, have a speech recogniser (faster-whisper) write each down, keep one that says the line,
  and give the model at least four or five words.

- **A crash on the second walk into the arena: `static UStaticMesh* Cone = LoadObject(...)`.** The player won all
  three bouts, went back to the cabinet and the game died in `SetStaticMesh`. A raw pointer in a static keeps
  nothing alive: on the street no actor used the cone, the garbage collector freed it, and the static still
  pointed there. No test walked in twice. Rule: an asset cached in a static is rooted (`Keep()` in `KSWorld.cpp`,
  `AddToRoot` in `KSAudio.cpp`); `tools/check.sh` greps the source for the bare pattern.
- **Dreamina refuses some video prompts with a bare number.** Two of six film shots failed with error 20114 and
  20115 and no text: suits slamming a stamp and pulling a lever, and a man grabbed and dragged by electricity.
  Reworded without people in trouble (machines going dark; the camera flying into the screen) both passed.
  Whether the failed tries were charged is not shown. Rule: keep harm to people out of video prompts.
- **The film is not played as video.** There is no ffmpeg here and Unreal's players differ per platform, with the
  Windows build untestable from this machine. Each shot is a 4096 sheet of 640x360 frames at 12 a second, drawn
  by the HUD: the same code everywhere, and the roughness passes for VHS. Cost: 96 MB in `Content/KS/Cine`.
- **Dreamina hands back PNG data in a file called `.jpg`.** Unreal chooses its importer by the file name and the
  import failed with nothing but "could not be found in the Asset Registry". Rule: look at the first bytes and
  hand the file over under its true type (`art/tools/import_arenas.py`).
- **A painted backdrop on the arena wall was invisible.** The fight camera sits low over the player's shoulder and
  looks down: it never sees the wall above 4 m, and the crowd fills what it does see. Ten paid backdrops showed
  as a strip of colour. Rule: look at a still from the real camera before making art for a place; the backdrops
  are now sunk so their horizon is at the floor, and each bout's intro opens with a wide shot of the arena.

- **"Same style" for music means the same recipe, not the same synth.** Asked for ten more tracks "same way, same
  style" for different arenas, the first set gave each arena its own genre (beach steel pan, bar shuffle, funk).
  The player kept only the one that happened to share the fight track's recipe. Rule: when a result is approved,
  vary what was not noticed (key, tempo, melody) and keep what makes it that result (beat, bass, instruments).

- **Dreamina's canvas CLI has limits the first crowd run hit one after another.** A request takes at most 20 nodes;
  a run above roughly 100 credits is refused until `node confirm --credit-ceiling` gives a token for
  `node run --credit-token`; `resource download` needs `--project-id` when the canvas is not the current one (it
  fails without a word otherwise); "temporarily unavailable" on `node create` is common and safe to retry, a `run`
  is not. Rule: `art/arena/gen_crowd.py` does all four; copy it for the next batch job.
- **Dreamina's audio tool only speaks (text to speech).** Crowd noise came from the soundtrack of short videos
  (seedance 2.0 mini, 720p, 5 credits a second), with the sounds and the pauses between them spelled out in the
  prompt, then cut by the loudness curve (`art/tools/prep_crowd.py`). There is no ffmpeg on this machine; PyAV in
  `~/ielts-listening/.venv` reads the mp4.
- **Keying sprites by "is it green" leaves the shadow and eats green clothes.** The sheets come with a darker green
  blob under the feet and white shock lines. Rule (`art/arena/slice_crowd.py`): green that touches the backdrop is
  backdrop, green shut inside an outline stays; loose specks outside the body are dropped. Look at
  `art/arena/crowd_contact_*.jpg` after slicing: one sheet in fifty came back with the wrong number of figures.
- **A slow line in a one-off script is still slow.** `np.convolve` with a half-second window over ten seconds of
  audio ran for minutes. Rule: running sums (`cumsum`) for moving averages.

- **The film stopped whenever the player moved** (player report, 2026-11-13). Every screen took "any punch the
  webcam called" as a confirm, and a person settling in front of the camera sets off the odd hook or uppercut.
  Rule: a screen that must not be left by accident asks for one particular move (the film: a jab), not for any
  event; the check feeds fake hooks and uppercuts and then a jab (`-KSFakePunch`).
- **Game over put the player on the old street** (player report, 2026-11-13). The street walk was taken off the
  menu, but two exits still led there: the CONTINUE? count running out (`EnterStreet(false)`) and the end of the
  ladder (`EnterStreet(true)`, the cabinet scene). Only the second was known, hence "sometimes". Rule: when a place
  is retired, grep for every way into it; the check now counts the calls to `EnterStreet` and plays a lost bout to
  the end of the count (`-KSGiveUp`).
- **Two- and three-word announcer lines came out as babble** ("Round one! Fight!" as "Raggle one" and "Ratman
  two"), as the fighters' short lines had. Rule, again: give Chatterbox five words ("Round number one! Fight!").
  The recogniser also writes "Round 1" for "round one" and "Laser Corn" for "Lasercorn": compare letters, not words.

- **New Meshy clips do not need the staging editor.** Every Meshy rig has the same 26 bones, so one bought clip is
  copied onto all seven fighters (`transplant_anim.py`), and Interchange imports animations alone onto a fighter's
  existing skeleton from a headless script (`import_only_animations` + `skeleton` on an override pipeline). With one
  clip in the file the asset is named after the file, with several after the clip (`extraFun_A`).
- **`unreal.log` from a headless script does not reach the terminal.** Three import runs looked as if they had done
  nothing. Rule: an import script also writes its lines to a file in `unreal/Saved`.
- **Unreal cannot hear a microphone on Linux.** `AudioCaptureCore` says "No Audio Capture implementations found":
  the RtAudio back end is built without RtAudio there, and on any platform the back end module has to be loaded by
  name first. Windows has `AudioCaptureWasapi`; on Linux `KSMic` reads `arecord` through a pipe.
- **An effect aimed from one fighter at the other is hidden from the fight camera**, which sits behind the player:
  his own body covers it. And a glow of 20 or more burns every colour to white. Rule: specials are shown from the
  side, and coloured pieces glow at 3 to 7.

- **Moving fighters looked "pixelated, bad texture"** (player report, 2026-11-13). Not the textures: the engine's
  default anti-aliasing (TSR) blends each frame with the last and needs motion vectors, and the fighters' drawn
  bodies are poseable meshes that give none. Still fighters were sharp, dancing ones dissolved into noise (seen in a
  side-by-side of the same moments). Rule: the game sets `r.AntiAliasingMethod` to 1 (FXAA) in `BeginPlay`; judge
  picture quality from stills of something moving, not of the stance.
- **Meshy's dances walk.** The hips travel a metre and more, so fighters left the picture on the select screen, and
  the Stagger clip backed off 1.6 m and snapped home. Rule: `add_moves.py` takes the slow travel out of the hips
  (moving average over 1.2 s) and keeps a quarter of the stagger's step.
- **Alternating idle clips with the stance in between read as "stopping and starting".** Rule: one clip, looped.

## Interface art

- **Badges and speech bubbles landed on the fighters, and calibration text ran out of its panel.** Both were placed at
  fixed spots or fixed offsets from a head, with fixed text sizes, so nothing knew where the fighters or the panel's
  rim were. Rule: anything that pops up goes through the HUD's placer (`BeginLayout`, `Reserve`, `FindSpot`: it knows
  both fighters' screen boxes from their bones and keeps a badge where it was first put); anything written in a box
  passes a `MaxW` or goes through `Lines`, and a panel's contents stay inside its rim (`RimX`, `RimY`).
- **Unreal's font importer crashes without a window** (`CurrentApplication.IsValid()` in a headless `pythonscript` run),
  and it took the rest of the import script down with it. Rule: fonts are plain `.ttf` files in
  `Content/KS/UI/Fonts`, read by `KSHUD` at run time and staged by `DefaultGame.ini`; never import them as assets.
- **A paid Dreamina run was nearly lost: `resource download` refuses to write over an existing file**, so the
  script stopped after the run had been charged. Rule: download to a new name and move it into place
  (`gen_ui.py: download`); a finished node's picture can always be fetched again with `node show`, never by a rerun.
- **Style words get drawn.** "Like the painted marquee of an arcade cabinet" gave three titles sitting on arcade
  cabinets, and "comic sound-effect art" put a made-up word over a plain glove. Rule: the prompt names only what
  should be in the picture, and says what must not be behind it (no plate, no sign, no machine).
- **Letters have holes.** Green inside an O is not connected to the backdrop, so the first cut-out left it green.
  Rule: enclosed green above a small size is backdrop too (`cut_ui.py: background`).
- **Stage lamps pointed at the ceiling.** `ASpotLight` carries its light component tilted 90 degrees down, so
  rotating the actor towards a target aims the light a quarter turn off. Rule: aim with
  `SpotLightComponent->SetWorldRotation`, never the actor's rotation.
- **Haze turned the arena milky.** In volumetric fog every bright lamp lights the air and the floor; the first
  version washed the neon out. Rule: lamps meant as beams get few candelas and a large
  `VolumetricScatteringIntensity`; the ring's own lights get a scattering near zero; fog albedo stays dark.
- **`pgrep -f "[B]uild.sh"` matched its own shell again**, because the same command line went on to call
  `Build.sh`. Rule: guard with `pgrep -x UnrealEditor` only; ask the other sessions about builds.
- **A build ran while another session was writing source files**, and one build died with a compiler bus error
  for no reason in the code. Rule: agree on a build turn by message first; a bus error is a retry, not a bug.
- **The arena's lamps were dark and aimed at the floor for five builds.** A scripted text replace added a `//`
  comment in the middle of a line, which commented out the two assignments after it (`Beam.Aim`, `Beam.Candelas`).
  It compiled, the check passed, and the stills were blamed on the fog, the material and the cone mesh in turn.
  Rule: a comment goes on its own line; and when something is invisible, log its real position and direction
  before changing how it is drawn.
- **`pgrep -x UnrealEditor` does not see `UnrealEditor-Cmd`**, and `UnrealEditor-Cmd --version` starts a whole
  editor that stays running. Rule: check headless runs with `pgrep -f "[U]nrealEditor-Cmd"`; never probe the
  binary with made-up flags.
- **The calibration screen's text blinked** (player report, 2026-11-13). The hint line and the OK/NO checklist were
  recomputed every frame from raw landmarks, so a player standing near a limit, or lost by the tracker for a frame,
  saw the words flip several times a second (1594 hint changes in one calibration, reproduced with
  `-KSCalDemo -KSCalJitter`). Rule: nothing a person has to read is driven straight by a per-frame camera reading;
  it goes through `Steady()` in `TickCalibrate` (a third of a second to change, with a margin on the limit).

- **A reference picture carries its style with it.** Asked to redraw the pixel spectators "realistic, like the
  fighters" with each old sprite as the reference, Dreamina returned the same pixel dolls with shading (4 credits).
  From the written description alone the style was right at once. Rule: a reference is for what must stay the
  same; when the look is what must change, describe the character in words and leave the picture out.
- **"Whole body" needs the room to be asked for.** Eight tall people on a 16:9 sheet came back cut off at the knee;
  on 4:3 (taller cells) with "the camera stands far enough back, empty space above the head and below the feet" they
  fit. Saying "each figure is small" instead made them cute and big-headed: size words are style words too.
- **A camera that leaves the player's back changes what "left" means.** The cameraman may go behind the opponent
  only while the player has nothing to dodge (opponent open or down, bout over); when a punch is wound up and he is
  out at the side he runs back behind a shoulder. The check counts the frames in which he lost the opponent's head.
- **Flat spectators and a moving camera.** Pictures fixed towards the ring centre turn into lines when seen from
  the side. They now turn on their feet to face the camera every frame (`TickCrowd`).
- **The webcam's light stayed on after quitting** (player report, 2026-11-13). The microphone's `arecord` was started
  straight from the game and inherited every file the game had open: the webcam, and the reading end of its own
  pipe, so it never got the broken-pipe signal and outlived the game (two were found, one 37 minutes old, ignoring
  SIGTERM). The only test had ended the game politely. Rule: a helper process is started through a shell that
  closes every file above the three standard streams first (`KSMic.cpp`), and is tested by killing the game with
  -9 and looking at what the helper still holds (`tools/check.sh`, "microphone recorder", flag `-KSMic`).
- **Unreal's `CreateProc` splits its argument string on double quotes only.** A `bash -c '...'` script in single
  quotes arrived as loose words and the recorder never started, while the game still reported the microphone open.
  Rule: wrap the script in double quotes and keep quotes and backslashes out of it (`exec {n}>&-` closes the file
  whose number is in `n` without `eval`).
- **The film's skip hint read "jab, enter, jab, enter"** (player report, 2026-11-13). It blinked on a timer and chose
  its words by whether the tracker saw a body that frame. The same rule as for calibration: nothing to be read is
  driven by a per-frame camera reading. It is steady now and worded by whether a camera is connected.
- **A description can add up to somebody else's character.** "A plumber in dungarees and a cap with a big moustache"
  came back as a famous game mascot, "a home-made superhero costume with a cape" as a famous hero, and a baseball
  player in stripes wore a real team's letters. Rule: look at every generated character for likenesses and logos
  before it goes in; reword with what sets it apart (bald, red beard, grey overalls; "no letters, no logo").
- **A figure taller than its cell cannot be sliced.** "An extremely tall" basketball player twice ran his legs
  through the row below. Rule: giants are drawn at normal proportions and made big by the game (`EKSBuild::Giant`).
- **A camera aimed at where a fighter is put down loses him when he dances** (player report, 2026-11-13: on the select
  screen Turbotron's breakdance carried him out of the picture, and the title band cut Count Hairspray's hair). The
  select camera looked at a fixed point with a fixed lens, and the only test showed one fighter standing. Three rules.
  A camera that must hold a fighter frames `AKSDummy::BodyBox()` (bones, the flesh round them, and the hair above the
  face from the `KSHeadRooms` table), not a point. What the interface covers and what the camera frames come from the
  same numbers (`KSSelectFrame`). And every fighter in the roster is toured by the check (`-KSSelectTour`), which also
  measures each model's hair and fails when a fighter is missing from the table: that is what catches a new character.
- **A dance clip from Meshy's library can travel across the ring and sink into the floor** (Turbotron's `Win`). On the
  select screen the fighter is held over his spot whatever the clip does; a dancer's body is lifted by whatever its
  lowest bone is under the floor (`AKSDummy::KeepOnFloor`).
- **Meshy's models are stored in metres and drawn a hundred times that size.** A measurement taken from the vertex
  buffer came out as 0 cm until that factor was applied. Print the raw extents once before trusting a derived number.

## Meshy set pieces (the arena kit)

- **A Meshy model has 0.1 to 2.7 million triangles, and Unreal cannot cut it down without a window.** The first
  import of 19 props was 956 MB and the game ran at 13 frames a second. `StaticMeshEditorSubsystem` (set_lods) does
  not exist in a `pythonscript` run, and the asset keeps the whole source mesh anyway. There is no Blender here.
  Rule: slim the GLB before import (`art/tools/slim_props.py`, numpy only), and print the triangle count in the
  import log.
- **Meshy's metal maps glitter.** Imported as they come, the risers sparkled like tinsel and the floor lamps looked
  carved from ice: near-mirror metal, fine perforated patterns, and no temporal anti-aliasing in this game. Rule:
  props wear `M_KS_Prop` (paint only, nearly matt, kit without its normal map, the much-repeated riser darkened).
- **Text-to-3D gives the object, not the part.** "One straight section of box truss" came back as a cube of truss,
  and "a long thin truss beam" as a whole gantry with legs. The gantry was the better set piece; the rig was
  redesigned around it instead of paying a third time. Rule: look at the thumbnail and the bounds before writing the
  code that places a model, and ask for things that exist as a whole object.
- **"No text" does not stop text.** The jukebox says JUCKERK and the lifeguard hut LUKY. Look at every thumbnail.
- **A lamp that lights a landmark at 5200 candelas burns it white**, with the material's own glow on top. 1300 and a
  glow of 0.16 still burn out the silver organ pipes. Judge exposure in the opening wide shot, where they are seen.
- **`pgrep -f "[U]nrealEditor-Cmd"` matched the shell that was about to run UnrealEditor-Cmd**, twice. Rule: match
  the process name: `pgrep -x UnrealEditor-Cm` (names are cut at 15 letters).
- **Interchange names imported textures after the file** (`model_slim_texture_0`), so renaming the source file
  renamed the assets and a script that looked for `model_texture_0` found nothing. Rule: find them by their ending.
- **The crowd's places were sorted by distance from (75, 0), not from the ring's middle**, so with a few places to
  spare the near end stayed short (36 of the 40 the check wants). Sorted from (0, 0) both ends fill alike.
- **A hold put on a fighter for his victory dance stayed on for the next bout.** The winner is held over his spot so
  the dance stays in the picture; the player's fighter is the same actor bout after bout, so in the next bout he
  stood where he had landed the knockout, inside his opponent. It showed only as two camera checks failing "at
  random". Rule: anything switched on for one phase on an actor that lives longer is switched off where the next
  phase begins (`AKSDummy::LetGo` in `StartCard`); and a check that fails now and then is logged in detail before
  its limit is touched (the log showed the player at x=130). The check now reports where the player fought from.

- **The special's side-on camera stood outside the ropes, level with the top one** (seen in stills, 2026-11-13): a
  cyan bar right across the picture in every special of the player's. The shot's wide position was 400 cm out; the
  ropes run at 330, the top one 130 high, the camera 132. It had been like that since the specials were made: six
  of the seven had never been looked at. Rule: a camera position written as numbers is checked against the ropes
  (330); and a new shot is not done until a still of it has been seen. Now 300 out with a wider lens.
- **Particle effects from a pack, first try: the whole screen white for seconds.** A dozen effects per special at
  double size, with the packs' light colours set to the fighter's colour times six. The player had asked for
  restraint in the same hour ("not covering the whole screen for too long, 1-2 seconds"). Rule: start from the
  pack's own size and brightness, one or two effects per moment, and cap every effect's life in the player
  (`KSVfx`); look at stills before adding a second layer. Details and the other pack lessons: `docs/VFX.md`.
- **A tour that photographs effects one after another must take the last one away first.** Half of them never end by
  themselves; the first set of 246 stills showed every earlier fire in every later picture.
- **`pgrep -f "[U]nrealEditor-Cmd "` matched the shell that was about to start it** (third time for this family of
  mistakes): a queued load test waited on itself for ten minutes while the player was told it was running. Rule:
  guards look at process names only, `ps -eo comm | grep -x -E "UnrealEditor|UnrealEditor-Cm|dotnet"` (names are cut
  at 15 letters), and a queued job is confirmed started before anybody is told so.
- **Fighters on two fixed marks cannot dodge, be driven back or land anywhere** (player, 2026-11-13: a dodge should
  move the fighter and the opponent adapt; the jumping special landed behind the opponent and then "reappeared" in
  front). Every move was a lean of the drawn body inside an actor that never moved, and a clip that travelled was
  eased back home when it ended. Rule: the actor is where the fighter is (`AKSGameMode::TickRing`); a lean or a
  clip's travel that should last is handed over to the actor (`AKSDummy::TakeShift`, `CommitTravel`), never eased
  away; and code written for "player at 0,0, opponent at 150,0" (the camera) stays in that frame and is carried into
  the world in one place (`RingToWorld`).
- **A script that deletes an asset before it has made the new one leaves nothing when it fails.**
  `make_beam_material.py` removed `M_KS_Beam` and then stopped on a Python error (a helper's argument named `a`
  clashed with the mask node's `a` property); for a minute the arena had no beam material at all. Rule: after a
  headless asset script, look for its "made" line (`unreal/Saved/beam_material.txt`) before building or running.
- **A beam of light drawn on a mesh needs its brightness from the beam, not from the mesh.** Flat glow on a cone is a
  coloured wedge; soft sides from the cone's own normals lost the narrow end. The material now works from the beam's
  axis and tip (two parameters set wherever a beam is aimed), which also works on a box, so the lasers share it.
- **"One particular move" was still an accident waiting to happen** (player, 2026-11-13, third report about the film
  and the menus). After "any punch" came "a jab" for the film and "left punch next, right punch choose" for the
  menus: a person settling in front of the camera threw twelve "punches" in half a minute. Rule: outside a bout
  nothing a single stroke can do is a button; a menu moves on a swipe (a shoulder's width in a quarter second) and
  "yes" is a jab with one hand and then the other. And measure it: phase changes are marked in the recordings now.
- **A shift of the body is called as a left and a right at once.** Both wrists move with the trunk; the recordings show
  pairs 0 to 170 ms apart. The detector throws out the second of a pair within 120 ms; the menus ask for 200.
- **Tuning from recordings: change one thing, replay, count.** A higher bar, an automatic bar and a both-arms rule
  together halved the punches in the player's sessions, far more than was meant; each alone was modest. The replay
  (`-KSReplay`) needs the tracker: it does nothing with `-KSNoCamera`.
- **A blend timed by the phase clock starts again at every phase change.** After the bell the camera turns to the
  winner over 0.7 s of `PhaseTime`; that clock restarts when the knockout gives way to the result, so the camera swung
  back to the fight view for a fifth of a second there and cut the winner (Channel Zero, 7 to 18 frames of 280). The
  first guess, a lens not wide enough, was wrong: the log of the bad frames showed the phase and the time. Rule: what
  spans several phases has its own clock (`OpSinceBell`); and log the bad frames before fixing by theory.
- **"Keeps the opponent's head in the picture" was not "keeps both fighters in the picture"** (player report,
  2026-11-13: the cameraman sometimes loses one of them). The check counted one head and allowed 7% of the frames.
  Rule: a promise about the picture is enforced where the picture is made, after everything else has had its say
  (the last block before `SetView` in `UpdateFightCamera`: both heads and both body middles inside the middle 86%,
  by turning the camera and, if need be, opening the lens), and the check counts on the picture as shown.
