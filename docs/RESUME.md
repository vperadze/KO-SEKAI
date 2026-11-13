# Where things stand

Updated 2026-11-13. Deadline 2026-11-16 02:59 CET.

## Works (checked by `tools/check.sh` or seen in a self-playing fight)

- Seven fighters, random player fighter, ladder of three random opponents.
- Punches connect: aimed clips, recoil, hit-stop, flash, sound, camera shake.
- Smooth transitions between clips, with momentum carried over.
- Arena: Dreamina ring mat and crowd-stand wall; photo flashes in the crowd. Stepped stands with neon edges that the
  crowd stands on, a dark patch under each spectator's feet, back rows dimmer; smoke-machine haze (volumetric fog)
  lit by fourteen stage lamps: eight on a rig over the ring and six on the floor at ringside whose beams (glowing
  cones, `M_KS_Beam` from `art/tools/make_beam_material.py`) rise in front of the crowd and sweep; a fan of eight
  lasers from the floor in front of the far stands; confetti plus a strobing light show after a knockout win
  (`KSWorld::BuildArena`, `AKSGameMode::TickArenaFx`). Seen in stills: stands, shadows, haze, beams and lasers all
  read. **Not seen**: anything in motion (sweep, strobe, confetti falling), and the cost in frame rate with the
  webcam tracker running.
- Crowd, redone 2026-11-13 at the player's request ("too cartoonish"): about 60 spectators drawn from 50 characters,
  now rendered 3D people in the fighters' look instead of arcade pixels (same cast: people of the 80s, monsters, a
  T-rex, a dragon, a UFO...), eight poses each (idle, cheer, wince, shout, fist pump, boo, clap, dance). Seated by
  size, people-sized (a normal one 190-215 cm). They move smoothly now (jumps with a squat on landing, sways,
  breathing, clapping, a dance), each picture turns to face the camera, a knockout is celebrated for 3-7 s, a wave
  goes round the stands, 18% root for the opponent. Pictures: `art/arena/gen_crowd_real.py` (120 credits spent, tests and six redone creature sheets included) ->
  `slice_crowd.py` -> `art/tools/import_crowd.py`; the list the game seats is the generated `KSCrowdData.h`.
  Seen in stills: they read as people in the stands, in every arena tried. The slime's sheet has a man's face in
  some poses and the unicorn's body changes between poses (no credits were left in the approved 120 to redo them).
  **Not seen in motion**: whether swapping whole poses on a realistic picture reads as acting or as a slide show.
- Crowd sound, cut from Dreamina video soundtracks (`art/arena/gen_crowd_sound.py` -> `art/tools/prep_crowd.py`):
  cheers on the player's hits, gasps and boos when he is hit, a roar on a KO, hecklers shouting, beast roars, and a
  looping murmur under the music. Loading is checked. **Nobody has listened to them**: what the shouted words
  are, how they sit against music and voices, and whether the murmur's loop point is audible are all unjudged.
- Fight camera, redone 2026-11-13 at the player's request: a Steadicam operator who walks the whole ring
  (`UpdateFightCamera`, "the cameraman"). He picks a new shot every 2-4 s (over either shoulder, three-quarter,
  side-on, low on one knee, high and wide), dives in close when the opponent is open and may then go right round
  behind him, runs back behind a shoulder when a punch is wound up, circles the ring after the bell. Footstep bob,
  lean into the run, a frame that trails him; the old springs (lurch when hit, drive-in on punches, slide with
  dodges) still act on top. The check counts his shots, the arc he covered and frames with the opponent out of
  the picture, and frames with the player's head in front of the opponent's (the first version stood straight
  behind the player's back; he now crosses there high, looking over the head, and comes close only from the side).
  Seen in stills: both fighters in nearly every frame; the opponent's head is out of the picture in up to 3.5% of a
  bout's frames (the check allows 7%). **Not judged by a person**: whether it is exciting or seasick, and whether defending still reads
  from the side views.
- Footwork: boxer's bounce, planted feet that take real steps when the body moves, idle shuffles
  (`AKSDummy::Footwork`, two-bone leg IK on the drawn body). Seen in stills only; the feel in motion is unjudged.
- Built-in webcam tracker: camera opens, body is found, 30 frames/s when the machine is otherwise idle.
- Session recording and offline replay of the move detector.
- Recorded sounds from the user's sound library for hits, counters, blocks, hurt, whooshes, dodges, KO, body
  fall, footsteps, coin, win and lose (`art/tools/prep_audio.py` -> `Content/KS/Audio`); bells, the opponent's
  tell and the pull-in stay synthesised. Loading is checked; nobody has listened to the mix yet.
- Fight music: an original 80s synth-rock loop, 75 s, made entirely in code (`art/tools/music.py` ->
  `art/audio/M_Fight.wav` -> `art/tools/import_voice_faces.py` -> `Content/KS/Audio/M_Fight`). `KSAudio::Music`
  loops it; `SetPhase` sets the level (bout 0.32, fighter card and KO 0.2, result and continue 0.12, silent on
  the street and in calibration). Loading, the loop flag and that playback starts are checked.
  **Nobody has listened to it yet**: the tune, the mix and its loudness against punches and voices are unjudged.
- Eleven arenas (`KSArenaData.h`, plan and table in `docs/ARENAS.md`): the original hall plus space, beach, bar,
  rooftop, dojo, castle, gym, scrapyard, volcano and TV studio. Each has its own Dreamina backdrop and ring canvas
  (`art/arena/gen_arenas.py`, 40 credits; `art/tools/import_arenas.py`), three neon colours, fog and camera tint,
  a subset of the 50 spectators, its music loop (`art/tools/music_arenas.py`) and something in the air (stars,
  fireflies, dust, rain, petals, wisps, sparks, embers, glitter); rooftop and castle have lightning, the volcano
  glows. A bout is fought at the opponent's home, the last bout of the ladder somewhere that is nobody's.
  `-KSArena=N` forces one. Each bout's intro opens with a wide shot of the place. The check builds all eleven.
  Seen in stills: every arena reads as its place in the wide shot; in the fight view the crowd, canvas and
  colours change but the painted wall is mostly out of frame. **Not seen**: anything moving (weather, lightning),
  the frame rate with the webcam running, and the Scrapyard canvas looks washed out under the ring light.
- Opening film (`art/cinematic`: `gen_cinematic.py`, 6 Dreamina shots, 28 s, 185 credits quoted including two
  refused shots redone; `prep_cinematic.py` turns them into frame sheets, `KSHUD::DrawIntro` shows them at 12 a
  second with captions and tape lines, the fight music under it). It plays at start-up, any confirm key skips it,
  then the start menu: FIGHT, CAMERA CALIBRATION, WATCH THE INTRO, QUIT. **Not seen in motion.**
- Calibration is optional: only the menu line starts it, and it returns to the menu. Nothing forces it before the
  first bout any more (`-KSNoCal` is now meaningless). Uncalibrated webcam play uses the detector's defaults;
  **nobody has tried that with a person since the change**.
- STEP BACK is now a small plate low in the middle, shown in the ring only and only after the hands have been out
  of view for 0.6 s (it was 64-point letters over the fighters). **Not seen**: it needs a camera and a person.
- Faces that change: every fighter rests on a calm face, snarls while punching, blinks, shows a hit or hurt face
  when struck, a smug one after winning (`AKSDummy::UpdateFace`). The five painted fighters got `calm`, `talk`,
  `angry`, `blink`, `smug` (Meshy edits, `art/tools/bake_faces.py`); TURBOTRON got LED `talk` and `blink`; Channel
  Zero reuses what it had (rests on `smug`, shouts with its default screen). The check counts expressions per bout.
- Voices: 8 short spoken lines per fighter (entrance, 3 taunts, 2 when hurt, win, lose), strong swearing at the
  player's request. Acted locally by Chatterbox, which copies each fighter's Kokoro voice and takes an emotion
  setting per kind of line (`art/tools/voices.py`, run with `art/voice-model/venv`, still needs the Kokoro model
  in `~/ielts-listening`; about 45 minutes for all, no graphics card). Three takes per line; a speech recogniser
  checked that the chosen take says the line; all takes are kept in `art/audio/voice/takes`. The files are pressed
  loud, play at 1.5 (opponent) and 1.3 (player), and the music drops to 40% while anyone talks. The mouth opens
  and closes with the line, a subtitle shows it, one voice at a time. Loading and playing are checked.
  **Nobody has listened to the acted voices yet**: whether the acting is good or just odd, whether each fighter
  still sounds like himself, the loudness against punches and music, and how often they talk are all unjudged.
  Faces were seen in stills only, where they are small at fighting distance.

- Interface in an 80s look (`KSHUD`, art in `Content/KS/UI`, made by `art/ui`): 29 Dreamina pictures (72 credits) and two
  open-licence fonts (Russo One, Bangers). Start menu on a painted background with the logo, three lines (start game,
  straight to the ring, quit) and a glove cursor, with the fight music; chrome health bars that jolt when hit, a timer
  badge, cue plates; comic badges for COUNTER!, DODGE!, BLOCKED, FAKE!, GUARD BREAK!, a POW!/BAM!/WHAM!/KRAK! word and
  the damage on a burst at the opponent's head with every hit (ten points to a point of health), the damage taken on a
  burst; spoken lines in a shouting speech bubble beside the speaker with one word in red, replacing the subtitle
  bar; FIGHT!, K.O.!, TIME!, DOWN!, YOU WIN, KNOCKED OUT, YOU LOSE, CONTINUE?, PAUSED as title art that slams in.
  Badges, bubbles and the cue plate are placed beside the fighters, never on a face (the HUD projects both fighters'
  bones each frame); all text is fitted to its box; the result and continue screens hide the health bars and show the
  spoken line on a plate along the bottom. Seen in stills only: whether the placing looks calm in motion (a bubble
  glides when the fighters move under it) is unjudged, and so is the calibration screen with a real camera picture.
  New sounds from the sound library: Pop (badges, bubbles), Menu, Select, Score (counter), Slam (titles).
  The check counts the pictures and fonts and sees a bubble, a damage badge and the menu drawn. Seen in stills only:
  **the animation (pops, slams, jolts), the new sounds and their loudness are unjudged**, and nobody has used the
  menu with a keyboard or with punches (left punch = next line, right punch = choose) yet.

- Flow, changed 2026-11-13 at the player's request: film -> menu -> FIGHT -> **choose your fighter** -> ladder -> menu.
  - Select screen (`EKSPhase::Select`, `KSHUD::DrawSelect`): the fighter looked at stands in the hall, name and
    tagline on the left, seven portraits along the bottom (photographs of the game's own models, `-KSPortraits` ->
    `art/ui/make_avatars.py`). Left/right or a left punch moves, Enter or a right punch chooses; the fighter then
    says his introduction and pounds his chest, and the ladder starts as him. Checked: all seven portraits load, the
    chosen fighter is the one played. Seen in two stills. **Not seen**: scrolling through all seven, the feel of it.
  - Esc goes to the start menu from anywhere (bout, card, result, continue, calibration, select). Checked mid-bout.
  - The street is gone from the game: the CONTINUE? count running out used to drop the player on the street walk,
    and winning the ladder ended on the dark street with the cabinet. Both now go to the menu (`ToMenu`). The street
    code remains behind `-KSStart=street` only. Checked for the count running out.
  - The film is skipped by a key or by a **jab** at the webcam, no longer by any punch (it stopped whenever the
    player moved). Checked with fake punches; **not tried with a person**: if settling in front of the camera sets
    off jabs too, the film will still stop.
- Ring announcer (`art/tools/announcer.py`, `KSAnnouncerData.h`, `AKSGameMode::Announcer`): 17 lines in a deep voice
  through a hall loudspeaker, made locally like the fighters' voices. He welcomes after the film, says "choose your
  fighter", calls each fighter's name on the select screen and before each bout ("In this corner: ..."), calls the
  round, the knockout, the player going down, time, the new champion after the last bout and game over. Fighters
  wait for him; the music dips under him. Checked: every line asked for has its recording.
  **Nobody has listened to him yet**: the voice, the pathos, the loudness and whether he talks too much are unjudged.

- Added 2026-11-13 (second request of the day), all checked by `tools/check.sh` and seen in stills only:
  - **Moves**: 36 Meshy library clips (108 credits), six per fighter: two fun idles and a "picked" move on the select
    screen (the fighter no longer stands in his stance), his special, a win celebration (a dance, replacing the one
    shared victory clip) and a stagger. `art/tools/add_moves.py` -> `import_moves.py`.
  - **Portraits**: painted by Dreamina from the photographs (14 credits), `art/ui/gen_avatars.py`.
  - **Block breaks his attack**: a blocked punch throws the opponent back (`Stagger` clip, knock-back, camera jolt)
    and leaves him open for a second (it was half a second and a face-punch loop).
  - **Special abilities**: a meter each under the names, filled by hits, counters, blocks and dodges. The player's
    goes off on a shout into the microphone or the S key; the opponent's by itself (duck or lean away; a block
    halves it). Each fighter has his own move, battle cry and effect, built in code from glowing engine shapes
    (`FxAdd`/`FxBeam`/`FxBurst`): ground slam, static, rainbow laser, fire ring, lightning, fireball, hairspray cloud
    with bats. 24 damage and the opponent left open; a side-on camera shot for the charge and the hit.
  - **Microphone** (`KSMic`): a shout is "much louder than the room for a fifth of a second"; it cannot tell words
    apart. Windows uses Unreal's capture; Unreal has none on Linux, so there it reads `arecord`. Opens here.
  - **Idle in a fight** (third request of the day): between attacks the opponent plays one of his own two fun moves
    (every third time the plain stance), picked up at a random place; the player's fighter does the same after 1.3 s
    without input. A slow blend in (0.32 s), the usual quick one out into a punch. Seen in one still: the two stand
    differently; with arms out at 150 cm apart their gloves can overlap.
  - Later the same day: the select screen loops one dance (the fighter's win dance) instead of alternating clips;
    the clips no longer travel (hips kept on the spot); the game uses FXAA, which stopped moving fighters dissolving
    into noise. Seen in six stills over ten seconds: sharp and in place. Thin lines (ropes, lasers) will be a little
    more jagged than before; not compared.
  **Not verified**: none of the 42 clips has been watched in motion (they were chosen by name from Meshy's list, so
  some may look wrong for the fighter or slide across the floor); six of the seven effects have not been seen even in
  a still (only the fireball); nobody has shouted at it (the threshold, and whether the game's own sound from the
  loudspeakers sets it off, are untested: headphones avoid that); the Windows microphone path is untested; the
  balance of the specials (meter speed, 24 and 22 damage) is a guess.

- Fixed 2026-11-13 (player reports), both checked: the webcam stays free after quitting (the microphone recorder no
  longer inherits the game's open files or outlives it), and the film's skip hint is steady.
- Three times the crowd, all round the ring, nobody twice (player request, 2026-11-13; 272 more credits, 392 for
  the realistic crowd in all): 179 characters (`crowd_roster.py`: `NEW`, `OLD`, `MORE`), 187 places on all four
  sides (three rows at each end, two rows and a platform row down each side, a stepped block in every corner, giants
  at the back, flyers above; `SeatCrowd`, stands in `KSWorld::BuildArena`). Every character is dealt once; an
  arena's own people get the front rows. The near end keeps an aisle for the wide shot that opens a bout. Checked:
  179 seated, none twice, 40 or more on each side. Seen in stills: the house is full from every camera angle.
  **Not measured**: the frame rate with 179 spectators and the webcam running; a "Preparing Textures (220)" notice
  showed on the first frame of a run, so the first bout may hitch while they load. The painted wall still covers
  only the far 230 degrees: behind the near stands it is dark. Crowd textures are now 126 MB before cooking.

## Built but not verified with a person

- Early punch call (punch starts while the arm is still extending).
- Absolute block rule (both fists at face height).
- STEP BACK warning and the fist-below-picture rejection.
- **Calibration screen** before the first bout: framing mirror and checklist, guard, three jabs each side (sets the
  detector's sensitivity from the player's own punch speed), block, lean left, lean right, duck. Steps through in
  demo mode and with a live camera; a person completing each step by doing the move is unverified.
  Its text no longer blinks when a reading sits on a limit (debounced; checked with `-KSCalJitter`); with a
  real camera and person this is still unconfirmed.
- Retry after a loss (goes through the same fresh-opponent path as the next bout).
- **Online play** (2026-11-13; `KSNet`, `KSGameModeOnline.cpp`, `tools/ko_relay.py`): PLAY ONLINE on the start menu,
  then host, or join a host or the relay server by address. A bout between two players uses the cabinet's own rules
  with the other player throwing the opponent's punches; each game is the judge of its own health. `tools/check.sh`
  plays two copies against each other on one machine, directly and through the relay, and checks that both tell the
  same story (winner, health, punches landed and taken), and that a rematch starts. **Not verified**: two people,
  two machines, a real network (only 127.0.0.1 was used, so delay and a firewall were never in play); whether half
  a second in the air is the right time for a person to answer a person; the damage numbers (two bots finish a
  bout in about 25 s); the lobby's address box and its three-line menu were never used by hand (only the waiting
  and fight screens were seen in stills); a webcam player online. The team has no relay server running anywhere
  yet, and `OnlineServer` in `DefaultGame.ini` is not set.
  Since the merge with the select screen and the specials (2026-11-13): PLAY ONLINE goes through the fighter select,
  so each player fights as the fighter they picked, and the host draws the arena. **Special abilities are switched off
  online** (a special takes health off the other side locally, and online each game judges only its own health); the
  meters are not drawn there. A packaged Windows build was made on the Windows PC and two copies of it played an
  online bout on that one machine.

## Next

1. **Scripted recording from the player**, standing far enough back: 3 s still, 5 left jabs, 5 right jabs,
   3 hooks and 3 uppercuts each side, 3 blocks of 2 s. Turn it into `tests/tracking/` cases and tune from it.
2. Commit and push (nothing since the `real-fighters` push at `ddc971e` is committed).
3. Windows package: build, size under 1 GB, test the webcam there (the OpenCV camera backend differs on Windows).
4. The 30–60 s gameplay video.

## Known gaps

- The new face textures add about 200 MB to `Content` before cooking (1.3 GB in all). The packaged size is not
  measured yet; if it passes 1 GB, the face textures are the first thing to shrink (1K, or only the head region).
- Channel Zero has no painted `calm`, `talk` or `blink` screen (Meshy credits: 118 left, 12 per edit).

- No duck or get-up clips.
- Fab assets: none in the project; Fab cannot be driven from here.
- `Content/KS/Tracker/pose_lite.uasset` (5 MB) is an accidental editor import the game does not use.
- Spectators are flat pictures that turn to face the camera; they have no side or back view.
