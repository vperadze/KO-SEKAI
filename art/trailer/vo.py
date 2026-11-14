#!/usr/bin/env python3
"""The trailer's voice-over, in the ring announcer's voice (the same local models as art/tools/announcer.py, whose
functions this uses; unlike it, nothing of the game is rewritten, so no rebuild).

    art/voice-model/venv/bin/python art/trailer/vo.py            every line that has no sound yet (about 2 minutes each)
    art/voice-model/venv/bin/python art/trailer/vo.py fists      these lines again

Writes art/trailer/vo/VO_<key>.wav and every take beside it (VO_<key>.take<N>.wav): listen, and copy another take
over the chosen one if it is better.
"""
import difflib, os, sys, zlib
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
from voices import OUT, RATE, echo, loud, lowpass, trim, words, write
from announcer import EMOTION, PACING

VO = os.path.join(HERE, 'vo')
TAKES = 4
# key: (what is said to the model, what the recogniser should have heard)
LINES = {
    'year':      ("In nineteen eighty-seven, one game was too much for the world.",) * 2,
    'cancelled': ("Seven fighters. Cancelled! Three weeks before launch.",) * 2,
    'cabinet':   ("One cabinet survived. And it wants a challenger!",) * 2,
    'fists':     ("No gamepad! Your fists are the controller!",) * 2,
    'rated':     ("Rated F. For fists!",) * 2,
}


def cinema(x):
    """A trailer voice: full and close, with a short room behind it (the ring's loudspeaker has no bass at all)."""
    x = x + 0.35 * lowpass(x, 220)
    x = np.tanh(x / np.abs(x).max() * 1.4)
    x = echo(x, [(0.061, 0.16), (0.137, 0.09), (0.251, 0.05)], 0.5)
    fade = int(0.12 * RATE); x[-fade:] *= np.linspace(1, 0, fade)
    return x / np.abs(x).max() * 0.95


def main():
    import torch
    from chatterbox.tts import ChatterboxTTS
    from faster_whisper import WhisperModel
    torch.set_num_threads(int(os.environ.get('KS_THREADS', '8')))
    os.makedirs(VO, exist_ok=True)
    ref = os.path.join(OUT, 'takes', 'ref_Announcer.wav')
    only = set(sys.argv[1:])
    todo = [k for k in LINES if k in only or (not only and not os.path.exists(os.path.join(VO, 'VO_%s.wav' % k)))]
    if not todo: sys.exit('nothing to make')
    actor = ChatterboxTTS.from_pretrained(device='cpu')
    ears = WhisperModel('small.en', device='cpu', compute_type='int8')
    for key in todo:
        said, wanted = LINES[key]
        best = None
        for take in range(TAKES):
            torch.manual_seed(zlib.crc32(('Trailer' + key).encode()) % 100000 + take)
            x = actor.generate(said, audio_prompt_path=ref, exaggeration=EMOTION, cfg_weight=PACING, temperature=0.85)
            x = trim(x.squeeze().numpy().astype(np.float64))
            write(os.path.join(VO, 'VO_%s.take%d.wav' % (key, take + 1)), cinema(loud(x)))
            at16k = np.interp(np.arange(int(len(x) * 16000 / RATE)) * RATE / 16000.0, np.arange(len(x)), x / np.abs(x).max()).astype(np.float32)
            heard = ' '.join(seg.text for seg in ears.transcribe(at16k, language='en')[0])
            match = max(difflib.SequenceMatcher(None, ''.join(words(t)), ''.join(words(heard))).ratio() for t in (said, wanted))
            sec = len(x) / RATE
            print('  %s take %d  %.2f s  match %.2f  "%s"' % (key, take + 1, sec, match, heard.strip()), flush=True)
            score = (round(match, 1), -abs(sec - 0.075 * len(said)))       # the right words first, then an unhurried, unstretched reading
            if best is None or score > best[0]: best = (score, x, take + 1, match)
        _, x, chosen, match = best
        write(os.path.join(VO, 'VO_%s.wav' % key), cinema(loud(x)))
        print('VO_%s.wav  take %d  %.2f s%s' % (key, chosen, len(x) / RATE, '' if match >= 0.8 else '   <-- WORDS DO NOT MATCH, listen to it'), flush=True)


if __name__ == '__main__':
    main()
