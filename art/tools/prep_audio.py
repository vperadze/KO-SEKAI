#!/usr/bin/env python3
"""Turns picked files from art/sound-library into short 16-bit game sounds in art/audio (S_<Name>_<n>.wav).
Trims silence, cuts long tails with a fade, and brings every sound to the same peak level."""
import wave, os, sys
import numpy as np
SRC = os.path.join(os.path.dirname(__file__), '..', 'sound-library', 'sound')
DST = os.path.join(os.path.dirname(__file__), '..', 'audio')
# game sound -> (source files, longest length in seconds)
PICK = {
 'Hit':      (['Foley/foley-punch-mid-tight-01', 'Foley/foley-combat-punch-01', 'Foley/cartoon-fx-punch-oldschool-01'], 0.7),
 'Counter':  (['Foley/foley-punch-low-smack-hit-01'], 1.1),
 'Blocked':  (['Fantasy & Survival/cartoon-fight-hit-punch-pillow-glove-01', 'Fantasy & Survival/cartoon-fight-hit-punch-pillow-glove-02'], 0.5),
 'Hurt':     (['Foley/foley-whoosh-fleshy-hit-01', 'Foley/foley-whoosh-fleshy-hit-02'], 0.9),
 'Whoosh':   (['Foley/foley-whoosh-short-01', 'Foley/foley-whoosh-heavy-fast-01', 'Foley/foley-whoosh-heavy-fast-02'], 0.7),
 'Dodge':    (['Foley/foley-whoosh-cloth-soft-dry-01', 'Foley/foley-whoosh-cloth-soft-dry-02'], 0.7),
 'KO':       (['Cinematics/cartoon-impact-massive-hit-01'], 3.0),
 'Fall':     (['Foley/foley-cloth-bodydrop-01'], 1.1),
 'Coin':     (['UI/ui-arcade-beep-positive-01'], 0.6),
 'Step':     (['Foley/foley-step-boots-concrete-single-left-01', 'Foley/foley-step-boots-concrete-single-right-01', 'Foley/foley-step-boots-concrete-single-left-02', 'Foley/foley-step-boots-concrete-single-right-02'], 0.6),
 'Footstep': (['Foley/foley-step-walk-textile-light-01', 'Foley/foley-step-walk-textile-light-02', 'Foley/foley-step-walk-textile-light-03'], 0.5),
 'Win':      (['UI/ui-arcade-level-up-8bit-01'], 1.1),
 'Lose':     (['UI/ui-arcade-loose-01'], 2.4),
}
def read(path):
    w = wave.open(path); n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = np.frombuffer(w.readframes(n), dtype=np.uint8)
    if sw == 3:
        b = raw.reshape(-1, 3).astype(np.int32); x = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)); x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    elif sw == 2: x = np.frombuffer(raw.tobytes(), dtype='<i2') / 32768.0
    else: sys.exit('unsupported sample width %d in %s' % (sw, path))
    return x.reshape(-1, ch), sr
for name, (files, longest) in PICK.items():
    for k, f in enumerate(files):
        x, sr = read(os.path.join(SRC, f + '.wav'))
        loud = np.abs(x).max(axis=1); peak = loud.max()
        on = np.where(loud > 0.02 * peak)[0]
        x = x[max(0, on[0] - int(0.003 * sr)): on[-1] + int(0.05 * sr)]
        x = x[: int(longest * sr)]
        fade = min(len(x), int((0.25 if longest > 1.5 else 0.04) * sr))
        x[-fade:] *= np.linspace(1, 0, fade)[:, None]
        x = x / np.abs(x).max() * 0.89
        out = os.path.join(DST, 'S_%s_%d.wav' % (name, k + 1))
        w = wave.open(out, 'wb'); w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((x * 32767).astype('<i2').tobytes()); w.close()
        print('%-22s %.2f s  from %s' % (os.path.basename(out), len(x) / sr, os.path.basename(f)))
