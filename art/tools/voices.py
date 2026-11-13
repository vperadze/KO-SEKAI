#!/usr/bin/env python3
"""Records every fighter's spoken lines, acted, and writes the table the game reads.

    art/voice-model/venv/bin/python art/tools/voices.py            all lines (about an hour, no graphics card)
    art/voice-model/venv/bin/python art/tools/voices.py Turbotron  one fighter
    ... --keep                                                     only the lines that have no sound yet

Two local models. Kokoro gives each fighter a voice (CAST): it reads one sample sentence per fighter. Chatterbox
copies that voice and acts the lines, with an emotion setting per kind of line (ACT) and per fighter (HEAT).
Kokoro alone read the lines flat ("speech to text, no emotions", said the player), whatever the punctuation.
Every line is recorded TAKES times; a speech recogniser throws out takes that say the wrong words. All takes stay
in art/audio/voice/takes: to use another one, copy it over V_<Fighter>_<line>.wav and run with --keep.

Writes art/audio/voice/V_<Fighter>_<line>.wav (24 kHz, mono, 16 bit, compressed loud) and
unreal/Source/KoSekai/KSVoiceData.h: each line's text and how far the mouth is open, 20 times a second, so the
face can talk in time with the sound. Import the sounds afterwards with art/tools/import_voice_faces.py -KSReimport.

The environment: python3 -m venv art/voice-model/venv; pip install torch==2.6.0 torchaudio==2.6.0 from
https://download.pytorch.org/whl/cpu, then chatterbox-tts kokoro-onnx faster-whisper.
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
        'hurt1': "Error! Error! Oh, fuck!",      # two words alone came out as mumbling in every take
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


# How hard each kind of line is acted: (emotion, how closely the pacing follows the reference voice).
# Emotion 0.5 is Chatterbox's neutral; above 1 it shouts and stretches words. A looser pacing lets it act more.
ACT = {'intro': (0.85, 0.35), 'hit': (1.05, 0.3), 'hurt': (1.25, 0.3), 'win': (0.9, 0.35), 'lose': (0.7, 0.4)}
# Per fighter, a factor on the emotion: the robot stays drier than the drill sergeant.
HEAT = {'SergeantMulletV2': 1.1, 'ChannelZero': 1.0, 'SirLasercorn': 0.95, 'AerobicsAnnihilator': 1.1,
        'GrandmaThunder': 0.95, 'Turbotron': 0.75, 'CountHairspray': 0.9}
# What each fighter's voice is copied from: Kokoro reads this, Chatterbox takes the timbre and acts the lines itself.
SAMPLE = "Listen to me, and listen well! I have waited years for this fight, and nobody, nobody is going to stop me now!"
TAKES = 3


def write(path, x):
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype('<i2').tobytes()); w.close()


def loud(x):
    """Brings a line up to shouting level: the quiet parts are lifted, the peaks pressed against a soft ceiling."""
    x = x / np.abs(x).max()
    step = RATE // 100
    level = np.array([np.sqrt(np.mean(x[i:i + step] ** 2)) for i in range(0, len(x), step)])
    level = np.interp(np.arange(len(x)), np.arange(len(level)) * step + step / 2, np.convolve(level, np.ones(9) / 9, 'same'))
    x = x * (np.maximum(level, 0.02) / level.max()) ** -0.45            # 0.45: roughly a 2:1 compressor
    x = np.tanh(x / np.percentile(np.abs(x), 99.5) * 1.3)
    return x / np.abs(x).max() * 0.95


def words(text): return ''.join(c.lower() if c.isalnum() or c == "'" else ' ' for c in text).split()


def main():
    import difflib, torch
    from kokoro_onnx import Kokoro
    from chatterbox.tts import ChatterboxTTS
    from faster_whisper import WhisperModel
    only = set(a for a in sys.argv[1:] if not a.startswith('--'))
    os.makedirs(os.path.join(OUT, 'takes'), exist_ok=True)
    kokoro = Kokoro(os.path.join(MODELS, 'kokoro-v1.0.onnx'), os.path.join(MODELS, 'voices-v1.0.bin'))
    actor = ears = None
    rows = []
    for fighter, (voice, pitch, pace, kind) in CAST.items():
        ref = os.path.join(OUT, 'takes', 'ref_%s.wav' % fighter)
        if not os.path.exists(ref):
            x, rate = kokoro.create(SAMPLE, voice=voice, speed=pace / pitch, lang='en-gb' if voice[0] == 'b' else 'en-us')
            x = trim(resample(np.asarray(x, dtype=np.float64), pitch)); write(ref, x / np.abs(x).max() * 0.9)
        for key, text in LINES[fighter].items():
            path = os.path.join(OUT, 'V_%s_%s.wav' % (fighter, key))
            if fighter in only or not os.path.exists(path) or (not only and '--keep' not in sys.argv):
                if actor is None:
                    actor = ChatterboxTTS.from_pretrained(device='cpu')
                    ears = WhisperModel('small.en', device='cpu', compute_type='int8')
                emotion, pacing = ACT[key.rstrip('0123456789')]
                emotion *= HEAT[fighter]
                best = None
                for take in range(TAKES):
                    torch.manual_seed(zlib.crc32((fighter + key).encode()) % 100000 + take)
                    x = actor.generate(text, audio_prompt_path=ref, exaggeration=emotion, cfg_weight=pacing, temperature=0.85)
                    x = trim(x.squeeze().numpy().astype(np.float64))
                    take_path = os.path.join(OUT, 'takes', 'V_%s_%s.take%d.wav' % (fighter, key, take + 1))
                    write(take_path, loud(x))
                    # Nobody listens to every take, so each one is checked by a speech recogniser: a take that says
                    # other words than the line (the model sometimes babbles on short lines) loses. Among the takes
                    # that say the line, the one the recogniser wrote down with the most '!' and CAPITALS wins (its own sign of
                    # shouting), then the shortest: a clean take has no drawn-out tail.
                    at16k = np.interp(np.arange(int(len(x) * 16000 / RATE)) * RATE / 16000.0, np.arange(len(x)), x / np.abs(x).max()).astype(np.float32)
                    heard = ' '.join(seg.text for seg in ears.transcribe(at16k, language='en')[0])
                    match = difflib.SequenceMatcher(None, words(text), words(heard)).ratio()
                    sec = len(x) / RATE
                    print('  take %d  %.2f s  match %.2f  "%s"' % (take + 1, sec, match, heard.strip()))
                    shout = heard.count('!') + sum(1 for w in heard.split() if len(w) > 2 and w.isupper())
                    score = (round(match, 1), shout, -sec)
                    if best is None or score > best[0]: best = (score, x, take + 1, match)
                _, x, chosen, match = best
                np.save(path[:-4] + '.npy', x)        # the mouth follows the voice as spoken, before it is pressed loud
                x = loud(x)
                fade = int(0.02 * RATE); x[-fade:] *= np.linspace(1, 0, fade)
                write(path, x)
                print('%-40s %.2f s  take %d%s' % (os.path.basename(path), len(x) / RATE, chosen, '' if match >= 0.7 else '   <-- WORDS DO NOT MATCH, listen to it'))
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
