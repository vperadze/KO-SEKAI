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
