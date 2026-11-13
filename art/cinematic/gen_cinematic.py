"""The opening film: six short shots made with Dreamina video, in the manner of an over-the-top 1980s action movie
on a worn VHS tape. The game plays them one after another before the menu (art/cinematic/prep_cinematic.py turns
the clips into frames the game can show; no video player is needed).

    python3 art/cinematic/gen_cinematic.py quote [shot ...]            # create the canvas nodes, print the cost; spends nothing
    python3 art/cinematic/gen_cinematic.py run <ceiling> [shot ...]    # generate (never more than <ceiling> credits), download

No shots named: every shot that has no file yet. Clips land in art/cinematic/raw/<shot>.mp4.
KS_VIDEO_MODEL picks the model (default seedance_2.0_mini, 720p).
"""
import json, os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'arena'))
from gen_arenas import cli

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, 'raw')
NODES = os.path.join(HERE, 'cinematic_nodes.json')
CANVAS = 'KO-SEKAI cinematic'
MODEL = os.environ.get('KS_VIDEO_MODEL', 'seedance_2.0_mini')

LOOK = ("A shot from an over-the-top 1980s action movie, filmed on 35 mm and seen on a worn VHS tape: heavy film grain, soft focus, "
        "tape tracking lines and colour bleed, anamorphic lens flares, smoke, hard backlight in magenta and cyan neon, wet streets. "
        "Dramatic, fast, a little ridiculous, completely serious. No text, no titles, no subtitles, no logos, no watermark. ")
SHOTS = {   # name: (seconds, what happens)
    'city':      (5, "Night, 1987. The camera flies fast and low over a neon city of skyscrapers and giant glowing billboards with abstract "
                     "shapes, a huge full moon, lightning forking across purple clouds, searchlights, then dives down between the towers "
                     "into a narrow alley full of steam."),
    'fighters':  (5, "Seven video game fighters walk side by side towards the camera in slow motion out of a wall of fire and exploding "
                     "arcade machines, not looking back: a huge soldier with a blond mullet and a red headband, a muscular knight with a "
                     "white unicorn's head, a woman in a neon aerobics leotard with enormous hair, a tiny furious grandmother in boxing "
                     "gloves, a chrome robot, a pale vampire with a towering black hairdo, and a man with a television set for a head. "
                     "Sparks rain down, their coats and hair blow in the blast."),
    # The first wording (businessmen slamming a stamp and pulling a lever) was refused by the service (error 20114).
    'cancelled': (4, "A huge dark warehouse full of rows of glowing arcade machines. One row after another the screens go black with a "
                     "crackle and a puff of sparks, the darkness sweeping towards the camera, until a single screen is left glowing at the "
                     "far end. Dust sheets are thrown over the dead machines. Thunder and rain outside the tall windows."),
    'alley':     (5, "A rain-soaked alley at night. One last arcade cabinet stands alone against a brick wall among rubbish and broken machines, "
                     "its screen flickering magenta and cyan, glowing like something alive, steam curling round it. A lone man seen from behind, "
                     "in a denim jacket with the collar up and a red headband, walks slowly towards it through the puddles. Low camera, slow push in."),
    'fists':     (4, "Extreme close-ups cut fast: hands pull a red headband tight behind a head; a hand in a red boxing glove clenches until the "
                     "leather creaks; eyes narrow under the headband as lightning flashes white across the face; two red boxing gloves bang "
                     "together with a burst of sparks."),
    # The first wording (electricity grabbing the man and dragging him in) was refused by the service (error 20115).
    'pull':      (5, "The screen of the lone arcade cabinet erupts in a blinding beam of magenta and cyan light that floods the whole alley. "
                     "The camera rushes forward straight into the screen and races down an endless glowing neon grid tunnel at tremendous "
                     "speed, lightning crackling along the walls, towards a white light that grows until it fills the whole picture."),
}


def load():
    return json.load(open(NODES)) if os.path.exists(NODES) else {'project': None, 'nodes': {}, 'prompts': {}}


def ensure_nodes(names):
    """A canvas node per shot, made again when its prompt or the model has changed. Costs nothing."""
    state = load()
    if not state['project']:
        made = cli('canvas', 'create', CANVAS)
        state['project'] = made.get('projectId') or made['project']['projectId']
        json.dump(state, open(NODES, 'w'), indent=1)
    for n in names:
        seconds, what = SHOTS[n]
        key = MODEL + '|' + str(seconds) + '|' + LOOK + what
        if state['nodes'].get(n) and state['prompts'].get(n) == key: continue
        d = cli('node', 'create', 'video', '--project-id', state['project'], '--mode', 't2v', '--model', MODEL, '--ratio', '16:9',
                '--resolution', '720p', '--duration', str(seconds), '--title', 'cinematic ' + n, '--prompt', LOOK + what)
        state['nodes'][n] = d['node']['nodeId']; state['prompts'][n] = key
        json.dump(state, open(NODES, 'w'), indent=1)
    return state


def main():
    mode = sys.argv[1]
    rest = sys.argv[2:]
    ceiling = int(rest.pop(0)) if mode == 'run' else 0
    os.makedirs(RAW, exist_ok=True)
    names = rest or [n for n in SHOTS if not os.path.exists(f'{RAW}/{n}.mp4')]
    if not names: sys.exit('nothing to make')
    state = ensure_nodes(names)
    ids = sum((['--node-id', state['nodes'][n]] for n in names), [])
    cost = cli('node', 'quote', '--project-id', state['project'], *ids)['totalMaxCredits']
    print('model', MODEL, 'shots', len(names), 'seconds', sum(SHOTS[n][0] for n in names), 'quote', cost, flush=True)
    if mode != 'run': return
    if cost > ceiling: sys.exit(f'quote {cost} is over the ceiling {ceiling}; nothing was spent')
    # Above some size the service wants the ceiling confirmed first; the token it returns lets the run through.
    token = cli('node', 'confirm', '--project-id', state['project'], '--credit-ceiling', str(cost), *ids)
    token = token.get('creditConfirmationToken') or token.get('token') or next(v for k, v in token.items() if 'oken' in k)
    out = cli('node', 'run', '--project-id', state['project'], '--credit-token', token, *ids)
    for it in out['items']:
        n = next(k for k, v in state['nodes'].items() if v == it['nodeId'])
        w = json.loads(subprocess.run(['dreamina-canvas', 'operation', 'wait', it['submitId'], '--project-id', state['project'], '--timeout', '20m', '--format', 'json'],
                                      capture_output=True, text=True).stdout or '{}')
        d = w.get('data', {})
        if d.get('state') == 'succeeded':
            res = d.get('resources') or it.get('resources') or []
            fresh = f'{RAW}/{n}.new.mp4'
            if os.path.exists(fresh): os.remove(fresh)
            cli('resource', 'download', res[0]['resourceId'], '--project-id', state['project'], '-o', fresh)
            os.replace(fresh, f'{RAW}/{n}.mp4')
        print(n, d.get('state'), flush=True)


if __name__ == '__main__':
    main()
