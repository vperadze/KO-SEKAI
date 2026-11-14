# KO-SEKAI — the jam trailer (40 s)

A worn VHS tape of a 1980s action-movie trailer: tracking lines, the recorder's PLAY mark, a deep voice, words that
slam onto the picture. First the story (the opening film's shots), then what you do (real gameplay), then the name.

Made by `art/trailer/make_trailer.py` from the takes of `art/trailer/takes.sh`; the cut itself is the three tables
at the top of that file (`CUT`, `TITLES`, `SOUND`). The result is `art/trailer/KO-SEKAI_trailer.mp4`.

| Time | Picture | Voice and sound | Words on screen |
|---|---|---|---|
| 0:00–0:03 | Film: flight over the neon city. PLAY mark, tape counter | VO: "In nineteen eighty-seven, one game was too much for the world." | 1987. |
| 0:03–0:06 | Film: the fighters walk out of the fire | music | THE GREATEST GAME NEVER RELEASED |
| 0:06–0:08 | Film: the arcade hall, the screens going dark | VO: "Seven fighters. Cancelled! Three weeks before launch." | CANCELLED. |
| 0:08–0:10 | Film: the alley, the last cabinet, the man walking to it | VO: "One cabinet survived. And it wants a challenger!" | ONE CABINET SURVIVED. |
| 0:10–0:13 | Film: the gloves bang together; the dive into the screen | slam | INSERT YOURSELF. |
| 0:13–0:17 | Game: the select screen, all seven fighters dancing, one after another | Ring announcer: "Challenger! Choose your fighter!" | 7 FIGHTERS |
| 0:17–0:20 | Game: the opening shot of four arenas with the VS card (volcano, space, dojo, castle) | Ring announcer: "In this corner: Grandma Thunder!" | 11 ARENAS |
| 0:20–0:28 | Game: six pieces of bouts with the game's own sound: hits with POW!/BAM!/KRAK!, the speech bubbles ("Oh! My fucking hip!", "Ow! Fuck! Not the screen!", "Shit. There goes my warranty."), dodges, a counter, DIZZY!, COMBO BROKEN! | VO: "No gamepad! Your fists are the controller!" | WEBCAM BOXING / NO GAMEPAD. JUST YOUR FISTS. / DODGE. BLOCK. COUNTER. |
| 0:28–0:33 | Game: three specials: Sir Lasercorn's rainbow laser, Grandma Thunder's lightning, Turbotron's blast | the game's sound | SHOUT TO UNLEASH YOUR SPECIAL |
| 0:33–0:36 | Game: the knockout, K.O.!!, the YOU WIN card | Ring announcer: "Knockout! It's all over!", the crowd | |
| 0:36–0:40 | The menu's sunset ring, the logo slams in. STOP mark | VO: "Rated F. For fists!" | KO-SEKAI / WEBCAM BOXING / UNRATED. UNRELEASED. UNTIL NOW. / EPIC MEGAJAM 2026 |

## Where the parts come from

- **Film shots**: `art/cinematic/raw/*.mp4`, the opening film's six Dreamina shots.
- **Gameplay**: `art/trailer/raw/*.mkv`, eleven takes of the game playing itself (`-KSAuto`), recorded with sound by
  `art/trailer/record.sh`. Nobody is at the webcam in them: the game is driven by its own bot.
- **Voice-over**: `art/trailer/vo/VO_*.wav`, five new lines in the ring announcer's voice (`art/trailer/vo.py`, local,
  no credits; all four takes of each line are kept beside the chosen one). The ring announcer's three lines are the
  game's own recordings.
- **Music**: the game's fight loop `art/audio/M_Fight.wav`, dropping under the voices.
- **Stills for the jam page**: `art/trailer/shots/` — `cover.png` (the film frame of the fighters with the logo; the film
  drew only six of the seven, the vampire is missing), `cover_plain.png` (without logo), `cover_seven.png` (the game's
  seven painted portraits), and six screenshots saved by the game itself (`art/trailer/stills.sh`: lossless, 1600x900).
- **The words**: the tagline is WEBCAM BOXING. The jam theme's name is not used anywhere, at the player's wish.

## Three more shots, not made

`art/trailer/gen_shots.py` holds three Dreamina shots (a tape pushed into a video recorder as the opener, a man
punching at his television with a camera on it, the seven fighters flexing on a stage): 70 credits quoted. The
account had too few credits when it was tried, so nothing was made or spent. With credits:
`python3 art/trailer/gen_shots.py run 70`, then put `('extra/tape', 0, 2.6, 1, 0.5, '')` and the like into `CUT`.

A fourth shot is prepared on the canvas and waits for credits too (25): the fighters walking out of the fire made again
with the vampire in the row ("cinematic fighters (seven, with the vampire)", node `fighters7` in `shot_nodes.json`).
