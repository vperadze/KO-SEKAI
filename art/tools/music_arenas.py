#!/usr/bin/env python3
"""Synthesises one music loop per planned arena (docs/ARENAS.md) into art/audio/M_<Arena>.wav. Needs numpy only.

    python3 art/tools/music_arenas.py            all ten (about a minute, one process per song)
    python3 art/tools/music_arenas.py Space Bar  only these

Same synth as the fight music: the oscillators, filters and effects are imported from music.py, which is left
alone so M_Fight stays as the player approved it. What differs per arena is in SONGS: tempo, key, chords, the
written melody, the lead instrument, the bass and drum patterns, and a few extra sounds (gong, anvil, claps).
After each render the tool prints how much of the melody sits on notes of the chord under it: nobody hears these
before the player does, and a mistyped note shows up there as a low figure.
Import afterwards with art/tools/import_voice_faces.py (it brings in every art/audio/M_*.wav and sets it to loop)."""
import os, sys, wave
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import music as m
np, SR = m.np, m.SR

def note(name):
    p, rest = m.SEMI[name[0]], name[1:]
    while rest[0] in '#b': p += 1 if rest[0] == '#' else -1; rest = rest[1:]
    return p + 12 * (int(rest) + 1)

def chord(sym):
    """'F#m', 'Bb', 'A7' -> (pitch class of the root, semitones of its tones)"""
    root, rest = sym[0], sym[1:]
    if rest[:1] in ('#', 'b'): root, rest = root + rest[0], rest[1:]
    return note(root + '0') % 12, [0, 3 if rest.startswith('m') else 4, 7] + ([10] if rest.endswith('7') else [])

def parse(grid):
    """Eight slots (eighth notes) per bar: '-' holds, '.' rests, 'X/Y' is two sixteenths. -> [[beat, beats, midi]]"""
    toks = grid.split(); assert len(toks) == 8, grid
    notes = []
    for slot, tok in enumerate(toks):
        if tok == '-':
            if notes: notes[-1][1] += 0.5
        elif tok == '.': continue
        elif '/' in tok:
            a, b = tok.split('/'); notes.append([slot * 0.5, 0.25, note(a)]); notes.append([slot * 0.5 + 0.25, 0.25, note(b)])
        else: notes.append([slot * 0.5, 0.5, note(tok)])
    return notes

DEFAULT = dict(swing=0.0, form=['A', 'B', 'C', 'B', 'C2', 'D'], lead='saw', bass='sixteenth', bass_bright=1.0,
               arp='updown', arp_width=0.3, pad={'A': 'stabs', 'B': 'held', 'C': 'stabs', 'D': 'sweep'},
               stabs=(([0, 3, 6], [0, 3, 6, 7]), ([0, 3, 6, 7], [0, 3, 4, 6])), power=False, drums='rock',
               hit='crash', sweeps=False, extra={}, mix={})
SHORT = ['A', 'B', 'C', 'C2', 'D']        # for the slow ones, so every loop stays near 80 s

# The player liked M_Fight and M_Castle and none of the first versions of the other nine, which had their own beats
# and instruments (shuffle, off-beat chords, struck leads, major keys). So every song now follows what those two
# share: a minor key with a major chord on the fifth, 126-146 BPM, sixteenth-note bass, a running arpeggio, the rock
# beat, and a sustained lead with vibrato. An arena shows in the key, tempo, melody and one small extra at most.
SONGS = [
    dict(name='Space', bpm=128, seed=11, lead='sine', sweeps=True, pad={'A': 'held', 'B': 'held', 'C': 'stabs', 'D': 'sweep'}, mix={'hall': 1.6, 'arp': 1.2},
         chords={'A': 'Am Am F G Am Am F G', 'B': 'Am F G Em Am F G E', 'C': 'F G C Am F G E E', 'D': 'Am Am F G'},
         verse=['E5 - A5 - C6 - B5 A5', 'A5 - - - F5 - A5 -', 'B5 - - G5 - - D5 -', 'E5 - - - - - G5 B5',
                'E5 - A5 - C6 - B5 A5', 'C6 - - - A5 - F5 -', 'D6 - - B5 - - G5 -', 'G#5 - - - B5 - E6 -'],
         chorus=['C6 - C6 A5 - F5 A5 -', 'D6 - D6 B5 - G5 B5 -', 'E6 - - - C6 - G5 -', 'A5 - - - . . E5 A5',
                 'C6 - C6 A5 - F5 A5 -', 'D6 - D6 B5 - G5 D6 -', 'E6 - - - D6 - B5 -', 'G#5 - B5 - E6 - - -']),
    dict(name='Beach', bpm=134, seed=12,
         chords={'A': 'Em C G D Em C G D', 'B': 'Em C G D Em C Am B', 'C': 'C D Em Em Am B Em B', 'D': 'Em Em Am B'},
         verse=['B5 - - G5 - - E5 -', 'E5 - G5 - C6 - - -', 'D6 - - B5 - - G5 -', 'A5 - - - F#5 - D5 -',
                'B5 - - G5 - - E5 -', 'E5 - G5 - C6 - E6 -', 'E6 - - C6 - - A5 -', 'B5 - - - - - D#6/B5 A5/F#5'],
         chorus=['E5 G5 - C6 - - G5 -', 'F#5 A5 - D6 - - A5 -', 'G5 B5 - E6 - - B5 -', 'E6 - D6 - B5 - - -',
                 'C6 - A5 C6 - A5 E5 -', 'D#6 - B5 D#6 - B5 F#5 -', 'E6 - - - B5 - G5 -', 'F#5 - - - B5 - D#6 -']),
    dict(name='Bar', bpm=144, seed=13, arp='harp', pad={'A': 'held', 'B': 'held', 'C': 'stabs', 'D': 'sweep'},
         chords={'A': 'Bm Bm Em F# Bm Bm Em F#', 'B': 'Bm G Em F# Bm G Em F#', 'C': 'G A Bm Bm Em F# Bm F#', 'D': 'Bm Bm Em F#'},
         verse=['B5 - - F#5 B5 - C#6 D6', 'D6 - - - B5 - G5 -', 'G5 - - E5 G5 - A5 B5', 'A#5 - - - F#5 - C#5 -',
                'B5 - - F#5 B5 - C#6 D6', 'D6 - - - B5 - D6 -', 'E6 - - - D6 - B5 -', 'C#6 - - - - - A#5 F#5'],
         chorus=['B5 - G5 B5 - G5 D5 -', 'C#6 - A5 C#6 - A5 E5 -', 'D6 - - - B5 - F#5 -', 'B5 - - - . . F#5 B5',
                 'B5 - G5 B5 - G5 E5 -', 'C#6 - A#5 C#6 - A#5 F#5 -', 'D6 - - - F#5 - B5 -', 'A#5 - C#6 - F#5 - - -']),
    dict(name='Rooftop', bpm=126, seed=14, pad={'A': 'held', 'B': 'held', 'C': 'stabs', 'D': 'sweep'}, mix={'hall': 1.4, 'pad': 1.15},
         chords={'A': 'F#m F#m D E F#m F#m D E', 'B': 'F#m D E C#m F#m D E C#', 'C': 'D E A F#m D E C# C#', 'D': 'F#m F#m D E'},
         verse=['C#6 - - A5 - - F#5 -', 'A5 - - F#5 A5 - D6 -', 'B5 - - G#5 - - E5 -', 'G#5 - - - - - E5 G#5',
                'C#6 - - A5 - - F#5 -', 'A5 - - F#5 A5 - D6 -', 'E6 - - B5 - - G#5 -', 'C#6 - - - - - G#5 F5'],
         chorus=['F#5 - A5 - D6 - C#6 -', 'B5 - - - G#5 - B5 -', 'C#6 - - - E6 - C#6 A5', 'A5 - F#5 - - - C#5 F#5',
                 'F#5 - A5 - D6 - C#6 -', 'E6 - - - B5 - E6 -', 'C#6 - - - G#5 - F5 -', 'F5 - G#5 - C#6 - - -']),
    dict(name='Dojo', bpm=138, seed=15, arp='harp', hit='gong',
         chords={'A': 'Cm Cm Ab Bb Cm Cm Ab Bb', 'B': 'Cm Ab Bb Gm Cm Ab Bb G', 'C': 'Ab Bb Cm Cm Fm G Cm G', 'D': 'Cm Cm Ab Bb'},
         verse=['G5 - C6 - Bb5/C6 Bb5 G5 -', 'Eb5 - - - Ab5 - C6 -', 'F5 - Bb5 - D6/C6 Bb5 F5 -', 'G5 - - - D5 - G5 Bb5',
                'G5 - C6 - Bb5/C6 Eb6 C6 -', 'C6 - - - Ab5 - Eb5 -', 'D6 - - Bb5 - - F5 -', 'G5 - B5 - D6 - - -'],
         chorus=['C6 - Ab5 C6 - Ab5 Eb5 -', 'D6 - Bb5 D6 - Bb5 F5 -', 'Eb6 - - - C6 - G5 -', 'G5 - - - . . Eb5 G5',
                 'Ab5 - C6 - F5 - Ab5 -', 'B5 - D6 - G5 - B5 -', 'C6 - - - Eb6 - C6 -', 'D6 - - - B5 - G5 -']),
    dict(name='Castle', bpm=132, seed=16, lead='sine', arp='harp', drums='rock',
         pad={'A': 'held', 'B': 'held', 'C': 'stabs', 'D': 'sweep'}, mix={'hall': 1.6, 'arp': 1.2},
         chords={'A': 'Gm Gm Cm D Gm Gm Cm D', 'B': 'Gm Eb Cm D Gm Eb Cm D', 'C': 'Eb F Gm Gm Cm D Gm D', 'D': 'Gm Gm Cm D'},
         verse=['D5 - G5 - Bb5 - A5 G5', 'G5 - - - Eb5 - G5 -', 'Eb5 - G5 - C6 - Bb5 G5', 'A5 - - - F#5 - D5 -',
                'D5 - G5 - Bb5 - A5 G5', 'Bb5 - - - G5 - Eb5 -', 'C6 - - Bb5 - - G5 -', 'F#5 - A5 - D6 - - -'],
         chorus=['Bb5 - G5 Bb5 - G5 Eb5 -', 'C6 - A5 C6 - A5 F5 -', 'D6 - - - Bb5 - G5 -', 'G5 - - - . . D5 G5',
                 'C6 - G5 C6 - G5 Eb5 -', 'D6 - A5 D6 - A5 F#5 -', 'G5 - Bb5 - D6 - - -', 'D6 - C6 - A5 - F#5 -']),
    dict(name='Gym', bpm=142, seed=17, extra={'clap': [4, 12]},
         chords={'A': 'Fm Fm Db Eb Fm Fm Db Eb', 'B': 'Fm Db Eb Cm Fm Db Eb C', 'C': 'Db Eb Ab Fm Db Eb C C', 'D': 'Fm Fm Db Eb'},
         verse=['C6 - Ab5 - F5 - Ab5 C6', 'Db6 - - - Ab5 - F5 -', 'Bb5 - G5 - Eb5 - G5 Bb5', 'C6 - - - G5 - Eb5 -',
                'C6 - Ab5 - F5 - Ab5 C6', 'Db6 - - - Ab5 - Db6 -', 'Eb6 - - Bb5 - - G5 -', 'E5 - G5 - C6 - - -'],
         chorus=['F5 - Ab5 - Db6 - - Ab5', 'G5 - Bb5 - Eb6 - - Bb5', 'C6 - - Eb6 - C6 Ab5 -', 'Ab5 - F5 - C5 - F5 -',
                 'F5 - Ab5 - Db6 - - Ab5', 'G5 - Bb5 - Eb6 - - Bb5', 'C6 - - - G5 - E5 -', 'G5 - - - C6 - E6 -']),
    dict(name='Scrapyard', bpm=130, seed=18, arp_width=0.5, extra={'anvil': [4, 12]}, pad={'A': 'held', 'B': 'held', 'C': 'stabs', 'D': 'sweep'},
         chords={'A': 'C#m C#m F#m G# C#m C#m F#m G#', 'B': 'C#m A F#m G# C#m A F#m G#', 'C': 'A B C#m C#m F#m G# C#m G#', 'D': 'C#m C#m F#m G#'},
         verse=['G#5 - G#5 - C#6 - B5 G#5', 'A5 - - - E5 - A5 -', 'F#5 - F#5 - A5 - G#5 F#5', 'G#5 - - - C6 - D#6 -',
                'G#5 - G#5 - C#6 - B5 G#5', 'C#6 - - - A5 - E5 -', 'C#6 - - A5 - - F#5 -', 'D#6 - - - C6 - G#5 -'],
         chorus=['E5 - A5 - C#6 - - -', 'F#5 - B5 - D#6 - - -', 'E6 - - - C#6 - G#5 -', 'G#5 - - - . . E5 G#5',
                 'A5 - C#6 A5 - F#5 A5 -', 'C6 - D#6 C6 - G#5 C6 -', 'C#6 - - - E6 - C#6 -', 'D#6 - - - - - C6 G#5']),
    dict(name='Volcano', bpm=146, seed=19, hit='boom',
         chords={'A': 'Dm Dm Bb C Dm Dm Bb C', 'B': 'Dm Dm Bb A Dm Dm Gm A', 'C': 'Bb C Dm Dm Gm A Dm A', 'D': 'Dm Dm Bb A'},
         verse=['D5 - - A5 - - D6 -', 'D6 - C6 - A5 - F5 -', 'Bb5 - - F5 - - D5 -', 'E5 - - - A5 - C#6 -',
                'D5 - - A5 - - D6 -', 'D6 - C6 - A5 - D6 -', 'D6 - - Bb5 - - G5 -', 'A5 - - - - - C#6/A5 G5/E5'],
         chorus=['D6 - D6 Bb5 - F5 Bb5 -', 'E6 - E6 C6 - G5 C6 -', 'D6 - - - A5 - F5 -', 'A5 - - - . . D5 A5',
                 'Bb5 - Bb5 G5 - D5 G5 -', 'C#6 - C#6 A5 - E5 A5 -', 'D6 - - - F5 - A5 -', 'E6 - - - C#6 - A5 -']),
    dict(name='Studio', bpm=136, seed=20, extra={'clap': [12]},
         chords={'A': 'Bbm Bbm Gb Ab Bbm Bbm Gb Ab', 'B': 'Bbm Gb Ab Fm Bbm Gb Ab F', 'C': 'Gb Ab Db Bbm Gb Ab F F', 'D': 'Bbm Bbm Gb Ab'},
         verse=['F5 - Bb5 - Db6 - Bb5 -', 'Db6 - - Bb5 - - Gb5 -', 'C6 - - Ab5 - - Eb5 -', 'F5 - - - Ab5 - C6 -',
                'F5 - Bb5 - Db6 - Bb5 -', 'Db6 - - Bb5 - - Db6 -', 'Eb6 - - C6 - - Ab5 -', 'A5 - - - F5 - C6 -'],
         chorus=['Bb5 - Bb5 Db6 - Bb5 Gb5 -', 'C6 - C6 Eb6 - C6 Ab5 -', 'Db6 - - - Ab5 - F5 -', 'Bb5 - - - . . F5 Bb5',
                 'Bb5 - Bb5 Db6 - Bb5 Gb5 -', 'C6 - C6 Eb6 - C6 Eb6 -', 'C6 - - - A5 - F5 -', 'A5 - C6 - F5 - - -']),
]

# ---------------------------------------------------------------- patterns
def bass_events(kind, third):
    """One bar of bass: [(sixteenth step, length in steps, semitones above the root)]"""
    if kind == 'sixteenth': return [(s, 1, 12 if s in (3, 6, 11, 14) else 0) for s in range(16)]
    if kind == 'eighth': return [(s, 2, 12 if s in (6, 14) else 0) for s in range(0, 16, 2)]
    if kind == 'disco': return [(s, 2, 12 if s % 4 == 2 else 0) for s in range(0, 16, 2)]
    if kind == 'boogie': return [(2 * i, 2, o) for i, o in enumerate([0, 0, third, third, 7, 7, 9, 7])]
    if kind == 'tropic': return [(2 * i, 2, o) for i, o in enumerate([0, None, 0, 7, None, 0, 7, 12]) if o is not None]
    if kind == 'gallop': return [(s, 1, 12 if s in (7, 15) else 0) for s in range(16) if s % 4 != 1]
    if kind == 'funk': return [(0, 2, 0), (3, 1, 0), (4, 1, 12), (6, 2, 0), (8, 1, 0), (10, 1, 12), (11, 1, 0), (14, 1, 10), (15, 1, 12)]
    raise ValueError(kind)

# Per style, the verse beat and the chorus beat. kick/alt (alt: added on every second bar), snare {step: strength},
# hat: every how many steps, and the extras each style brings.
DRUMS = {
    'rock':    (dict(kick=[0, 6, 8], alt=[11], snare={4: 1, 12: 1}, hat=2),
                dict(kick=[0, 4, 8, 12], alt=[14], snare={4: 1, 12: 1}, hat=1)),
    'four':    (dict(kick=[0, 4, 8, 12], snare={4: 1, 12: 1}, hat=1, open=[2, 6, 10, 14], clap=[4, 12]),
                dict(kick=[0, 4, 8, 12], alt=[15], snare={4: 1, 12: 1}, hat=1, open=[2, 6, 10, 14], clap=[4, 12])),
    'half':    (dict(kick=[0, 10], alt=[14], snare={8: 1}, hat=2),
                dict(kick=[0, 3, 6, 10], alt=[14], snare={8: 1}, hat=2, boom=[0])),
    'shuffle': (dict(kick=[0, 8], alt=[14], snare={4: 1, 12: 1}, hat=2),
                dict(kick=[0, 6, 8], alt=[14], snare={4: 1, 12: 1}, hat=2, clap=[4, 12])),
    'taiko':   (dict(kick=[0, 8], snare={}, hat=4, toms={0: 3, 3: 3, 6: 2, 8: 3, 11: 3, 14: 2}, clack=[4, 12]),
                dict(kick=[0, 8], alt=[6], snare={4: 0.8, 12: 0.8}, hat=2, toms={0: 3, 3: 3, 6: 2, 8: 3, 10: 2, 11: 3, 14: 1, 15: 2})),
    'pop':     (dict(kick=[0, 7, 8], snare={4: 1, 12: 1}, hat=2, shaker=True, conga={3: 0, 6: 1, 11: 0, 14: 1}),
                dict(kick=[0, 4, 8, 12], snare={4: 1, 12: 1}, hat=2, shaker=True, conga={3: 0, 6: 1, 10: 1, 11: 0, 14: 1}, clap=[4, 12])),
    'funk':    (dict(kick=[0, 3, 8, 10], alt=[15], snare={4: 1, 12: 1, 7: 0.3, 15: 0.3}, hat=1, cow=[0, 4, 8, 12]),
                dict(kick=[0, 3, 6, 8, 10], alt=[15], snare={4: 1, 12: 1, 7: 0.3}, hat=1, cow=[0, 4, 8, 12], clap=[4, 12])),
    'machine': (dict(kick=[0, 3, 6, 8, 11, 14], snare={4: 1, 12: 1}, hat=1, anvil=[4, 12]),
                dict(kick=[0, 4, 8, 12], alt=[14], snare={4: 1, 12: 1}, hat=1, anvil=[4, 10, 12])),
}

def drum_kit(rng):
    shape, hp, lp, env = m.shape, m.hp, m.lp, m.env
    def T(sec): n = int(sec * SR); return n, np.arange(n) / SR
    def noise(n): return rng.uniform(-1, 1, n)
    def sine(f): return np.sin(2 * np.pi * np.cumsum(f) / SR)
    def kick():
        n, t = T(0.34); y = sine(56 + 170 * np.exp(-t / 0.028)) * np.exp(-t / 0.11)
        c = int(0.004 * SR); y[:c] += noise(c) * 0.4
        return np.tanh(y * 2.2) * env(n, 0.0005, 0.03)
    def snare():
        n, t = T(0.32); nz = shape(noise(n), lambda f: hp(1000)(f) * lp(9500)(f)) * np.exp(-t / 0.10)
        return np.tanh((nz * 1.3 + sine(185 + 95 * np.exp(-t / 0.02)) * np.exp(-t / 0.07) * 0.8) * 1.6) * env(n, 0.0005, 0.03)
    def hat(decay, length):
        n, t = T(length); return shape(noise(n), hp(7500, 3)) * np.exp(-t / decay) * env(n, 0.0005, 0.01)
    def tom(f0):
        n, t = T(0.5); y = sine(f0 * (1 + 0.7 * np.exp(-t / 0.04))) * np.exp(-t / 0.17)
        return np.tanh((y + shape(noise(n), lp(3000)) * np.exp(-t / 0.03) * 0.5) * 1.5) * env(n, 0.0005, 0.05)
    def crash():
        n, t = T(2.2); return shape(noise(n), hp(4500)) * np.exp(-t / 0.55) * env(n, 0.001, 0.2)
    def clap():
        n, t = T(0.28); e = sum(np.where(t >= d, np.exp(-(t - d) / 0.008), 0) for d in (0, 0.011, 0.022)) + np.where(t >= 0.03, np.exp(-(t - 0.03) / 0.07), 0)
        return shape(noise(n), lambda f: hp(900)(f) * lp(4200)(f)) * e * 0.8 * env(n, 0.0005, 0.03)
    def cow():
        n, t = T(0.3); return shape(m.pulse(545, n) + m.pulse(815, n), hp(500)) * np.exp(-t / 0.08) * 0.5 * env(n, 0.0005, 0.03)
    def anvil():
        n, t = T(0.6); y = sum(np.sin(2 * np.pi * f * t) * np.exp(-t / d) for f, d in ((643, 0.25), (1210, 0.22), (1873, 0.18), (2937, 0.14), (4410, 0.10)))
        c = int(0.002 * SR); y[:c] += noise(c) * 2
        return y * 0.3 * env(n, 0.0005, 0.05)
    def gong():
        n, t = T(3.6); y = sum(np.sin(2 * np.pi * f * t + i) * np.exp(-t * (1 + 0.25 * i) / 2.2) / (i + 1) for i, f in enumerate((98, 147.3, 201, 263, 338, 417, 523)))
        return y * 0.5 * env(n, 0.006, 0.4)
    def boom():
        n, t = T(1.3); return np.tanh(sine(50 + 30 * np.exp(-t / 0.08)) * np.exp(-t / 0.45) * 1.8) * env(n, 0.001, 0.2)
    def clack():
        n, t = T(0.08); return (np.sin(2 * np.pi * 1800 * t) + 0.6 * np.sin(2 * np.pi * 905 * t)) * np.exp(-t / 0.012) * env(n, 0.0005, 0.01)
    return dict(K=kick(), S=snare(), CH=hat(0.02, 0.12), OH=hat(0.11, 0.4), SH=hat(0.03, 0.1), CR=crash(), T=[tom(f) for f in (230, 172, 128, 96)],
                clap=clap(), cow=cow(), anvil=anvil(), gong=gong(), boom=boom(), clack=clack())

def lead_line(notes, kind):
    """One mono melody line. -> (dry sound, filter cutoff per sample)"""
    L = m.L
    if not notes: return np.zeros(L), np.full(L, 1000.0)
    if kind in ('bell', 'pluck'):        # struck sounds: every note rings out on its own
        raw = np.zeros(L); cut = np.full(L, 7000.0)
        for st, n, mm in notes:
            f = m.hz(mm); k = min(L - st, n + int((0.5 if kind == 'bell' else 0.25) * SR))
            if k <= 0: continue
            t = np.arange(k) / SR
            if kind == 'bell':           # a steel-pan-like tone: a struck fundamental with its octave and twelfth dying fast
                x = (np.sin(2 * np.pi * f * t + 1.6 * np.exp(-t / 0.05) * np.sin(2 * np.pi * 2 * f * t))
                     + 0.45 * np.sin(2 * np.pi * 2 * f * t) * np.exp(-t / 0.12) + 0.2 * np.sin(2 * np.pi * 3 * f * t) * np.exp(-t / 0.06)) * np.exp(-t / 0.32)
            else:                        # a plucked string: bright at the pick, the pitch settling
                ff = f * (1 + 0.015 * np.exp(-t / 0.025))
                x = (m.saw(ff, k) * 0.7 + m.pulse(ff, k, 0.18) * 0.5) * np.exp(-t / 0.2)
                cut[st:st + k] = 900 + 7500 * np.exp(-t / 0.07)
            raw[st:st + k] += x * m.env(k, 0.002, 0.03) * 0.8
        return raw, cut
    glide, depth, rate = {'saw': (0.014, 0.28, 5.8), 'square': (0.002, 0.10, 6.0), 'sine': (0.045, 0.55, 5.2), 'organ': (0.002, 0.12, 6.6)}[kind]
    notes = sorted(notes); pitch = np.full(L, float(notes[0][2])); gate = np.zeros(L); vib = np.zeros(L)
    for k, (st, n, mm) in enumerate(notes):
        end = notes[k + 1][0] if k + 1 < len(notes) else L
        pitch[st:end] = mm; gate[st:st + int(n * 0.94)] = 1
        if n > 0.3 * SR:
            e = min(L, st + n); vib[st:e] = np.clip((np.arange(n) - 0.16 * SR) / (0.2 * SR), 0, 1)[:e - st]
    if kind == 'organ': vib[:] = 1
    kern = np.exp(-np.arange(int(max(glide * 6, 0.01) * SR)) / (glide * SR)); pitch = m.conv(pitch - pitch[0], kern / kern.sum()) + pitch[0]
    h = np.hanning(int(0.012 * SR)); amp = np.clip(m.conv(gate, h / h.sum()), 0, 1)
    t = np.arange(L) / SR
    f = m.hz(pitch + depth * vib * np.sin(2 * np.pi * rate * t))
    if kind == 'saw':
        raw = m.saw(f, L) * 0.6 + m.pulse(f * 1.004, L, 0.32) * 0.5 + m.saw(f * 0.5, L, 0.3) * 0.15
        cut = np.clip(f * 3.2 + 900, 300, 9000)
    elif kind == 'square':
        raw = m.pulse(f, L, 0.5) * 0.5 + m.pulse(f * 2, L, 0.25) * 0.14
        cut = np.clip(f * 4 + 1500, 300, 9000)
    else:
        ph = 2 * np.pi * np.cumsum(f) / SR
        if kind == 'sine': raw = np.sin(ph) * 0.9 + m.saw(f, L) * 0.1
        else: raw = sum(a * np.sin(k * ph) for k, a in ((1, 1.0), (2, 0.7), (3, 0.4), (4, 0.3), (6, 0.15))) * 0.4
        cut = np.full(L, 6500.0)
    return raw * amp, cut * (0.55 + 0.45 * amp)

# ---------------------------------------------------------------- one song
def render(song):
    S = dict(DEFAULT, **song)
    rng = m.rng = np.random.default_rng(S['seed'])
    m.BEAT = 60.0 / S['bpm']
    bars, first = [], []                   # bars: (section, bar in section, chord); first: (section, its first bar)
    for sec in S['form']:
        first.append((sec, len(bars)))
        for k, c in enumerate(S['chords'][sec[0]].split()): bars.append((sec, k, c))
    BARS = len(bars)
    N = m.N = int(round(BARS * 4 * m.BEAT * SR)); L = m.L = N + 4 * SR
    at, dur, put, env, hz = m.at, m.dur, m.put, m.env, m.hz
    step = dur(0.25); sw = S['swing']
    def spos(bar, s16):                    # where a sixteenth step falls, with the song's swing
        beat, sub = divmod(int(s16), 4)
        return at(bar) + dur(beat + (0, 0.25 + sw / 2, 0.5 + sw, 0.75 + sw / 2)[sub])

    bass = np.zeros(L); bass_cut = np.full(L, 300.0)
    arp = np.zeros(L); arp_cut = np.full(L, 800.0)
    pad = np.zeros(L); pad_cut = np.full(L, 900.0)
    for bar, (sec, k, c) in enumerate(bars):
        pc, tones = chord(c); third = tones[1]
        root = 24 + pc
        while root < 33: root += 12
        pr = 36 + pc
        if pr < 45: pr += 12
        # --- bass
        if sec == 'D': events = [(0, 8, 0), (8, 8, 0)] if k < 2 else [(s, 2, 12 if s in (6, 14) else 0) for s in range(0, 16, 2)]
        else: events = bass_events(S['bass'], third)
        for s, steps, up in events:
            n = int(step * steps * (0.86 if steps == 1 else 0.92 if steps == 2 else 0.95)); f = hz(root + up); t = np.arange(n) / SR
            x = m.saw(f, n) * 0.65 + m.pulse(f, n, 0.4, 0.25) * 0.45 + np.sin(2 * np.pi * f * t) * 0.3
            st = spos(bar, s); e = min(L, st + n)
            put(bass, st, x * env(n, 0.003, 0.012) * (1.0 if s % 4 == 0 else 0.85))
            bass_cut[st:e] = (260 + (2600 if up >= 12 else 1900) * S['bass_bright'] * np.exp(-t / (0.07 if steps == 1 else 0.14 if steps == 2 else 0.5)))[:e - st]
        # --- arpeggio
        if S['arp'] and not (sec == 'A' and k < 2):
            a0 = pr + 12; tn = [a0 + x for x in tones[:3]] + [a0 + 12 + x for x in tones[:3]]
            if S['arp'] == 'slow': seq, decay, length = [(2 * i, tn[o]) for i, o in enumerate([0, 2, 4, 2, 5, 4, 2, 1])], 0.3, 1.9
            else: seq, decay, length = [(s, tn[o]) for s, o in enumerate([0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 3])], 0.11, 0.9
            for s, mm in seq:
                n = int(step * length); t = np.arange(n) / SR; f = hz(mm); st = spos(bar, s); e = min(L, st + n)
                x = m.saw(f, n) * 0.6 + m.pulse(f * 2, n, 0.2) * 0.35 if S['arp'] == 'harp' else m.pulse(f, n, S['arp_width'])
                put(arp, st, x * np.exp(-t / decay) * env(n, 0.002, 0.01) * (1.0 if s % 2 == 0 else 0.7))
                arp_cut[st:e] = (700 + (5200 if sec.startswith('C') else 3600) * np.exp(-t / (0.05 if decay < 0.2 else 0.2)))[:e - st]
        # --- chords
        style = S['pad'][sec[0]]
        voicing = [pr, pr + 7, pr + 12, pr + 19] if S['power'] else [pr, pr + 7, pr + 12, pr + 12 + third] + ([pr + 10] if len(tones) > 3 else [])
        if sec[0] != 'B' and not S['power']: voicing.append(pr + 24 + third)
        if style == 'stabs': hits = [(sl * 2, 0.42) for sl in S['stabs'][0 if sec == 'A' else 1][k % 2]]
        elif style == 'skank': hits = [(b * 4 + 2, 0.3) for b in range(4)]
        elif style == 'none': hits = []
        else: hits = [(0, 4.0)]
        for s, beats in hits:
            n = dur(beats); rel = int(0.09 * SR); t = np.arange(n + rel) / SR; x = np.zeros(n + rel)
            for mm in voicing:
                for cents in (-11, -4, 0, 5, 12): x += m.saw(hz(mm) * 2 ** (cents / 1200.0), n + rel, rng.random())
            st = spos(bar, s); e = min(L, st + n + rel)
            en = np.ones(n + rel); a = int((0.008 if beats < 1 else 0.06) * SR); en[:a] = np.linspace(0, 1, a); en[n:] = np.linspace(1, 0, rel)
            put(pad, st, x * en * 0.12 * (5.0 / len(voicing)))
            t = t[:e - st]
            if style == 'held': pad_cut[st:e] = 1100 + 250 * np.sin(2 * np.pi * 0.2 * (st / SR + t))
            elif style == 'sweep': pad_cut[st:e] = 350 * (16.0 ** ((k + t / (4 * m.BEAT)) / 4.0))
            else: pad_cut[st:e] = 700 + 4300 * np.minimum(t / 0.035, 1) * np.exp(-np.maximum(t - 0.035, 0) / 0.22)

    # --- lead: the verse over B, the chorus over C; the last chorus gets a second voice on the chord note below
    def below(mm, c):
        pc, tones = chord(c); pcs = {(pc + x) % 12 for x in tones}; h = mm - 3
        while h % 12 not in pcs: h -= 1
        return h
    main, second, on_chord, total = [], [], 0.0, 0.0
    for sec, b0 in first:
        grids = S['verse'] if sec == 'B' else S['chorus'] if sec[0] == 'C' else []
        for i, g in enumerate(grids):
            pc, tones = chord(bars[b0 + i][2])
            for beat, beats, mm in parse(g):
                ev = (spos(b0 + i, round(beat * 4)), dur(beats), mm)
                main.append(ev); total += beats; on_chord += beats * ((mm - pc) % 12 in tones)
                if sec == 'C2': second.append((ev[0], ev[1], below(mm, bars[b0 + i][2])))
    lead, lead_cut = lead_line(main, S['lead'])
    harm, harm_cut = lead_line(second, S['lead'])
    bass, arp, pad, lead, harm = [m.svf(x, c, q) for x, c, q in ((bass, bass_cut, 2.2), (arp, arp_cut, 1.6), (pad, pad_cut, 1.1), (lead, lead_cut, 1.3), (harm, harm_cut, 1.3))]

    # --- drums
    kit = drum_kit(rng); pan = m.pan
    dry = np.zeros((2, L)); big = np.zeros((2, L)); kicks = []
    for bar, (sec, k, c) in enumerate(bars):
        fill = sec != 'D' and k == 7
        if k == 0:
            if S['hit'] != 'gong': put(dry, at(bar), pan(kit['CR'], 0.4) * 0.33)
            if S['hit'] in ('gong', 'boom'): put(dry, at(bar), pan(kit[S['hit']], 0.5) * 0.6)
        if sec == 'D':
            P = {}
            kick_at = [0] if k < 2 else [0, 8]
            snare_at = {8: 1.0} if k < 3 else {0: 0.45, 2: 0.55, 4: 0.7, 6: 0.85}
            hats = {s: 0.5 for s in range(0, 16, 4)} if k < 3 else {}
            toms = {s: (s - 8) // 2 for s in range(8, 16)} if k == 3 else {}
        else:
            P = DRUMS[S['drums']][1 if sec[0] == 'C' else 0]
            kick_at = P['kick'] + (P.get('alt', []) if k % 2 else [])
            snare_at = dict(P['snare'])
            if fill: snare_at[10] = 0.8
            hats = {s: (1.0 if s % 4 == 0 else 0.6 if s % 2 == 0 else 0.35) for s in range(0, 16, P['hat'])}
            toms = {13: 0, 14: 1, 15: 2} if fill else {}
            for s, which in ({} if fill else P.get('toms', {})).items():
                x = pan(kit['T'][which], 0.3 + 0.13 * which) * (0.6 if s % 8 == 0 else 0.42); put(dry, spos(bar, s), x); put(big, spos(bar, s), x)
            for s, which in P.get('conga', {}).items(): put(dry, spos(bar, s), pan(kit['T'][which], 0.3 + 0.4 * which) * 0.2)
            for name, gain, where in (('clap', 0.5, 0.45), ('cow', 0.32, 0.7), ('anvil', 0.5, 0.35), ('clack', 0.4, 0.6), ('boom', 0.5, 0.5)):
                for s in list(P.get(name, [])) + S['extra'].get(name, []): put(dry, spos(bar, s), pan(kit[name], where) * gain * (0.6 if name in S['extra'] else 1))
            if P.get('shaker'):
                for s in range(16): put(dry, spos(bar, s), pan(kit['SH'], 0.3) * (0.09 if s % 2 else 0.05))
        for s in kick_at: put(dry, spos(bar, s), pan(kit['K'], 0.5) * 0.66); kicks.append(spos(bar, s))
        for s, v in snare_at.items(): x = pan(kit['S'], 0.5) * 0.5 * v; put(dry, spos(bar, s), x); put(big, spos(bar, s), x)
        for s, v in hats.items():
            soft_open = s in P.get('open', [])
            is_open = soft_open or (s == 14 and k % 2 == 1 and not toms and sec != 'D')
            put(dry, spos(bar, s), pan(kit['OH'] if is_open else kit['CH'], 0.62) * 0.2 * v * (0.7 if soft_open else 1))
        for s, which in toms.items(): x = pan(kit['T'][which], 0.25 + 0.17 * which) * 0.62; put(dry, spos(bar, s), x); put(big, spos(bar, s), x)
    def swell(bar, length_bars, gain):     # a hiss that rises into a bar line
        n = at(length_bars); r = m.shape(rng.uniform(-1, 1, n), m.hp(2500)) * np.linspace(0, 1, n) ** 2.5 * gain
        put(dry, at(bar) - n, np.array([r, np.roll(r, 37)]))
    swell(BARS, 2, 0.16)
    if S['sweeps']:
        for sec, b0 in first[1:]: swell(b0, 1, 0.1)

    # --- mix
    g = dict(drums=1.0, bass=1.0, arp=1.0, pad=1.0, lead=1.0, hall=1.0); g.update(S['mix'])
    duck = np.ones(L); n = int(0.26 * SR); curve = 1 - 0.55 * np.exp(-np.arange(n) / (0.085 * SR))
    for st in kicks:
        k = min(n, L - st); duck[st:st + k] = np.minimum(duck[st:st + k], curve[:k])
    hall, gated = m.room(2.3, 0.42), m.room(0.27, 0.6, gate=True); wet = m.wet
    bus = {}
    bus['drums'] = (dry + wet(big, gated) * 0.55) * g['drums']
    bus['bass'] = pan(np.tanh(bass * 1.4) * duck, 0.5) * 0.50 * g['bass']
    a = m.pingpong(pan(arp, 0.5), 0.75, mix=0.38) * duck
    bus['arp'] = (a + wet(a, hall) * 0.18 * g['hall']) * 0.26 * g['arp']
    p = m.chorus(pad, 0.5, 0.004, 0.014, 0.6) * duck
    bus['pad'] = (p + wet(p, hall) * 0.22 * g['hall']) * 0.52 * g['pad']
    ld = m.chorus(lead, 0.8, 0.002, 0.009, 0.35) * np.array([[1.0], [0.92]]) + m.chorus(harm, 0.7, 0.002, 0.010, 0.35) * np.array([[0.55], [0.8]])
    ld = m.pingpong(ld, 1.0, fb=0.35, mix=0.2, reps=4)
    bus['lead'] = (ld + wet(ld, hall) * 0.3 * g['hall']) * 0.36 * g['lead']
    mix = sum(bus.values())
    mix[:, :L - N] += mix[:, N:]; mix = mix[:, :N]
    mix = np.array([m.shape(ch, lambda f: m.hp(38)(f) * (1 + 0.6 * m.hp(6000, 1)(f))) for ch in mix])
    mix = np.tanh(mix * 0.95 / np.percentile(np.abs(mix), 99.9))
    mix = mix / np.abs(mix).max() * 0.89

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'audio', 'M_%s.wav' % S['name'])
    w = wave.open(out, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((mix.T * 32767).astype('<i2').tobytes()); w.close()
    def db(x): return 20 * np.log10(max(np.sqrt(np.mean(x * x)), 1e-9))
    mono = mix.mean(0); spec = np.abs(np.fft.rfft(mono)) ** 2; f = np.fft.rfftfreq(len(mono), 1.0 / SR)
    bands = ' '.join('%d%%' % round(100 * spec[(f >= lo) & (f < hi)].sum() / spec.sum()) for lo, hi in ((0, 60), (60, 250), (250, 1000), (1000, 4000), (4000, 22050)))
    return ('M_%-10s %3d BPM %5.1f s  level %.1f dB  parts(dB) %s  bands %s  melody on chord notes %d%%'
            % (S['name'], S['bpm'], N / SR, db(mix), ' '.join('%s %.0f' % (k, db(v[:, :N])) for k, v in bus.items()), bands, round(100 * on_chord / total)))

if __name__ == '__main__':
    want = [s for s in SONGS if not sys.argv[1:] or s['name'] in sys.argv[1:]]
    with Pool(min(len(want), 10), maxtasksperchild=1) as pool:
        for line in pool.imap(render, want): print(line, flush=True)
