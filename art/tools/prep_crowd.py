#!/usr/bin/env python3
"""Cuts the crowd's game sounds out of the Dreamina clips in art/arena/sound (made by art/arena/gen_crowd_sound.py).

    ~/ielts-listening/.venv/bin/python art/tools/prep_crowd.py        # needs PyAV to read the mp4 soundtracks

Writes art/audio/S_<Name>_<n>.wav and the looping murmur art/audio/M_Crowd.wav. Import with art/tools/import_crowd.py.
The cut times were read off each clip's loudness curve; a regenerated clip needs new ones.
"""
import os, wave
import av
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SRC, DST, RATE = os.path.join(ROOT, 'arena', 'sound'), os.path.join(ROOT, 'audio'), 44100
# game sound -> [(clip, from second, to second)]
CUTS = {
    'Cheer': [('cheers', 0.10, 2.30), ('cheers', 3.40, 5.10), ('cheers', 6.40, 8.40)],
    'Roar':  [('cheers', 6.40, 10.05)],
    'Gasp':  [('gasps', 0.30, 1.60), ('gasps', 4.00, 5.05)],
    'Boo':   [('gasps', 7.00, 10.05)],
    'Shout': [('shouts', 0.00, 0.55), ('shouts', 1.00, 2.15), ('shouts', 2.20, 3.20), ('shouts', 4.00, 5.30), ('shouts', 6.30, 7.70), ('shouts', 8.30, 9.25)],
    'Beast': [('beasts', 0.25, 1.45), ('beasts', 2.50, 3.25), ('beasts', 4.20, 5.09)],
}


def read(clip):
    container = av.open(os.path.join(SRC, clip + '.mp4'))
    resampler = av.AudioResampler(format='flt', layout='stereo', rate=RATE)
    frames = [f.to_ndarray().reshape(-1, 2) for packet in container.decode(container.streams.audio[0]) for f in resampler.resample(packet)]
    return np.concatenate(frames)


def save(name, x):
    x = np.clip(x, -1, 1)
    with wave.open(os.path.join(DST, name + '.wav'), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes((x * 32767).astype('<i2').tobytes())
    print('%-14s %.2f s' % (name, len(x) / RATE))


clips = {}
for name, cuts in CUTS.items():
    for k, (clip, start, end) in enumerate(cuts, 1):
        if clip not in clips: clips[clip] = read(clip)
        x = clips[clip][int(start * RATE): int(end * RATE)].copy()
        fade_in, fade_out = int(0.015 * RATE), int(min(0.35, 0.3 * len(x) / RATE) * RATE)
        x[:fade_in] *= np.linspace(0, 1, fade_in)[:, None]
        x[-fade_out:] *= np.linspace(1, 0, fade_out)[:, None]
        save('S_%s_%d' % (name, k), x / np.abs(x).max() * 0.85)

# The murmur: level evened out over its length, then its tail cross-faded into its head so that it loops without a seam.
x = read('murmur')
window = RATE // 2
power = np.concatenate([[0.0], np.cumsum((x ** 2).mean(axis=1))])
at = np.arange(len(x)); lo, hi = np.maximum(at - window // 2, 0), np.minimum(at + window // 2, len(x))
level = np.sqrt((power[hi] - power[lo]) / (hi - lo))
x = x * (np.median(level) / np.maximum(level, 1e-4))[:, None]
overlap = int(1.2 * RATE)
ramp = np.linspace(0, 1, overlap)[:, None]
loop = np.concatenate([x[-overlap:] * np.sqrt(1 - ramp) + x[:overlap] * np.sqrt(ramp), x[overlap:len(x) - overlap]])
save('M_Crowd', loop / np.abs(loop).max() * 0.7)
