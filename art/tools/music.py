#!/usr/bin/env python3
"""Synthesises the fight music (an original 80s synth-rock loop) into art/audio/M_Fight.wav. Needs numpy only.
Everything is made here: oscillators, filters, drums, chorus, echo, hall and the gated drum room.
The track is 44 bars at 140 BPM in E minor and loops without a seam (effect tails wrap round to the start).
Sections: A groove (8 bars), B verse (8), C chorus (8), B again, C with a second lead voice, D break (4)."""
import os, sys, wave
from multiprocessing import Pool
import numpy as np

SR, BPM, BARS = 44100, 140, 44
BEAT = 60.0 / BPM
N = int(round(BARS * 4 * BEAT * SR))      # the loop
L = N + 4 * SR                            # plus room for tails, folded back at the end
rng = np.random.default_rng(7)

def at(bar, beat=0.0): return int(round((bar * 4 + beat) * BEAT * SR))
def dur(beats): return int(round(beats * BEAT * SR))

SEMI = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
def midi(name):
    p, rest = SEMI[name[0]], name[1:]
    if rest[0] == '#': p, rest = p + 1, rest[1:]
    return p + 12 * (int(rest) + 1)
def hz(m): return 440.0 * 2 ** ((m - 69) / 12.0)

# ---------------------------------------------------------------- sound sources and effects
def saw(freq, n, phase0=0.0):
    """Saw wave without aliasing (polyBLEP). freq is a number or an array of n values."""
    dt = np.broadcast_to(np.asarray(freq, float) / SR, (n,))
    ph = (phase0 + np.cumsum(dt)) % 1.0
    y = 2 * ph - 1
    a = ph < dt; x = ph[a] / dt[a]; y[a] -= x + x - x * x - 1
    b = ph > 1 - dt; x = (ph[b] - 1) / dt[b]; y[b] -= x * x + x + x + 1
    return y
def pulse(freq, n, width=0.5, phase0=0.0): return saw(freq, n, phase0) - saw(freq, n, phase0 + width)

def svf(x, cutoff, q):
    """Resonant low-pass whose cutoff changes every sample (one filter per instrument, like a mono synth)."""
    g = np.tan(np.pi * np.clip(cutoff, 30, SR * 0.45) / SR); k = 1.0 / q
    a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2
    out = [0.0] * len(x); ic1 = ic2 = 0.0; i = 0
    for xi, b1, b2, b3 in zip(x.tolist(), a1.tolist(), a2.tolist(), a3.tolist()):
        v3 = xi - ic2; v1 = b1 * ic1 + b2 * v3; v2 = ic2 + b2 * ic1 + b3 * v3
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; out[i] = v2; i += 1
    return np.array(out)

def conv(x, ir):
    n = len(x) + len(ir) - 1; m = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(ir, m), m)[:len(x)]
def shape(x, fn):
    return np.fft.irfft(np.fft.rfft(x) * fn(np.fft.rfftfreq(len(x), 1.0 / SR)), len(x))
def lp(fc, order=2): return lambda f: 1 / np.sqrt(1 + (f / fc) ** (2 * order))
def hp(fc, order=2): return lambda f: 1 / np.sqrt(1 + (fc / np.maximum(f, 1e-6)) ** (2 * order))

def chorus(x, rate=0.7, depth=0.0028, base=0.011, mix=0.5):
    t = np.arange(len(x), dtype=float); out = []
    for ph in (0.0, np.pi / 2):
        d = (base + depth * np.sin(2 * np.pi * rate * t / SR + ph)) * SR
        out.append(x * (1 - mix / 2) + mix * np.interp(t - d, t, x))
    return np.array(out)
def pingpong(st, beats, fb=0.42, mix=0.3, reps=6):
    d = dur(beats); out = st.copy(); mono = st.mean(0)
    for k in range(1, reps + 1): out[k % 2, k * d:] += mix * fb ** (k - 1) * mono[:L - k * d]
    return out
def room(length, decay, gate=False):
    n = int(length * SR); t = np.arange(n) / SR; ir = []
    for _ in range(2):
        x = shape(rng.uniform(-1, 1, n), lambda f: lp(6500)(f) * hp(280)(f)) * np.exp(-t / decay)
        if gate: x[-int(0.03 * SR):] *= np.linspace(1, 0, int(0.03 * SR))
        ir.append(np.concatenate([np.zeros(int(0.012 * SR)), x / np.sqrt((x * x).sum())]))
    return ir
def wet(st, ir): return np.array([conv(st.mean(0), ir[0]), conv(st.mean(0), ir[1])])
def pan(x, p): return np.array([x * np.cos(p * np.pi / 2), x * np.sin(p * np.pi / 2)])     # p: 0 left .. 1 right
def env(n, attack, release):
    e = np.ones(n); a = min(n, int(attack * SR)); r = min(n, int(release * SR))
    e[:a] = np.linspace(0, 1, a); e[n - r:] *= np.linspace(1, 0, r); return e
def put(buf, start, x):
    if start >= L: return
    x = x[..., :L - start]; buf[..., start:start + x.shape[-1]] += x

# ---------------------------------------------------------------- the song
CHORD = {  # bass root, pad voicing, arpeggio tones
    'Em': ('E2', ['E3', 'B3', 'E4', 'G4'], ['E4', 'G4', 'B4', 'E5', 'G5', 'B5']),
    'C':  ('C2', ['C3', 'G3', 'C4', 'E4'], ['C4', 'E4', 'G4', 'C5', 'E5', 'G5']),
    'D':  ('D2', ['D3', 'A3', 'D4', 'F#4'], ['D4', 'F#4', 'A4', 'D5', 'F#5', 'A5']),
    'Bm': ('B1', ['B2', 'F#3', 'B3', 'D4'], ['B3', 'D4', 'F#4', 'B4', 'D5', 'F#5']),
    'B':  ('B1', ['B2', 'F#3', 'B3', 'D#4'], ['B3', 'D#4', 'F#4', 'B4', 'D#5', 'F#5']),
    'G':  ('G2', ['G3', 'B3', 'D4', 'G4'], ['G4', 'B4', 'D5', 'G5', 'B5', 'D6']),
}
A = ['Em', 'Em', 'C', 'D', 'Em', 'Em', 'C', 'D']
B = ['Em', 'C', 'D', 'Bm', 'Em', 'C', 'D', 'B']
C = ['C', 'D', 'G', 'Em', 'C', 'D', 'B', 'B']
D = ['Em', 'Em', 'C', 'D']
SONG = [('A', A), ('B', B), ('C', C), ('B', B), ('C2', C), ('D', D)]
BAR_CHORD, BAR_SEC, BAR_IN_SEC = [], [], []
for sec, chords in SONG:
    for i, c in enumerate(chords): BAR_CHORD.append(c); BAR_SEC.append(sec); BAR_IN_SEC.append(i)
assert len(BAR_CHORD) == BARS

# Lead melody: eight slots (eighth notes) per bar. '-' holds the note, '.' is a rest, 'X/Y' is two sixteenths.
VERSE = ['E5 - - B4 E5 - G5 -', 'G5 - - E5 - - C5 -', 'A5 - - F#5 - - D5 -', 'F#5 - - - - - D5 E5',
         'E5 - - B4 E5 - G5 -', 'G5 - - E5 - - G5 -', 'A5 - - F#5 - - A5 -', 'B5 - - - - - B5/A5 G5/F#5']
CHORUS = ['G5 - G5 A5 - G5 E5 -', 'F#5 - F#5 G5 - F#5 D5 -', 'B5 - - - A5 - G5 -', 'G5 - E5 - - - B4 D5',
          'G5 - G5 A5 - G5 E5 -', 'A5 - A5 B5 - A5 F#5 -', 'B5 - - - C6 - B5 -', 'D#6 - - - B5/A5 F#5/D#5 B4 -']

def parse(grid):
    """-> [(beat, beats, midi)] inside one bar."""
    notes = []
    for slot, tok in enumerate(grid.split()):
        if tok == '-': notes[-1][1] += 0.5
        elif tok == '.': continue
        elif '/' in tok:
            a, b = tok.split('/'); notes.append([slot * 0.5, 0.25, midi(a)]); notes.append([slot * 0.5 + 0.25, 0.25, midi(b)])
        else: notes.append([slot * 0.5, 0.5, midi(tok)])
    return notes
SCALE = [4, 6, 7, 9, 11, 0, 2]            # E natural minor, as pitch classes
def third_below(m, chord):
    pc = m % 12
    if pc == 3: pc = 2                    # D# counts as the seventh degree
    lower = SCALE[(SCALE.index(pc) - 2) % 7]
    if chord == 'B' and lower == 2: lower = 3
    if chord == 'B' and lower == 7: lower = 6        # G would grind against the chord's F#
    if chord == 'Em' and lower == 0: lower = 11
    return m - ((m % 12 - lower) % 12)

def lead_notes(first_bar, grids, harmony=False):
    out = []
    for i, g in enumerate(grids):
        for beat, beats, m in parse(g):
            if harmony: m = third_below(m, BAR_CHORD[first_bar + i])
            out.append((at(first_bar + i, beat), dur(beats), m))
    return out

def lead_voice(notes):
    """One mono lead line: glide between notes, vibrato that comes in on held notes. -> (raw, cutoff)"""
    notes = sorted(notes); pitch = np.full(L, float(notes[0][2])); gate = np.zeros(L); vib = np.zeros(L)
    for k, (st, n, m) in enumerate(notes):
        end = notes[k + 1][0] if k + 1 < len(notes) else L
        pitch[st:end] = m; gate[st:st + int(n * 0.94)] = 1
        if n > 0.3 * SR: vib[st:st + n] = np.clip((np.arange(n) - 0.16 * SR) / (0.2 * SR), 0, 1)
    k = np.exp(-np.arange(int(0.08 * SR)) / (0.014 * SR)); pitch = conv(pitch - pitch[0], k / k.sum()) + pitch[0]
    h = np.hanning(int(0.012 * SR)); amp = np.clip(conv(gate, h / h.sum()), 0, 1)
    t = np.arange(L) / SR
    f = hz(pitch + 0.28 * vib * np.sin(2 * np.pi * 5.8 * t))
    raw = (saw(f, L) * 0.6 + pulse(f * 1.004, L, 0.32) * 0.5 + saw(f * 0.5, L, 0.3) * 0.15) * amp
    return raw, np.clip(f * 3.2 + 900, 300, 9000) * (0.55 + 0.45 * amp)

def build_sources():
    """-> the dry instrument lines and their filter sweeps, before filtering."""
    bass = np.zeros(L); bass_cut = np.full(L, 300.0)
    arp = np.zeros(L); arp_cut = np.full(L, 800.0)
    pad = np.zeros(L); pad_cut = np.full(L, 900.0)
    step = dur(0.25)
    for bar in range(BARS):
        root, voicing, tones = CHORD[BAR_CHORD[bar]]; sec = BAR_SEC[bar]; k = BAR_IN_SEC[bar]
        # --- bass: driving sixteenths with octave pops; long notes in the break
        if sec == 'D':
            events = [(0, 8, 0), (8, 8, 0)] if k < 2 else [(s, 2, 12 if s in (6, 14) else 0) for s in range(0, 16, 2)]
        else:
            events = [(s, 1, 12 if s in (3, 6, 11, 14) else 0) for s in range(16)]
        for s, steps, up in events:
            n = int(step * steps * (0.86 if steps == 1 else 0.95)); f = hz(midi(root) + up); t = np.arange(n) / SR
            x = saw(f, n) * 0.65 + pulse(f, n, 0.4, 0.25) * 0.45 + np.sin(2 * np.pi * f * t) * 0.3
            st = at(bar) + s * step
            put(bass, st, x * env(n, 0.003, 0.012) * (1.0 if s % 4 == 0 else 0.85))
            bass_cut[st:st + n] = 260 + (2600 if up else 1900) * np.exp(-t / (0.07 if steps == 1 else 0.5))
        # --- arpeggio: sixteenth plucks up and down the chord (rests in the first two bars of the groove)
        if not (sec == 'A' and k < 2):
            order = [0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 3]
            for s in range(16):
                n = int(step * 0.9); t = np.arange(n) / SR; f = hz(midi(tones[order[s]]))
                st = at(bar) + s * step
                put(arp, st, pulse(f, n, 0.3) * np.exp(-t / 0.11) * env(n, 0.002, 0.01) * (1.0 if s % 2 == 0 else 0.7))
                arp_cut[st:st + n] = 700 + (5200 if sec.startswith('C') else 3600) * np.exp(-t / 0.05)
        # --- chords: brass stabs in the groove and chorus, a soft held pad in the verse, a rising sweep in the break
        if sec in ('A', 'C', 'C2'):
            slots = ([0, 3, 6], [0, 3, 6, 7]) if sec == 'A' else ([0, 3, 6, 7], [0, 3, 4, 6])
            hits = [(sl * 0.5, 0.42) for sl in slots[k % 2]]
        else:
            hits = [(0, 4.0)]
        for beat, beats in hits:
            n = dur(beats); rel = int(0.09 * SR); t = np.arange(n + rel) / SR; x = np.zeros(n + rel)
            for name in voicing + ([voicing[-1].replace('4', '5')] if sec != 'B' else []):
                for cents in (-11, -4, 0, 5, 12):
                    x += saw(hz(midi(name)) * 2 ** (cents / 1200.0), n + rel, rng.random())
            st = at(bar, beat)
            e = np.ones(n + rel); a = int((0.008 if beats < 1 else 0.06) * SR)
            e[:a] = np.linspace(0, 1, a); e[n:] = np.linspace(1, 0, rel)
            put(pad, st, x * e * 0.12)
            end = min(L, st + n + rel); t = t[:end - st]
            if sec == 'B': pad_cut[st:end] = 1100 + 250 * np.sin(2 * np.pi * 0.2 * (st / SR + t))
            elif sec == 'D': pad_cut[st:end] = 350 * (16.0 ** ((k + t / (4 * BEAT)) / 4.0))
            else: pad_cut[st:end] = 700 + 4300 * np.minimum(t / 0.035, 1) * np.exp(-np.maximum(t - 0.035, 0) / 0.22)
    # --- lead: verse, chorus, verse, chorus with a second voice a third below
    main = lead_notes(8, VERSE) + lead_notes(16, CHORUS) + lead_notes(24, VERSE) + lead_notes(32, CHORUS)
    lead, lead_cut = lead_voice(main)
    harm, harm_cut = lead_voice(lead_notes(32, CHORUS, harmony=True))
    return [(bass, bass_cut, 2.2), (arp, arp_cut, 1.6), (pad, pad_cut, 1.1), (lead, lead_cut, 1.3), (harm, harm_cut, 1.3)]

def build_drums():
    def kick():
        n = int(0.34 * SR); t = np.arange(n) / SR
        y = np.sin(2 * np.pi * np.cumsum(56 + 170 * np.exp(-t / 0.028)) / SR) * np.exp(-t / 0.11)
        c = int(0.004 * SR); y[:c] += rng.uniform(-1, 1, c) * 0.4
        return np.tanh(y * 2.2) * env(n, 0.0005, 0.03)
    def snare():
        n = int(0.32 * SR); t = np.arange(n) / SR
        nz = shape(rng.uniform(-1, 1, n), lambda f: hp(1000)(f) * lp(9500)(f)) * np.exp(-t / 0.10)
        body = np.sin(2 * np.pi * np.cumsum(185 + 95 * np.exp(-t / 0.02)) / SR) * np.exp(-t / 0.07)
        return np.tanh((nz * 1.3 + body * 0.8) * 1.6) * env(n, 0.0005, 0.03)
    def hat(decay, length):
        n = int(length * SR); t = np.arange(n) / SR
        return shape(rng.uniform(-1, 1, n), hp(7500, 3)) * np.exp(-t / decay) * env(n, 0.0005, 0.01)
    def tom(f0):
        n = int(0.5 * SR); t = np.arange(n) / SR
        y = np.sin(2 * np.pi * np.cumsum(f0 * (1 + 0.7 * np.exp(-t / 0.04))) / SR) * np.exp(-t / 0.17)
        return np.tanh((y + shape(rng.uniform(-1, 1, n), lp(3000)) * np.exp(-t / 0.03) * 0.5) * 1.5) * env(n, 0.0005, 0.05)
    def crash():
        n = int(2.2 * SR); t = np.arange(n) / SR
        return shape(rng.uniform(-1, 1, n), hp(4500)) * np.exp(-t / 0.55) * env(n, 0.001, 0.2)
    K, S, CH, OH, CR = kick(), snare(), hat(0.02, 0.12), hat(0.11, 0.4), crash()
    T = [tom(f) for f in (230, 172, 128, 96)]
    dry = np.zeros((2, L)); big = np.zeros((2, L)); kicks = []          # big = sent through the gated room
    step = dur(0.25)
    for bar in range(BARS):
        sec = BAR_SEC[bar]; k = BAR_IN_SEC[bar]; b0 = at(bar); fill = sec != 'D' and k == 7
        if k == 0: put(dry, b0, pan(CR, 0.4) * 0.33)
        if sec == 'D':
            kick_at = [0] if k < 2 else [0, 8]
            snare_at = {8: 1.0} if k < 3 else {0: 0.45, 2: 0.55, 4: 0.7, 6: 0.85}
            hats = {s: 0.5 for s in range(0, 16, 4)} if k < 3 else {}
            toms = {s: (s - 8) // 2 for s in range(8, 16)} if k == 3 else {}
        else:
            chorus_beat = sec.startswith('C')
            kick_at = ([0, 4, 8, 12] + ([14] if k % 2 else [])) if chorus_beat else ([0, 6, 8] + ([11] if k % 2 else []))
            snare_at = {4: 1.0, 12: 1.0}
            if fill: snare_at[10] = 0.8
            hats = {s: (1.0 if s % 4 == 0 else 0.6 if s % 2 == 0 else 0.35) for s in range(0, 16, 1 if chorus_beat else 2)}
            toms = {13: 0, 14: 1, 15: 2} if fill else {}
        for s in kick_at: put(dry, b0 + s * step, pan(K, 0.5) * 0.66); kicks.append(b0 + s * step)
        for s, v in snare_at.items(): put(dry, b0 + s * step, pan(S, 0.5) * 0.5 * v); put(big, b0 + s * step, pan(S, 0.5) * 0.5 * v)
        for s, v in hats.items():
            is_open = s == 14 and k % 2 == 1 and not toms
            put(dry, b0 + s * step, pan(OH if is_open else CH, 0.62) * 0.2 * v)
        for s, which in toms.items():
            x = pan(T[which], 0.25 + 0.17 * which) * 0.62; put(dry, b0 + s * step, x); put(big, b0 + s * step, x)
    # A hiss that swells through the last two bars of the break into the loop point.
    n = at(2); r = shape(rng.uniform(-1, 1, n), hp(2500)) * np.linspace(0, 1, n) ** 2.5 * 0.16
    put(dry, at(42), np.array([r, np.roll(r, 37)]))
    return dry, big, kicks

def main():
    sources = build_sources()
    with Pool(len(sources)) as pool: bass, arp, pad, lead, harm = pool.starmap(svf, sources)
    drums, big, kicks = build_drums()

    duck = np.ones(L); n = int(0.26 * SR); curve = 1 - 0.55 * np.exp(-np.arange(n) / (0.085 * SR))
    for st in kicks:
        m = min(n, L - st); duck[st:st + m] = np.minimum(duck[st:st + m], curve[:m])

    hall, gated = room(2.3, 0.42), room(0.27, 0.6, gate=True)
    bus = {}
    bus['drums'] = drums + wet(big, gated) * 0.55
    bus['bass'] = pan(np.tanh(bass * 1.4) * duck, 0.5) * 0.50
    a = pingpong(pan(arp, 0.5), 0.75, mix=0.38) * duck
    bus['arp'] = (a + wet(a, hall) * 0.18) * 0.26
    p = chorus(pad, 0.5, 0.004, 0.014, 0.6) * duck
    bus['pad'] = (p + wet(p, hall) * 0.22) * 0.52
    ld = chorus(lead, 0.8, 0.002, 0.009, 0.35) * np.array([[1.0], [0.92]]) + chorus(harm, 0.7, 0.002, 0.010, 0.35) * np.array([[0.55], [0.8]])
    ld = pingpong(ld, 1.0, fb=0.35, mix=0.2, reps=4)
    bus['lead'] = (ld + wet(ld, hall) * 0.3) * 0.36

    mix = sum(bus.values())
    mix[:, :L - N] += mix[:, N:]; mix = mix[:, :N]                       # fold the tails round: seamless loop
    mix = np.array([shape(ch, lambda f: hp(38)(f) * (1 + 0.6 * hp(6000, 1)(f))) for ch in mix])   # rumble out, a little air in
    mix = np.tanh(mix * 0.95 / np.percentile(np.abs(mix), 99.9))          # gentle saturation as the limiter
    mix = mix / np.abs(mix).max() * 0.89

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'audio', 'M_Fight.wav')
    if len(sys.argv) > 1: out = sys.argv[1]
    w = wave.open(out, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((mix.T * 32767).astype('<i2').tobytes()); w.close()

    # Numbers to judge the mix by without ears.
    def db(x): return 20 * np.log10(max(np.sqrt(np.mean(x * x)), 1e-9))
    print('%s  %.1f s' % (os.path.normpath(out), N / SR))
    print('level by part (dB): ' + '  '.join('%s %.1f' % (k, db(v[:, :N])) for k, v in bus.items()))
    mono = mix.mean(0); spec = np.abs(np.fft.rfft(mono)) ** 2; f = np.fft.rfftfreq(len(mono), 1.0 / SR); total = spec.sum()
    bands = [(0, 60), (60, 250), (250, 1000), (1000, 4000), (4000, 22050)]
    print('energy by band:     ' + '  '.join('%d-%d Hz %.0f%%' % (lo, hi, 100 * spec[(f >= lo) & (f < hi)].sum() / total) for lo, hi in bands))
    print('level by section:   ' + '  '.join('%s %.1f' % (name, db(mix[:, at(a):at(b)])) for name, a, b in
          [('A', 0, 8), ('B', 8, 16), ('C', 16, 24), ('B', 24, 32), ('C2', 32, 40), ('D', 40, 44)]))
    print('overall %.1f dB, peak %.2f, loudest-to-average %.1f dB' % (db(mix), np.abs(mix).max(), 20 * np.log10(np.abs(mix).max()) - db(mix)))
    jump = np.abs(mix[:, 0] - mix[:, -1]).max(); usual = np.percentile(np.abs(np.diff(mix, axis=1)), 99)
    print('loop seam: step of %.4f across the join (99%% of ordinary steps are under %.4f)' % (jump, usual))

if __name__ == '__main__':
    main()
