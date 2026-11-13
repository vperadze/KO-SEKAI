"""Crowd sound from Dreamina. Its audio tool only speaks, so the crowd is recorded off the soundtrack of short videos
(seedance 2.0 mini, 720p: 5 credits a second).

    python3 art/arena/gen_crowd_sound.py <ceiling> <clip> [clip ...]

Saves art/arena/sound/<clip>.mp4. art/tools/prep_crowd.py cuts the soundtracks into game sounds.
"""
import json, os, subprocess, sys
from gen_crowd import cli, PROJECT

HERE = os.path.dirname(os.path.abspath(__file__))
LOOK = ("Static wide shot of a packed, smoky 1980s boxing arena crowd seen from the ring, hundreds of excited fans in 80s clothes on their feet. "
        "No music at any point, no commentator, no narrator, no bell. Sound only: ")
CLIPS = {   # name: (seconds, what is heard)
    'cheers': (10, "the whole crowd erupts in a loud cheer with applause and whistles for two seconds, then goes almost quiet for a second; "
                   "a second, different cheer with whoops; quiet again; then a third, enormous roaring ovation that goes on to the end."),
    'gasps':  (10, "the whole crowd lets out a sharp shocked 'OOOOH!' as if a boxer was hit hard, then near silence; a second pained collective groan "
                   "'ooooh'; near silence; then the whole crowd boos loudly and angrily, 'BOOOO', with jeers, to the end."),
    'shouts': (10, "over a low crowd murmur, single rough voices in the crowd shout one after another with a clear pause between them: a man yells "
                   "'Hit him!'; a woman screams 'Come on!'; a man bellows 'Knock him out!'; an old man yells 'Get up, you bum!'; a kid shouts "
                   "'Yeah! Do it again!'; a man yells 'My grandma hits harder!'."),
    'murmur': (10, "the steady, even murmur and chatter of a big restless arena crowd waiting, hundreds of indistinct voices, a few distant "
                   "whistles, constant level from start to end with no cheers and no clear words."),
    'beasts': (5,  "in the crowd a huge dinosaur lets out a deep roar; a short pause; then a gorilla hoots and beats its chest; a short pause; "
                   "then a dragon gives a rasping fiery screech."),
}


def main():
    ceiling, names = int(sys.argv[1]), sys.argv[2:]
    os.makedirs(f'{HERE}/sound', exist_ok=True)
    nodes = {}
    for n in names:
        seconds, heard = CLIPS[n]
        d = cli('node', 'create', 'video', '--project-id', PROJECT, '--mode', 't2v', '--model', 'seedance_2.0_mini', '--ratio', '16:9',
                '--resolution', '720p', '--duration', str(seconds), '--title', 'crowd sound ' + n, '--prompt', LOOK + heard)
        nodes[n] = d['node']['nodeId']
    ids = sum((['--node-id', v] for v in nodes.values()), [])
    quote = cli('node', 'quote', '--project-id', PROJECT, *ids)['totalMaxCredits']
    print('quote', quote, 'ceiling', ceiling, flush=True)
    if quote > ceiling:
        sys.exit('over the ceiling, nothing run')
    # Above some size the service wants the ceiling confirmed first; the token it returns lets the run through.
    token = cli('node', 'confirm', '--project-id', PROJECT, '--credit-ceiling', str(quote), *ids)
    token = token.get('creditConfirmationToken') or token.get('token') or next(v for k, v in token.items() if 'oken' in k)
    out = cli('node', 'run', '--project-id', PROJECT, '--credit-token', token, *ids)
    for it in out['items']:
        n = [a for a, b in nodes.items() if b == it['nodeId']][0]
        w = json.loads(subprocess.run(['dreamina-canvas', 'operation', 'wait', it['submitId'], '--timeout', '20m', '--format', 'json'], capture_output=True, text=True).stdout)
        state = w.get('data', {}).get('state')
        print(n, state, json.dumps(it.get('resources'))[:300], flush=True)
        if state == 'succeeded':
            r = subprocess.run(['dreamina-canvas', 'resource', 'download', it['resources'][0]['resourceId'], '--project-id', PROJECT, '-o', f'{HERE}/sound/{n}.mp4', '--format', 'json'], capture_output=True, text=True)
            print(r.stdout[:300], flush=True)


if __name__ == '__main__':
    main()
