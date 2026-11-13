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

- Arena rebuilt from Meshy 3D models (player request, 2026-11-13; 600 credits: 19 models and one bought twice).
  A kit every arena uses, in `Content/KS/Props` (`art/arena/gen_props.py` -> `art/tools/slim_props.py` ->
  `art/tools/import_props.py`, placed by `FKSBuilder::Prop` in `KSWorld.cpp`): stage risers as the stands, steel crowd
  barriers at ringside, ring posts, four truss gantries standing round the ring with stage lamps hung under them that
  turn with their beams, moving heads on the floor, a scoreboard cube over the ring, a wide gantry across the far end,
  loudspeaker towers, four round podiums. And one landmark per themed arena behind the far stands (a pair, the Dojo's
  gate alone in the middle): rocket, lifeguard tower, jukebox, water tower, torii gate, pipe organ, boombox, stack of
  wrecked cars, stone idol, TV camera.
  The stands are steeper (each row 55 cm above the one in front, was 40; measures in `KSStands`, `KSWorld.h`), the
  near end has a real aisle, the giants stand at the back of the platforms and on the round podiums. Checked: every
  arena reports its pieces and none missing, 179 seated, 40 or more on each side. Seen in stills (hall, space, beach,
  dojo, castle, volcano): four levels of crowd read in the fight view; the landmarks show in each bout's opening wide
  shot, not in the fight view (the crowd fills it).
  The other five were looked at later in their opening shot: jukeboxes, water towers, boomboxes and car stacks stand
  the right way round and read; the studio's TV cameras are dark and hard to make out.
  **Not verified**: the scoreboard, podiums and near-end loudspeakers have not been seen in any still;
  nothing has been seen in motion (lamp housings turning); the frame rate was not measured (a self-played bout made
  about as many frames as before, 948 and 1255); the cameraman may pass behind a gantry leg (46 cm thick, at the rig's
  corners); whether a spectator's feet sit exactly on his riser was judged by eye. Known flaws: the organ's pipes and
  the white lifeguard huts burn out under their lamps; the risers' frames still glint in the bright arenas; the
  jukebox and the hut carry made-up words (JUCKERK, LUKY). `Content/KS/Props` is 196 MB before cooking.

- Select screen and celebrations, fixed 2026-11-13 after player reports (Turbotron danced out of the picture and
  into the floor, the title band cut Count Hairspray's hair, ropes across the picture, winners leaving the frame):
  - The select camera frames the room the fighter uses (`AKSDummy::BodyBox`: bones, flesh, and hair from the
    `KSHeadRooms` table), between the bands given by `KSSelectFrame`, from inside the far ropes; the fighter stands
    at the player's end, dances on the spot (`StayOnSpot`) and on the floor (`KeepOnFloor`); the ropes are hidden
    on this screen. He is drawn smaller than before the fix, because all of him now fits between the bands.
  - After the bell the winner dances on his spot and on the floor, the cameraman steps back, looks at where he is
    and opens the lens until he fits.
  - Guards in `tools/check.sh`: every fighter is toured (`-KSSelectTour`) and must stay inside his part of the
    screen; each model's hair is measured and must be covered by the table (a new fighter fails until he is
    listed); frames with the winner partly out of the picture after the bell; where the player fought from.
  Checked, and the select screen seen in stills. **Not seen**: a celebration (no stills taken of one since the
  change), anything in motion, and whether holding dancers to their spot makes their feet slide noticeably.

- Special abilities redrawn with particle effects, and fighters that move (player requests, 2026-11-13 evening).
  - **Effects**: the glowing shapes are gone; each special is a few Niagara effects from free Fab packs
    (`docs/VFX.md`, `KSVfx`): rings while charging, then fire along the floor (slam), an electric beam and crackle
    (static), six coloured beams (laser), a ring of flames (fire ring), bolts from the roof (lightning), a flying
    ball with flames (blast), purple puffs and bats (hairspray); sparks and a ring of bent air where it lands.
    Every effect is gone within two seconds. Seen in stills of all seven, as the player's special and as the
    opponent's. **Not seen in motion; the beams and bolts are faint in the stills and may need more.**
  - **Movement** (`TickRing`): the fighters have real places in the ring. A dodge that is let go of is a step and
    the opponent turns and closes in after a moment; a punch drives the one hit back and the other follows; a block
    throws the attacker back; a special throws its victim most of a metre. Aerobics Annihilator's jump leaves her
    where she lands (187 cm on) and the opponent turns to her; the camera follows the pair round. In three
    self-played bouts: 4 dodge steps and 5-7 m of travel a bout, no crash. **Nobody has played it**: whether the
    camera swinging round after the jump is readable, whether dodging feels like stepping, and whether the pushes
    are too much or too little are all unjudged.
  - The special's side-on shot had a ring rope across it; fixed (see LESSONS).
  - **Fixture lights** (player: "lights done with meshes look bad"): the lamps' beams and the lasers are no longer
    flat wedges and solid rods of one colour. `M_KS_Beam` (`art/tools/make_beam_material.py`) now shades a beam from
    its own axis: bright down the middle, nothing at the edge, hot at the lamp, uneven like haze and drifting, fading
    into whatever it meets; the lasers use it too. Seen in stills of the hall: soft shafts and thin soft lines.
    **Not seen**: the other ten arenas, the knockout light show, the drift in motion.
  - **Hit by a special, the clips**: bought (6 Meshy credits) and on all seven fighters: `Shocked` (Electrocution
    Reaction) for static, laser and lightning, `Blown` (Hit Reaction) for the rest, 1.2 s. The check sees the right
    one chosen. **Not looked at closely**: the stills are too small to judge the acting.
  - **Hit marks**: the glowing ball at the point of contact is replaced by thin rings flying off the glove (bigger,
    with sparks, on a counter; red when the player is hit; pale ones off a guard). Seen in stills: small and crisp.
    **Not seen in motion.**

- Webcam input made less twitchy (player, 2026-11-13 night; details and numbers in `docs/TRACKING.md`):
  - **Film, menu, select**: a hand swept down/up moves the menu, right/left moves along the fighters, a jab with one
    hand and then the other chooses or skips the film. A punch alone does nothing there any more.
  - **Bouts**: a punch has to be faster (bar 5.0, was 3.5), the bar rises to 60% of the player's own typical punch
    (at most 6.25), and a left and a right in the same instant (the body shifting) count as one. Replayed over three
    of the player's recorded sessions the detector now calls about 30% fewer punches (308 -> 204, 434 -> 283,
    398 -> 282).
  **Not verified**: nothing here has been tried by a person. Whether real punches are now missed, whether a swipe is
  always seen, and whether left-then-right is comfortable are all open; a calibration from the menu still adapts the
  bar to a soft hitter.

- Both fighters always in the picture during a bout (player report, 2026-11-13): the last step of the fight camera
  turns it, and opens the lens if it must, until each fighter's head and the middle of his body are inside the
  middle 86% of the screen; the check counts frames with either of them out (0 in every bout run so far; the rule
  acts in about a fifth of the frames). After the bell the cameraman also keeps 3.2 m from the winner, and the
  turn to the winner no longer restarts at the result screen (Channel Zero's dance was cut there; nine replays
  clean). Seen in stills: both fighters in every bout frame; the winner after the bell is whole but small.
  **Not judged**: whether the correction is felt as a jerk in motion.

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
