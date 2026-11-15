# Where things stand

Updated 2026-11-15. Deadline 2026-11-16 02:59 CET.

## Works (checked by `tools/check.sh` or seen in a self-playing fight)

- 2026-11-15: the opening film's words stand in the lower black bar (they covered the grandmother's face in the
  walking shot), and the engine's corner notices ("Preparing textures") are switched off. Both checked by log line;
  **not seen by the player yet**, and the notice only ever appeared while textures were being prepared, so its
  absence was not watched happening.
  The story's year is 1985 now (film, menu line, ending; "41 years"), at the player's request. The jam trailer in
  `art/trailer` still says 1987, in its caption and in the spoken line.
- 2026-11-15: the opening film has its own sound beside the music (`M_Cine`, the six shots' own soundtracks levelled
  and joined by `art/cinematic/prep_cine_sound.py`). Loading and length are checked. The player listened to it:
  good, except the shot of the seven walking, whose take had music in it; that shot is silent now (**not heard again since**).
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
  - **A left-right jab no longer picks the neighbouring line** (player, 2026-11-14): a jab was also read as a swipe,
    so the list moved between the two jabs. The pair now takes back the swipe steps made during it (menu, select,
    lobby). Checked with a fake player only; a lone jab can still move the list by one.
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

- Merged 2026-11-13 night: the colleague's online play (main) with the particle effects, the moving fighters, the
  model-built arenas and the calmer webcam controls (real-fighters). `tools/check.sh` passes on Linux with both
  (122 ok, the two online matches included). What the merge leaves open:
  - **Online and the moving fighters**: each game moves its own two fighters (a dodge is a step, hits push); the
    places are not sent to the other game, so the two players do not see exactly the same ring. Nobody has looked.
  - **Online and the camera checks**: those cover the single-player path only.
  - The lobby takes the webcam as the menus do now (a swept hand moves, left jab then right jab chooses).
  - **Windows**: not built since the merge (it needs the Windows PC). The effect packs must be added there from
    Fab as well (`docs/VFX.md`); they are not in the repository.

- Linux package measured 2026-11-13 night (`unreal/package_linux.sh`, Development): the folder is 1558 MB, of which
  273 MB debug symbols (`.debug`, `.sym`) and 258 MB Vulkan debugging layers that need not ship: **1026 MB without
  them, 727 MB as a `.tar.gz`**. The content (`.ucas`) is 587 MB (it was 202 MB a day earlier: three times the
  crowd, the arena models, the effects), the game binary 286 MB. The packaged game was run: it loads all 15
  effects and plays both specials. **Just over 1 GB unpacked**: if the jam counts the unpacked folder, the crowd
  pictures are the first thing to shrink, or a Shipping build of the binary. Windows not built.
  **Crowd at half resolution, done 2026-11-14** (`art/tools/import_crowd.py`, `SHRINK = 2`: the engine caps each
  sheet at 512, or 1024 for the eleven wide ones; the drawn pictures are untouched and `SHRINK = 1` undoes it). The
  cooked crowd went from 255 MB to 65 MB as expected, **but the package shrank by only 22 MB**: the content file is
  compressed and the crowd compressed well already (`.ucas` 587 -> 565 MiB). Measured: the folder 1536 MiB, without
  debug symbols and Vulkan debugging layers **1003 MiB = 1052 MB, so still just over 1 GB unpacked** (it was 1026
  MiB). The packaged game was run: 15 effects loaded, both specials played, 179 spectators seated; the crowd looks
  the same in fight-view stills. **Not seen**: the front rows close up, where the cameraman passes them. What is
  left to shrink, biggest first: the game binary (299 MB, a Development build; Shipping is smaller), the fighters
  (244 MiB cooked), the engine's shaders (81). Built again at 12:45 with the new opponent and the menu jab fix in it: the same size, and the packaged game
  ran (15 effects, both specials, no crash). `Packaged/Linux/KoSekai/Saved` holds 60 MB of test stills from those
  runs: leave it out of anything that is shipped.
- **Windows package under 1 GB, 2026-11-14** (`unreal/package_windows.bat`, now Shipping by default and without the
  `.pdb`): the folder is **841 MB (802.5 MiB)**. As a Development build it was 1050 MB without the `.pdb` (1001 MiB):
  the program fell from 318 to 159 MiB and the 34 MiB of Vulkan debugging layers are no longer staged. The content
  (`.ucas`, 513.5 MiB) is unchanged: it was already compressed (Oodle, 38% saved), so `-compressed` gains nothing.
  The package was run by itself (`-KSStart=fight -KSAuto -KSSpecials -KSHitShots`): exit 0 after 37 s, and the stills
  show the arena, the crowd, the interface with its fonts and both specials with their effects. A Shipping build
  **writes no log**, so no `KSTEST` lines can be read from it (`set KS_CONFIG=Development` for a build that logs), and
  it keeps its settings, stills and tracking recordings in `%LOCALAPPDATA%\KoSekai\Saved`, not beside the program.
  It contains the lobby code, but `OnlineServer` is still not set. **Not verified** in it: webcam, microphone, sound,
  the menus by hand, online play. The Linux script still builds Development.

- **Badges and the fighters (2026-11-14)**: the one failed line of the night before ("badges cover the fighters")
  is explained and fixed; the note that stood here blamed the wrong thing. The measure used a box round each
  fighter, which is mostly air when an arm is out, and it looked only at the moment a badge was placed. What a
  player could really see was the hit fighter falling back into the badge at his shoulder. Now the HUD knows the
  bodies themselves (`BodyCover` in `KSHUD`, points along every bone from `AKSDummy::BodyPoints`), leaves a little
  room round them, and a badge slides away when a fighter moves under it. Over four self-played ladders each: a
  third of a badge lay on a fighter in 36 to 41% of the frames before (`-KSBadgesStay` brings that back), 3 to 8%
  now. **Seen in stills and numbers only**: whether a sliding badge looks right in motion nobody has watched.

- Opponent made harder and bouts made longer (player, 2026-11-14: "I can act really fast, his movements are slow and
  I win easy... make battles last like 3 times more"). `OppAI`, `OppAttack`, `PlayerPunch`, `ResolveStrike`:
  - Wind-ups 0.50 / 0.66 / 0.76 s (were 0.65 / 0.85 / 0.95), times 0.9 and 0.8 on the second and third opponent,
    each a little different, never under 0.34 s; shorter rests.
  - A punch that lands may be followed by one or two more; a block or dodge ends the combination.
  - He fakes from the first bell and most after being countered; a punch into a fake meets his gloves and gets a
    quick answer (it used to be a counter). Punching his guard gets an answer too, often at the first punch.
  - He reads the stance held in front of him and the defence the player used most lately, and throws what beats it
    (how often is `Skill` in `LadderTiers`: 0.40, 0.62, 0.85). A counter is worth 2 times, was 2.5.
  - Length: every punch takes about a third of what it did, specials 12 and 11 (were 24 and 22), meters fill at 40%,
    the clock is 5:00. Online damage is half what it was.
  Measured in `tools/check.sh` (132 ok, all passed, three runs): the bot that makes no mistake wins in 59-62 and
  67-69 s (was 24 and 20); a player who does nothing is knocked out in 35-38 s (was 17) and takes 4-7 follow-up
  punches; two online bots finish in 47 and 33 s (was 15). A bot that punches all the time (`-KSSpam`, new) got 13
  quick punches back and was drawn by 2 fakes in 36 s.
  **Known**: that last bot also defends everything, and it won in 36 s, quicker than the patient bot: each answer it
  blocks is another opening. A person cannot block an answer that comes 0.35 s after his own punch, but if punching
  all the time still turns out to pay, make a blocked answer not open him (`ResolveStrike`).
  **Not verified**: everything a person would feel. Whether the quickest punches can still be dodged through the
  webcam's delay, whether stray webcam punches now get punished unfairly, whether a real bout is three times as long
  (a person is not the bot), whether the third opponent is beatable, and whether fakes and answers read on screen.

- **Harder again, dizzy, optional webcam, key settings (player, 2026-11-14 afternoon, five messages).**
  - **Opponent**: wind-ups 0.34 / 0.44 / 0.50 s (were 0.50 / 0.66 / 0.76), never under 0.24; damage 3.2 / 5.2 / 6.8
    (were 2.4 / 4.0 / 5.2); health 130 / 155 / 180 (were 100 / 120 / 140); rests about half as long; combinations of
    up to four. His punch is **drawn** differently: a short held tell, then the clip at about twice its speed
    (`WindSnap`, `WindTell`); it used to creep out at half speed. His stance and recovery clips play faster too.
  - **He defends**: he leans out of the way of punches (`Stand_Dodge` clips, "MISSED"), an opening is no longer three
    free punches (after the first he may cover up or slip, and answer), and now and then he sees a counter coming.
  - **He adapts** (`Heat`, 0 to 1): it rises with how fast the player punches and how many of his punches the player
    gets out of; up to a fifth off his wind-ups, nearly half off his rests, and he reads the player more often
    (`Wits`). Measured: 85-93% against the bots that avoid everything, 0% against a player who does nothing.
  - **Dizzy**: a blocked punch, or a combination broken (a follow-up punch blocked, dodged or countered), leaves him
    dizzy for **0.5 s** ("dizzy should break after 0.5 sec"): Meshy's "Head Hold in Pain" (3 credits, on all seven as
    `Dizzy`), five stars circling his head, every punch lands and counts 1.25 times. A dodge of a lone punch leaves him
    open for 0.56-0.72 s instead (it was 1.2 s), a block used to give a second.
  - **Webcam optional**: shut when the game starts; the menu has seven lines now (FIGHT, WEBCAM: OFF/ON, CAMERA
    CALIBRATION, CONTROLS, WATCH THE INTRO, PLAY ONLINE, QUIT). The film is skipped by Enter only.
  - **Controls screen** (`KSKeys`, `EKSPhase::Controls`): the twelve moves of a bout, each with its key and how it is
    done at the webcam; Enter on a line and the next key pressed is its key (two moves swap rather than share),
    RESET ALL KEYS, saved in `GameUserSettings.ini` under `[KoSekai.Keys]`. The hints in the ring show the player's
    keys. Menu keys (arrows, Enter, Esc) are fixed.
  Measured by itself: the bot that makes no mistake wins in 67 and 77 s; a player who does nothing is knocked out in
  21 s (was 35-38) after 8 follow-up punches; the bot that punches all the time wins in 69 s (27 dizzy spells, 36
  answers, 18 slips, 7 counters seen coming).
  **Not verified, and this is most of it**: nobody has played it. Whether punches this quick can be avoided at all
  through the webcam's delay (0.38 s from the start of the quickest punch to the last moment a defence counts; the
  third opponent, hot, is at that floor most of the time); whether half a second of dizzy is long enough to land
  anything with a real arm; whether a person can still win a ladder; how the tell-then-snap punch, the dizzy clip at
  1.5 speed, the stars and the leaning dodge look in motion (seen in stills only: the stars circle the head as five
  glowing cubes, the DIZZY! and COMBO BROKEN! words show, a glove is at the head in some dizzy frames; the first
  stars were inside Turbotron's helmet and the hurt clip hid the dizzy pose, both changed); the controls screen and
  the seven-line menu were looked at in one still each and read well, but have not been used by hand; rebinding
  by pressing a real key (the check rebinds through the same function, not through the keyboard); switching the
  webcam on from the menu with a real camera, and off again; the menu's WEBCAM line when no camera is plugged in.
  **The check**: 139 ok on the first full run; the second failed "a fighter was out of the picture" (0.8 and 1.1% of
  the frames; fixed: the cameraman steps back when the two are too far apart for the lens, 0 lost frames in the four
  bouts since); the third had 138 ok and one failure, "the player's fighter stood 46 cm from his mark" against a limit
  of 45. That number follows how much the player punches (8 with no punches, 52 for the bot that punches all the
  time; the bug it guards reads 130), so the limit is now 75. **The check has not been run in full since that last
  edit of the limit.**
  If it is now too hard, the knobs are `WindupSeconds`, `MinWindup`, `AttackDamage` and `LadderTiers` at the top of
  `KSGameMode.cpp`, and the three Heat factors in `StartWindup`, `OppIdle` and `Wits`.
  **Windows**: `KSKeys.cpp` is a new source file, and the `Dizzy` clip is new content: both go with the commit.

- **Evening of 2026-11-14 (player, after playing: "the rest is fine")**:
  - **Sound lost after alt+tab**: Unreal turns the sound to nothing while another window has the focus, and a looping
    sound played at nothing is stopped for good. The game now keeps its sound on (`FApp::SetUnfocusedVolumeMultiplier`
    in `BeginPlay`). The check sees the setting; **nobody has alt-tabbed since**, so that the music really comes
    back is unconfirmed.
  - **Bouts about 30% shorter**: the player's punches count 1.15 / 1.43 / 1.86 (were 0.8 / 1.0 / 1.3), his special
    17 (was 12). The bot that makes no mistake wins in 42 and 49 s (was 64 to 73); the check wants 35 to 62.
  - `tools/check.sh`: 138 ok, all passed, on this code.
  - **Linux package** built on this code and run (15 effects, dizzy clip, webcam shut at start, no crash): folder
    1594 MiB; without debug symbols (274), Vulkan debugging layers (263) and the `Saved` folder of test runs (58):
    **1003 MiB = 1052 MB unpacked, 737 MB as a `.tar.gz`**. The same as before today (the dizzy clip is small).
    Still just over 1 GB (decimal) unpacked, just under 1 GiB.
  - **Windows package: not built.** It cannot be made on this machine: the Linux engine has no Windows target
    (`Engine/Binaries` holds Linux and Android only) and cannot compile Direct3D shaders. On the Windows PC: pull,
    add the four Fab packs, run `unreal\package_windows.bat`, and measure `Packaged\Windows` without `.pdb` files.
    The content file will be about the Linux one (564 MiB); the rest is unmeasured.

- **The jam trailer and the page's stills (2026-11-14, night)**: `art/trailer/KO-SEKAI_trailer.mp4`, 40.0 s,
  1920x1080, 68 MB, cut by `art/trailer/make_trailer.py` after `art/trailer/SCRIPT.md`: the film's six shots, then
  the select screen, four arenas, six pieces of bouts, three specials, a knockout, the logo; five new voice-over lines
  in the announcer's voice (`art/trailer/vo.py`, local), the fight music under them, words slammed on in the game's
  fonts, a worn-tape look. The gameplay is eleven takes of the game playing itself, recorded with sound by
  `art/trailer/record.sh` (`takes.sh` runs them all, 13 minutes; 1600x900, stretched to 1080). Stills in
  `art/trailer/shots`: `cover.png` (the fighters walking out of the fire, with the logo; `cover_plain.png` without)
  `cover_seven.png` (the game's seven painted portraits: the film frame has only six fighters, no vampire)
  and six screenshots saved by the game itself (`art/trailer/stills.sh`, lossless; the first set, cut from the
  video, was rejected as pixelated and overbright). The tagline is WEBCAM BOXING; the jam theme's name is used
  nowhere (player, 2026-11-14). All of it, and the script as a text note, is on the Dreamina canvas "KO-SEKAI trailer".
  **Seen in stills only (one picture a second, and three at full size); nobody has watched it or heard it.**
  Unjudged: the mix (the takes carry each arena's own music under the trailer's, thinned but not gone), the voice-over's
  acting (four takes of each line are in `art/trailer/vo`), the pace of the cuts, and
  whether the words cover too much of the fight. The takes show "WEBCAM OFF - KEYBOARD" in a corner; it is smeared
  out, which leaves a soft patch at the bottom right. Nobody boxes at a webcam in it: the words say so, no picture does.
  Three Dreamina shots that would show it (`art/trailer/gen_shots.py`, 70 credits) and the film's fighters shot
  made again with the vampire in it (25 credits, prepared on the canvas) were refused: **the Dreamina
  account is out of credits** (nothing was spent).

- **The film's second shot has all seven fighters (2026-11-14, night)**: Dreamina drew six in "the fighters walk out of
  the fire" (no vampire). Made again from a still with the seven in one row (`art/trailer/shot_nodes.json`,
  `cover_final` and `fighters7v`; 39 credits with the tries), it replaces `art/cinematic/raw/fighters.mp4`, sheet
  `T_KS_Cine_1`; same 61 frames, so `KSCineData.h` did not change. Seen in stills of the game's film; `tools/check.sh`
  150 ok, all passed, on the code merged with the match server. The trailer and the cover use the same shot.

## Built but not verified with a person

- **VS card shows the whole arena (2026-11-14, night)**: the opening shot is taken from high behind the player's
  corner, the card is one band over the bottom quarter (it was two bands leaving a 44% strip), "FIGHT n OF m" is
  smaller and above the heads, and speech bubbles keep off the names under the health bars. Judged from stills of two
  arenas only; the player said "this is better" of the stills' first version.

- **Special by hand (2026-11-14, late evening)**: both hands thrown up or brought down together set off the special
  when the meter is full, next to the shout and the key; calibration has a tenth step that teaches it (a shout counts
  there too), the bout's hint and the CONTROLS screen say it. The check runs made-up moves through the reader only:
  no person has done the move in front of the camera. See `docs/TRACKING.md`, "The special ability by hand".

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
- **Punches online no longer in slow motion** (2026-11-15; player: "the punching motions look different and slow", not
  the network's delay). Two causes. The player's own punch clip was stretched evenly over the punch's whole time in the
  air (0.4 to 0.8 of its speed); it is now drawn as the cabinet's fighter draws his (`WindSnap`, `WindTell`): the tell at
  once, then the fist fired out at 1.5 to 2.8 times the clip's speed, out when the other game's answer is due (time in
  the air plus the link's delay, `PunchLead`). And the times in the air (`Travel` in `KSGameModeOnline.cpp`) were still
  0.45, 0.58, 0.70 from before the cabinet's wind-ups were made quicker; they are now the cabinet's 0.34, 0.44, 0.50,
  so the other player's punches come as fast as the cabinet's and a punch can be thrown again sooner.
  `tools/check.sh`: every punch of a player's own in the three online matches logs its speed and none may be under the
  clip's own. **Not verified**: how it looks and feels to a person (logs and bot bouts only); whether 0.34 s plus the
  grace is enough for a person to answer a jab over a real link. **Still different, and cannot be the same**: against
  the cabinet a punch lands 0.09 s after it is thrown; against a person it has to be in the air long enough for him to
  see it, so the fist starts with a tell instead of leaving at once.
- **Specials against another player** (2026-11-14 night; player: "online fight lacks special fights, the fight should
  be the same in AI mode and online"). The special online is the cabinet's own (`StartSpecial`, `TickSpecial`,
  `SpecialLand`), not a copy: the same meter filled by the same things (2.5 times faster, `OnlineMeterBoost`, an online
  bout being much shorter), the same key, shout or hands, the same warning, move, effects and ways out. The thrower's
  game sends `X`; the other shows it, settles it with its player's stance and answers `XR <H|B|D> <damage> <health>`;
  each game sends its meter (`M`) and both are drawn. A special that lands does 11 (half blocked) and leaves the other
  dizzy for 1.5 s after the move (no punches, no defence); a dodged one leaves the thrower open 1.6 s. Thrown at the
  same moment, the host's happens and the guest keeps his meter. `tools/check.sh`: in each of the three online
  matches a special must be thrown and the two logs must list the same endings and damage (seen: hits and dodges on
  both sides, three specials a player in a bout). **Not verified**: seen by nobody (logs only, no stills of an online
  special); a person throwing one by key, shout or hands; a blocked one (the bots duck or take it); two thrown at
  once; over a real network with its delay. **To watch**: three specials each in a 50 s bout may be too many;
  `OnlineMeterBoost` is the number to turn down. **Still different online**: punch damage (3, 4.5, 6) and the
  cabinet's own tricks (feints, combinations, dizzy spell, adapting), which belong to the cabinet's brain.
- **The match server is deployed** (2026-11-14 evening): an AWS EC2 `t3.micro` (Ubuntu 24.04, Stockholm, instance
  `ko-sekai-server`, free-plan account with 100 dollars of credit until 2027-05-14), `13.60.232.190:7777`, set as
  `[KoSekai] OnlineServer` in `DefaultGame.ini`. `ko_relay.py` is in `/opt/ko-sekai`, runs as the service `ko-sekai`
  (`tools/ko-sekai.service`; user `kosekai`, restarts by itself, starts at boot), ratings in
  `/var/lib/ko-sekai/ko_players.json`, log with `journalctl -u ko-sekai`. The SSH key is the owner's `ko-sekai.pem`
  (not in the repository). To update the server: copy `tools/ko_relay.py` over `/opt/ko-sekai/ko_relay.py` and
  `sudo systemctl restart ko-sekai`. Tested from this PC with no `-KSServer` flag: two editor copies (`-KSLobby=CHECKA`
  and `CHECKB`) met, fought and were rated 1016 and 984 (ping about 170 ms through the server); two copies of the
  Shipping package did the same (seen in the server's log, the package writes none). The test players were removed
  from the ratings file afterwards. **Not verified**: two people on two machines in two networks; the lobby by hand
  or by webcam; a restart of the machine. **Limits**: the address is not an Elastic IP, so stopping and starting the
  machine changes it and every shipped build then points nowhere (allocate an Elastic IP, or never stop it); no
  NetApp volume (it was a sponsor's offer of a session, not a requirement).
- **Match server: lobby, ratings, leaderboard** (2026-11-14; `tools/ko_relay.py`, `EKSLobby::Naming` and `Server`).
  PLAY ONLINE now has a first line FIND A MATCH: the player types a name once (kept with a random key in the user
  settings; the server refuses the name to any other key), then sees who else is on the server, his own rating, rank
  title and place, and the ten best. ENTER or a jab pair challenges the player under the glove; the other is asked
  and accepts or declines. The bout is the same as any online bout, with the server passing the lines on; both games
  report the result and the server moves the ratings (Elo, K 32, start 1000) when they agree. Leaving a bout in
  progress loses it. After a bout the result screens show the new rating; when the opponent leaves, the game goes
  back to the lobby by itself. Ratings live in one JSON file (`--data`), rewritten whole through a temporary file,
  so it can sit on a network volume. `tools/ko_relay_test.py` (22 checks, fake players) and a two-bot lobby match are
  in `tools/check.sh`. **Not verified**: a server anywhere but 127.0.0.1 (none is deployed: no AWS account or CLI on
  this PC, and `OnlineServer` in `DefaultGame.ini` is still not set, so FIND A MATCH in a shipped build says so and
  does nothing); the ratings file on a NetApp volume; the name box and the list used by hand or by webcam (seen in
  stills only: list, leaderboard, challenge prompt); the rating line on the result screens (logged, not seen).
  **Known limits**: the server trusts the two games' reports (two who collude can trade wins); there is no way in the
  game to change a name once typed (delete `PlayerName` from `GameUserSettings.ini`); nothing is encrypted.

## Next

1. **Scripted recording from the player**, standing far enough back: 3 s still, 5 left jabs, 5 right jabs,
   3 hooks and 3 uppercuts each side, 3 blocks of 2 s. Turn it into `tests/tracking/` cases and tune from it.
2. The player tries the harder opponent, the dizzy, the WEBCAM line and the CONTROLS screen (see "Not verified" above).
3. Windows package: build, size under 1 GB, test the webcam there (the OpenCV camera backend differs on Windows).
4. Watch and listen to the trailer (below) and say what to change; upload it and the stills to the jam page.

## Known gaps

- The new face textures add about 200 MB to `Content` before cooking (1.3 GB in all). The packaged size is not
  measured yet; if it passes 1 GB, the face textures are the first thing to shrink (1K, or only the head region).
- Channel Zero has no painted `calm`, `talk` or `blink` screen (Meshy credits: 118 left, 12 per edit).

- No duck or get-up clips.
- Fab assets: none in the project; Fab cannot be driven from here.
- `Content/KS/Tracker/pose_lite.uasset` (5 MB) is an accidental editor import the game does not use.
- Spectators are flat pictures that turn to face the camera; they have no side or back view.
