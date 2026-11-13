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

## Interface art

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

