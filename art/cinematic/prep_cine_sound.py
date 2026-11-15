"""The opening film's own sound: what Dreamina put under each shot, cut to the shot's length and joined in the film's order.

    ~/ielts-listening/.venv/bin/python art/cinematic/prep_cine_sound.py      (PyAV and numpy, as art/tools/prep_crowd.py)
    then art/tools/import_voice_faces.py headless (it brings in art/audio/M_*.wav); no rebuild.

Writes art/audio/M_Cine.wav. The game plays it under the film beside the music (KSGameMode.cpp, SetPhase), so each
shot is brought to a level of its own choosing here, not left as it came: the takes differ by 9 dB. Two shots carry
a low pulse that reads as a beat and would fight the game's music, so their bottom is cut away and they are kept low;
one came with music outright and is left out.
"""
import os, re, wave
import av
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, '..', '..', 'unreal', 'Source', 'KoSekai', 'KSCineData.h')
OUT = os.path.join(HERE, '..', 'audio', 'M_Cine.wav')
RATE, FPS = 44100, 12.0
# clip: (level in dB the shot is brought to, everything under this many Hz is cut away)
# No level: the shot is left silent. The seven walking came with music of its own, which played against the game's (player, 2026-11-15).
SHOTS = [('city', -18, 50), ('fighters', None, 50), ('cancelled', -18, 50), ('alley', -25, 500), ('fists', -19, 150), ('pull', -17, 50)]


def load(path):
    with av.open(path) as clip:
        fix, out = av.AudioResampler(format='s16', layout='stereo', rate=RATE), []
        for frame in clip.decode(audio=0):
            for piece in fix.resample(frame): out.append(piece.to_ndarray().reshape(-1, 2))
    return np.concatenate(out).astype(np.float64) / 32768.0


def highpass(a, hz):
    spec = np.fft.rfft(a, axis=0); f = np.fft.rfftfreq(len(a), 1.0 / RATE)
    spec *= np.clip((f - hz * 0.7) / (hz * 0.3), 0.0, 1.0)[:, None]          # a slope, not a wall: no ringing
    return np.fft.irfft(spec, n=len(a), axis=0)


frames = [int(n) for n in re.findall(r'TEXT\("T_KS_Cine_\d+"\), (\d+),', open(HEADER).read())]
assert len(frames) == len(SHOTS), 'KSCineData.h and SHOTS disagree'
parts = []
for (name, level, cut), count in zip(SHOTS, frames):
    want = int(round(count / FPS * RATE))
    a = highpass(load(os.path.join(HERE, 'raw', name + '.mp4')), cut)[:want]
    if len(a) < want: a = np.concatenate([a, np.zeros((want - len(a), 2))])
    if level is None: parts.append(np.zeros((want, 2))); print('%-10s %.2f s  silent' % (name, want / RATE)); continue
    a *= 10 ** (level / 20.0) / (np.sqrt(np.mean(a ** 2)) + 1e-9)
    edge = int(0.02 * RATE); ramp = np.linspace(0.0, 1.0, edge)[:, None]     # no click at a cut
    a[:edge] *= ramp; a[-edge:] *= ramp[::-1]
    parts.append(a); print('%-10s %.2f s  level %d dB  peak %.1f dB' % (name, want / RATE, level, 20 * np.log10(np.abs(a).max())))
film = np.concatenate(parts)
n = int(0.3 * RATE); film[:n] *= np.linspace(0.0, 1.0, n)[:, None]
n = int(0.5 * RATE); film[-n:] *= np.linspace(1.0, 0.0, n)[:, None]
film = np.tanh(film * 1.1) / 1.1 if np.abs(film).max() > 0.95 else film     # the loudest bangs are rounded off, not clipped
with wave.open(OUT, 'wb') as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
    w.writeframes((np.clip(film, -1.0, 1.0) * 32767).astype('<i2').tobytes())
print('wrote %s  %.2f s' % (os.path.relpath(OUT), len(film) / RATE))
