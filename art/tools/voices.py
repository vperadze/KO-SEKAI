#!/usr/bin/env python3
"""Records every fighter's spoken lines with the local Kokoro voices and writes the table the game reads.

    ~/ielts-listening/.venv/bin/python art/tools/voices.py            all lines
    ~/ielts-listening/.venv/bin/python art/tools/voices.py Turbotron  one fighter

Writes art/audio/voice/V_<Fighter>_<line>.wav (24 kHz, mono, 16 bit) and
unreal/Source/KoSekai/KSVoiceData.h: each line's text and how far the mouth is open, 20 times a second, so the
face can talk in time with the sound. Import the sounds afterwards with art/tools/import_voice_faces.py.

Each fighter is one Kokoro voice, left natural ('plain'); effect() keeps the old effects for anyone who wants one back. The pitch shift is a plain
resample: Kokoro speaks slower or faster by the same factor first, so the pace stays what CAST asks for.
"""
import os, sys, wave, zlib
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
MODELS = os.path.expanduser('~/ielts-listening/tools/audio/models')
OUT = os.path.join(ROOT, 'art', 'audio', 'voice')
HEADER = os.path.join(ROOT, 'unreal', 'Source', 'KoSekai', 'KSVoiceData.h')
RATE = 24000
MOUTH_HZ = 20

# fighter (content folder) -> voice, pitch (1 = the voice's own), pace, effect
# The player found the first cast "too robotic": heavy effects (ring modulation, bit crushing, clipping) and big
# pitch shifts made Kokoro sound like a machine. Now the voices are left natural: no effect, pitch within 6%.
CAST = {
    'SergeantMulletV2':    ('am_fenrir',  1.00, 1.10, 'plain'),
    'ChannelZero':         ('am_puck',    1.00, 1.08, 'plain'),
    'SirLasercorn':        ('bm_fable',   1.00, 0.96, 'plain'),
    'AerobicsAnnihilator': ('af_bella',   1.00, 1.15, 'plain'),
    'GrandmaThunder':      ('bf_emma',    0.95, 0.90, 'plain'),
    'Turbotron':           ('am_michael', 1.00, 0.98, 'plain'),
    'CountHairspray':      ('bm_george',  0.94, 0.90, 'plain'),
}

# Line keys: intro, hit1-3 (they landed a punch), hurt1-2 (they took one), win, lose.
# Short and silly: a line has to fit between two punches. Six words is long.
LINES = {
    'SergeantMulletV2': {
        'intro': "Legal cut me. Fuck legal. And fuck you!",
        'hit1': "Sit down, you piece of shit!",
        'hit2': "Mullet power, bitch!",
        'hit3': "You're trash, maggot!",
        'hurt1': "Fuck! Not the moustache!",
        'hurt2': "Lucky shot, asshole!",
        'win': "Dismissed, you little shit.",
        'lose': "Fuck. Tell my barber I loved him.",
    },
    'ChannelZero': {
        'intro': "Tonight on Channel Zero: you die!",
        'hit1': "Breaking news: you're trash!",
        'hit2': "And now the weather: fuck you!",
        'hit3': "Stay tuned, shithead!",
        'hurt1': "Ow! Fuck! Not the screen!",
        'hurt2': "Shit! Technical difficulties!",
        'win': "Good night, you piece of shit.",
        'lose': "No signal. Fuck.",
    },
    'SirLasercorn': {
        'intro': "Kneel, peasant. Then fuck off.",
        'hit1': "Taste the rainbow, bitch!",
        'hit2': "Horn to the face, you trash!",
        'hit3': "You will die majestically!",
        'hurt1': "Not the horn, you fuck!",
        'hurt2': "Rude! So fucking rude!",
        'win': "Majestic as fuck. As always.",
        'lose': "Shit. Tell no one.",
    },
    'AerobicsAnnihilator': {
        'intro': "Warm-up is over. Time to die, sweat bags!",
        'hit1': "And one! And two! And fuck you!",
        'hit2': "Feel the burn, bitch!",
        'hit3': "Kiss my leg warmers, trash!",
        'hurt1': "Fuck no! Not the hair!",
        'hurt2': "Ow! You piece of shit!",
        'win': "And rest. In peace, loser.",
        'lose': "Shit. I pulled a hamstring.",
    },
    'GrandmaThunder': {
        'intro': "Grandma's gonna fuck you up, dearie.",
        'hit1': "That's for not calling, you little shit!",
        'hit2': "Eat your vegetables, asshole!",
        'hit3': "Want a cookie? Fuck you!",
        'hurt1': "Oh! My fucking hip!",
        'hurt2': "You piece of trash!",
        'win': "Now go to your room, you little shit.",
        'lose': "Fuck it. Grandma needs a nap.",
    },
    'Turbotron': {
        'intro': "Batteries not included. Fuck you included.",
        'hit1': "Beep boop, bitch.",
        'hit2': "You are trash. I did the math.",
        'hit3': "Prepare to die. Please.",
        'hurt1': "Error! Fuck!",
        'hurt2': "Shit. There goes my warranty.",
        'win': "Human deleted. Fuck yeah.",
        'lose': "Low battery. Oh, shit.",
    },
    'CountHairspray': {
        'intro': "Good evening. Fuck you.",
        'hit1': "Bleh! You piece of shit!",
        'hit2': "You punch like a bat, trash!",
        'hit3': "Fang you very fucking much!",
        'hurt1': "Not the hair, asshole!",
        'hurt2': "Fuck! My hairspray!",
        'win': "I am fabulous. You are trash.",
        'lose': "Shit. I just did my hair.",
    },
}
# What the screen shows where the spelling was bent to get the accent out of the voice.
SHOWN = {}


def resample(x, factor):
    """Plays x faster by factor (pitch up by the same), with straight-line interpolation."""
    n = int(len(x) / factor)
    return np.interp(np.arange(n) * factor, np.arange(len(x)), x)


def lowpass(x, hz):
    a = float(np.exp(-2 * np.pi * hz / RATE)); y = np.empty_like(x); s = 0.0
    for i, v in enumerate(x.tolist()):
        s = (1 - a) * v + a * s; y[i] = s
    return y


def echo(x, taps, tail=0.0):
    y = np.concatenate([x, np.zeros(int(tail * RATE))])
    out = y.copy()
    for sec, gain in taps:
        d = int(sec * RATE); out[d:] += y[:len(y) - d] * gain
    return out


def effect(x, kind, rng):
    t = np.arange(len(x)) / RATE
    if kind == 'bark':        # a shouting drill sergeant: pushed into soft clipping
        x = np.tanh(x / np.abs(x).max() * 3.2)
    elif kind == 'tv':        # a small television speaker: narrow band, coarse steps, static underneath
        x = x - lowpass(x, 450); x = lowpass(x, 3200); x = x / np.abs(x).max()
        x = np.repeat(x[::3], 3)[:len(x)]
        t = np.arange(len(x)) / RATE
        x = np.round(x * 24) / 24 + rng.normal(0, 0.012, len(x)) + 0.01 * np.sin(2 * np.pi * 50 * t)
    elif kind == 'shimmer':   # too majestic: a short bright hall
        x = echo(x, [(0.031, 0.35), (0.11, 0.22), (0.19, 0.12)], 0.3)
        x = x + 0.25 * (x - lowpass(x, 2500))
    elif kind == 'bright':    # an exercise tape: thin, loud and close
        x = x + 0.6 * (x - lowpass(x, 1800)); x = np.tanh(x / np.abs(x).max() * 1.8)
    elif kind == 'wobble':    # an old voice: the pitch and the loudness tremble
        shift = 0.0016 * RATE * np.sin(2 * np.pi * 5.5 * t)
        x = np.interp(np.arange(len(x)) + shift, np.arange(len(x)), x) * (1 + 0.18 * np.sin(2 * np.pi * 6.3 * t))
    elif kind == 'robot':     # a machine: the voice multiplied by a buzz, and a metal pipe
        x = x * (0.45 + 0.55 * np.sign(np.sin(2 * np.pi * 62 * t)))
        x = echo(x, [(0.004, 0.55), (0.008, 0.3)], 0.05); x = x / np.abs(x).max(); x = np.round(x * 40) / 40
    elif kind == 'crypt':     # a vampire in a stone hall
        x = echo(x, [(0.067, 0.42), (0.131, 0.3), (0.211, 0.2), (0.337, 0.12), (0.489, 0.07)], 0.6)
    return x


def trim(x, floor=0.01, keep=0.03):
    on = np.where(np.abs(x) > floor * np.abs(x).max())[0]
    return x[max(0, on[0] - int(keep * RATE)): on[-1] + int(keep * RATE)]


def mouth(x):
    """How far the mouth is open, 0-9, MOUTH_HZ times a second: loudness against the loudest moment of the line."""
    step = RATE // MOUTH_HZ
    rms = np.array([np.sqrt(np.mean(x[i:i + step] ** 2)) for i in range(0, len(x), step)])
    return ''.join(str(int(v)) for v in np.clip(rms / (rms.max() * 0.55) * 9, 0, 9))


def main():
    from kokoro_onnx import Kokoro
    tts = Kokoro(os.path.join(MODELS, 'kokoro-v1.0.onnx'), os.path.join(MODELS, 'voices-v1.0.bin'))
    only = set(sys.argv[1:])
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for fighter, (voice, pitch, pace, kind) in CAST.items():
        for key, text in LINES[fighter].items():
            path = os.path.join(OUT, 'V_%s_%s.wav' % (fighter, key))
            if fighter in only or not os.path.exists(path) or (not only and '--keep' not in sys.argv):
                rng = np.random.default_rng(zlib.crc32((fighter + key).encode()))
                x, rate = tts.create(text, voice=voice, speed=pace / pitch, lang='en-gb' if voice[0] == 'b' else 'en-us')
                assert rate == RATE
                dry = trim(resample(np.asarray(x, dtype=np.float64), pitch))
                x = effect(dry, kind, rng)
                x = x / np.abs(x).max() * 0.9
                fade = int(0.02 * RATE); x[-fade:] *= np.linspace(1, 0, fade)
                w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
                w.writeframes((x * 32767).astype('<i2').tobytes()); w.close()
                # The mouth follows the voice before the effect: an echo's tail is not the mouth moving.
                np.save(path[:-4] + '.npy', np.concatenate([dry, np.zeros(max(0, len(x) - len(dry)))]))
                print('%-40s %.2f s' % (os.path.basename(path), len(x) / RATE))
            dry = np.load(path[:-4] + '.npy')
            shown = text
            for said, written in SHOWN.items(): shown = shown.replace(said, written)
            rows.append((fighter, key, shown, len(dry) / RATE, mouth(dry)))
    with open(HEADER, 'w') as f:
        f.write('// Generated by art/tools/voices.py - do not edit. Every spoken line: who says it, when, the words for the\n')
        f.write('// subtitle, its length, and how far the mouth is open (0-9) %d times a second.\n#pragma once\n\n' % MOUTH_HZ)
        f.write('struct FKSVoiceLine { const TCHAR* Fighter; const TCHAR* Key; const TCHAR* Text; float Seconds; const char* Mouth; };\n\n')
        f.write('static const float KSVoiceMouthHz = %d.f;\n\nstatic const FKSVoiceLine KSVoiceLines[] = {\n' % MOUTH_HZ)
        for fighter, key, text, sec, m in rows:
            f.write('\t{ TEXT("%s"), TEXT("%s"), TEXT("%s"), %.2ff, "%s" },\n' % (fighter, key, text.replace('"', '\\"'), sec, m))
        f.write('};\n')
    print('%d lines -> %s' % (len(rows), os.path.relpath(HEADER, ROOT)))


if __name__ == '__main__':
    main()
