# Arenas (plan)

Written 2026-11-13. **All ten are built** (`KSArenaData.h`, `KSWorld::BuildArena`, `AKSGameMode::UseArena`), with
the original hall as arena 0. State and what is unverified: `docs/RESUME.md`.

Since 2026-11-13 the hall itself is built from Meshy models: a kit shared by all eleven arenas (risers, barriers,
truss gantries, lamps, posts, scoreboard, loudspeakers, round podiums) and one landmark per themed arena, named like
the arena's key (`/Game/KS/Props/<Key>/SM_<Key>`), placed by the `Landmarks` table in `KSWorld::BuildArena`. A new
arena therefore also needs one model (30 Meshy credits) and a row in that table; without it the arena simply has no
landmark.

## What an arena is, so that ten are affordable

An arena is the same ring with five things swapped. No new geometry, no new fighters, no new rules.

| Part | Today | Per arena |
|---|---|---|
| Floor | one ring mat picture | one new mat picture |
| Surround | one crowd-stand wall picture on three sides | one new wall picture (the place itself) |
| Light | white key light, photo flashes | key colour, fill colour, fog colour |
| Crowd | 46 sprite spectators | the same sprites with a tint; a few arena-specific ones later |
| Music | `M_Fight` | `M_<Arena>` |

Cost per arena, roughly: two Dreamina pictures (ask before spending credits), a headless import, one row in an
arena table in code. The first arena costs more: `FKSArena` has to take its pictures and colours from a table
instead of fixed names, and `KSAudio::Music` has to take a track name (it loads `M_Fight` only).

## The ten

Each fighter has a home; three arenas belong to nobody and suit the last bout of the ladder.

| # | Arena | Home of | The place | Light | Music (`art/audio/M_<name>.wav`) |
|---|---|---|---|---|---|
| 1 | **Orbital Ring** (`Space`) | Sir Lasercorn | A ring on a glass deck of a space station; Earth turning below, stars, a docking arm. Crowd in the dark behind railings. | Cold blue key, magenta rim | 128 BPM, A minor. Theremin-like lead as in Castle, hiss swells into every section. |
| 2 | **Sunset Pier** (`Beach`) | Grandma Thunder | A ring on the sand by a pier at sunset; palms, deck chairs, a retirement resort's bingo banner. | Orange low sun, long shadows | 134 BPM, E minor on brighter chords (Em C G D). The lightest of the set. |
| 3 | **The Rusty Tap** (`Bar`) | Sergeant Mullet | A bar-room brawl: tables pushed back to make the ring, neon beer signs, a jukebox, dart board. | Warm tungsten, green neon | 144 BPM, B minor. Harpsichord-like arpeggio, a restless lead. |
| 4 | **Neon Rooftop** (`Rooftop`) | nobody | A rooftop at night in the rain; city skyline, a giant billboard, steam from vents. | Pink and cyan neon, wet reflections | 126 BPM, F# minor. The slowest: held pads, a lead that falls in long steps. |
| 5 | **Mountain Dojo** (`Dojo`) | nobody | A wooden dojo open to a mountain valley; paper screens, a gong, cherry trees. | Soft morning light | 138 BPM, C minor. Five-note turns in the melody, a gong on every section. |
| 6 | **Castle Hairspray** (`Castle`) | Count Hairspray | A castle hall: candelabras, a pipe organ, portraits of the Count with different hairdos, bats. | Candle orange against moonlight blue | 132 BPM, G minor. A wailing theremin-like lead, harpsichord arpeggio. **Approved by the player.** |
| 7 | **Sweat Studio** (`Gym`) | Aerobics Annihilator | An 80s aerobics studio: mirrors, step boards, a wall of leg-warmer posters, a boom box. | Bright white and hot pink | 142 BPM, F minor. Claps on the snare, a climbing chorus. |
| 8 | **Scrapyard** (`Scrapyard`) | TURBOTRON | A ring between walls of crushed cars; a crane with a magnet, sparks, a conveyor belt. | Sodium yellow, welding flashes | 130 BPM, C# minor. Repeated-note lead, a quiet anvil with the snare. |
| 9 | **Volcano** (`Volcano`) | nobody (last bout) | A stone platform in a crater; lava falls, smoke, glowing cracks in the mat. | Red from below, black sky | 146 BPM, D minor. The fastest; a boom on every section. |
| 10 | **Studio 0** (`Studio`) | Channel Zero | A game-show set: an applause sign, a prize wheel, studio cameras, a wall of televisions. | Flat studio white, coloured spots | 136 BPM, Bb minor. A clap on the last beat of each bar. |

`M_Fight` (140 BPM, E minor) stays the default and the music of the present ring.

## Music: how it is made and what is unknown

- `python3 art/tools/music_arenas.py [names]` renders them (40 s for all ten); `art/tools/import_voice_faces.py`
  imports every `art/audio/M_*.wav` to `/Game/KS/Audio` and sets it to loop. All ten are imported.
- Same synth as `M_Fight` (`art/tools/music.py`, untouched). Each song is one entry in `SONGS`: tempo, chords,
  a written verse and chorus melody, and at most one small extra sound.
- **All eleven follow one recipe**, because the player approved `M_Fight` and `M_Castle` and rejected the first
  versions of the other nine, which each had their own beat and instruments (shuffle, off-beat chords, steel pan,
  plucked leads, major keys, funk). The recipe: minor key with a major chord on the fifth, 126-146 BPM,
  sixteenth-note bass, running arpeggio, rock beat, sustained lead with vibrato. Do not stray from it again
  without asking.
- Form: groove, verse, chorus, verse, chorus with a second voice, break; 72 to 84 s; the loop has no seam.
- **Nobody has listened to the nine rewritten ones.** Checked by numbers only: level, balance of the parts, and
  that 94-100% of each melody sits on notes of its chord.
- Size: the ten add 110 MB to `Content` before cooking. Cooked audio is compressed; if the package nears 1 GB,
  drop the unused ones or render at 32 kHz.

## Order to build, if there is time before the deadline

1. Make `FKSArena` and `KSAudio::Music` table-driven with the present ring as entry 0 (no visible change).
2. One arena end to end. Volcano or Neon Rooftop: dark surroundings forgive a flat backdrop picture best.
3. Home arenas chosen by the opponent of the bout; the remaining ones for the last bout.
