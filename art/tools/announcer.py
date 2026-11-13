#!/usr/bin/env python3
"""Records the ring announcer: a big old-time boxing voice over a hall loudspeaker.

    art/voice-model/venv/bin/python art/tools/announcer.py            every line (about 20 minutes, no graphics card)
    art/voice-model/venv/bin/python art/tools/announcer.py ko down    only these lines
    ... --keep                                                        only the lines that have no sound yet

Same two local models as voices.py (Kokoro gives the voice, Chatterbox acts the lines, a speech recogniser throws
out takes that say other words). Writes art/audio/voice/V_Announcer_<key>.wav and
unreal/Source/KoSekai/KSAnnouncerData.h (rebuild the game after it); import the sounds with
art/tools/import_voice_faces.py -KSReimport. All takes stay in art/audio/voice/takes.
"""
import difflib, os, sys, wave, zlib
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from voices import MODELS, OUT, RATE, ROOT, echo, loud, lowpass, resample, trim, words, write

HEADER = os.path.join(ROOT, 'unreal', 'Source', 'KoSekai', 'KSAnnouncerData.h')
VOICE, PITCH, PACE = 'am_onyx', 0.93, 0.88          # the deepest American voice, a little lower and slower still
EMOTION, PACING = 1.2, 0.3                          # see ACT in voices.py: above 1 it shouts and stretches words
TAKES = 4
SAMPLE = ("Ladies and gentlemen! Welcome to the main event of the evening! In the red corner, weighing two hundred "
          "pounds, the undefeated heavyweight champion of the world!")

FIGHTERS = {'SergeantMulletV2': 'Sergeant Mullet', 'ChannelZero': 'Channel Zero', 'SirLasercorn': 'Sir Lasercorn',
            'AerobicsAnnihilator': 'the Aerobics Annihilator', 'GrandmaThunder': 'Grandma Thunder', 'Turbotron': 'Turbotron',
            'CountHairspray': 'Count Hairspray'}
# Chatterbox mumbles on lines of two or three words, so every line has a few more.
LINES = {
    'welcome': "Ladies and gentlemen! Let's get ready to fight!",
    'choose': "Challenger! Choose your fighter!",
    'round1': "Round number one! Fight!",      # "Round one! Fight!" came out as "Raggle one" and "Ratman two"
    'round2': "Round number two! Fight!",
    'round3': "The final round! Fight!",
    'ko': "Knockout! It's all over!",
    'down': "Down goes the challenger!",
    'time': "Time! There's the final bell!",
    'champion': "The new champion of the world!",
    'gameover': "Game over, ladies and gentlemen!",
}
for folder, name in FIGHTERS.items(): LINES['name_' + folder] = "In this corner: %s!" % name


def hall(x):
    """A loudspeaker in a big hall: no deep bass, a hard middle, and the walls answering."""
    x = x - lowpass(x, 170)
    x = x + 0.5 * (lowpass(x, 3400) - lowpass(x, 900))
    x = np.tanh(x / np.abs(x).max() * 1.6)
    x = echo(x, [(0.083, 0.30), (0.171, 0.19), (0.293, 0.12), (0.447, 0.07), (0.631, 0.04)], 0.75)
    fade = int(0.15 * RATE); x[-fade:] *= np.linspace(1, 0, fade)
    return x / np.abs(x).max() * 0.95


def main():
    import torch
    from kokoro_onnx import Kokoro
    from chatterbox.tts import ChatterboxTTS
    from faster_whisper import WhisperModel
    torch.set_num_threads(8)
    only = set(a for a in sys.argv[1:] if not a.startswith('--'))
    os.makedirs(os.path.join(OUT, 'takes'), exist_ok=True)
    ref = os.path.join(OUT, 'takes', 'ref_Announcer.wav')
    if not os.path.exists(ref):
        kokoro = Kokoro(os.path.join(MODELS, 'kokoro-v1.0.onnx'), os.path.join(MODELS, 'voices-v1.0.bin'))
        voice = VOICE if VOICE in kokoro.get_voices() else 'am_michael'
        x, _ = kokoro.create(SAMPLE, voice=voice, speed=PACE / PITCH, lang='en-us')
        x = trim(resample(np.asarray(x, dtype=np.float64), PITCH)); write(ref, x / np.abs(x).max() * 0.9)
        print('reference voice: %s' % voice)
    actor = ears = None
    rows = []
    for key, text in LINES.items():
        path = os.path.join(OUT, 'V_Announcer_%s.wav' % key)
        if key in only or not os.path.exists(path) or (not only and '--keep' not in sys.argv):
            if actor is None:
                actor = ChatterboxTTS.from_pretrained(device='cpu')
                ears = WhisperModel('small.en', device='cpu', compute_type='int8')
            best = None
            for take in range(TAKES):
                torch.manual_seed(zlib.crc32(('Announcer' + key).encode()) % 100000 + take)
                x = actor.generate(text, audio_prompt_path=ref, exaggeration=EMOTION, cfg_weight=PACING, temperature=0.85)
                x = trim(x.squeeze().numpy().astype(np.float64))
                write(os.path.join(OUT, 'takes', 'V_Announcer_%s.take%d.wav' % (key, take + 1)), loud(x))
                at16k = np.interp(np.arange(int(len(x) * 16000 / RATE)) * RATE / 16000.0, np.arange(len(x)), x / np.abs(x).max()).astype(np.float32)
                heard = ' '.join(seg.text for seg in ears.transcribe(at16k, language='en')[0])
                # Letters, not words: the recogniser writes "Laser Corn" and "Turbo Tron".
                heard = heard.replace('1', ' one ').replace('2', ' two ')
                match = difflib.SequenceMatcher(None, ''.join(words(text)), ''.join(words(heard))).ratio()
                sec = len(x) / RATE
                print('  take %d  %.2f s  match %.2f  "%s"' % (take + 1, sec, match, heard.strip()), flush=True)
                shout = heard.count('!') + sum(1 for w in heard.split() if len(w) > 2 and w.isupper())
                # An announcer draws his words out, so among good takes the longer one wins (up to a point).
                # A take with every letter right beats one that is nearly right ("Gave over" scored 0.96 and won on length).
                score = (match > 0.99, round(match, 1), shout, sec if sec < 4.5 else -sec)
                if best is None or score > best[0]: best = (score, x, take + 1, match)
            _, x, chosen, match = best
            write(path, hall(loud(x)))
            print('%-40s take %d%s' % (os.path.basename(path), chosen, '' if match >= 0.75 else '   <-- WORDS DO NOT MATCH, listen to it'), flush=True)
        w = wave.open(path); sec = w.getnframes() / w.getframerate(); w.close()
        rows.append((key, text, sec))
    with open(HEADER, 'w') as f:
        f.write('// Generated by art/tools/announcer.py - do not edit. The ring announcer\'s lines: /Game/KS/Voice/V_Announcer_<Key>,\n')
        f.write('// the words, and the length in seconds (fighters wait for him to finish).\n#pragma once\n\n')
        f.write('struct FKSAnnouncerLine { const TCHAR* Key; const TCHAR* Text; float Seconds; };\n\n')
        f.write('static const FKSAnnouncerLine KSAnnouncerLines[] = {\n')
        for key, text, sec in rows:
            f.write('\t{ TEXT("%s"), TEXT("%s"), %.2ff },\n' % (key, text.replace('"', '\\"'), sec))
        f.write('};\n')
    print('%d lines -> %s' % (len(rows), os.path.relpath(HEADER, ROOT)))


if __name__ == '__main__':
    main()
