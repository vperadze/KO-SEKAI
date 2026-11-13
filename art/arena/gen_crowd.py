"""Makes the six-pose spectator sheets with Dreamina (2 credits each).

    python3 art/arena/gen_crowd.py <ceiling> [name ...]      # no names: every character without a sheet yet

Each sheet is a 3x2 grid on green: idle, cheer, wince / shout, fist pump, boo. New characters are drawn from the
text in crowd_roster.py; the first twenty are redrawn from their old three-pose sheet so they stay themselves.
Slice the results with slice_crowd.py.
"""
import json, os, subprocess, sys, time
from crowd_roster import NEW, OLD

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = '60ac2f9c-c352-417a-ad39-71aa45081ef5'      # canvas "KO-SEKAI crowd"

STYLE = ("Sprite sheet from an old 1980s arcade cabinet video game, authentic early-80s 8-bit pixel art: very large chunky square pixels, "
         "low resolution, limited palette of about 12 bright colours, hard pixel edges, no gradients, no anti-aliasing, no smooth shading. ")
GRID = ("The SAME character drawn exactly six times in a strict grid of three columns and two rows, the whole body visible every time, front view facing "
        "the viewer, all six the same size, each one centred in its own cell with wide empty gaps between them and nothing overlapping or touching. "
        "Top row, left to right: 1) standing calmly, watching; 2) cheering wildly, both arms thrown high, mouth wide open; 3) wincing in shock, recoiling, hands on cheeks. "
        "Bottom row, left to right: 4) shouting, leaning forward with hands cupped around a wide open mouth; 5) punching one fist high in the air, grinning; "
        "6) booing angrily with a thumb pointed down and a furious scowl. Adapt each pose to the body this character has. ")
END = ("Flat solid pure green #00FF00 background everywhere, no green on the character, no shadows, no floor, no text, no numbers, no labels, no grid lines, no borders, no watermark.")


def cli(*args):
    for attempt in range(4):
        r = subprocess.run(['dreamina-canvas', *args, '--format', 'json'], capture_output=True, text=True)
        try:
            d = json.loads(r.stdout or r.stderr)
        except ValueError:
            sys.exit(f'{args[:3]}: {r.stdout[:300]} {r.stderr[:300]}')
        # "Temporarily unavailable" is common. Never repeat a run: that could charge twice.
        if d.get('ok') or args[1] == 'run' or not d.get('error', {}).get('retryable'):
            break
        time.sleep(5)
    if not d.get('ok'):
        sys.exit(f'{args[:3]}: {d}')
    return d['data']


def main():
    ceiling = int(sys.argv[1])
    names = sys.argv[2:] or [n for n in list(NEW) + OLD if not os.path.exists(f'{HERE}/crowd_{n}.jpg')]
    spent = 0
    for at in range(0, len(names), 20):       # the service takes at most 20 nodes in one request
        spent += batch(names[at:at + 20], ceiling - spent)


def batch(names, ceiling):
    nodes = {}
    for n in names:
        if n in NEW:
            prompt = STYLE + GRID + f"The character: {NEW[n][0]}. " + END
            extra = ['--mode', 't2i']
        else:
            res = cli('resource', 'upload', '--project-id', PROJECT, '--file', f'{HERE}/spec_{n}.jpg')
            rid = res.get('resourceId') or res['resource']['resourceId']
            prompt = (STYLE + "The character is the one in the reference picture: keep exactly the same character, clothes, colours, proportions and pixel style. "
                      + GRID + END)
            extra = ['--mode', 'i2i', '--ref', f'res:{rid}']
        d = cli('node', 'create', 'image', '--project-id', PROJECT, '--model', 'seedream_4.7', '--ratio', '16:9', '--resolution', '2K',
                '--title', 'crowd ' + n, '--prompt', prompt, *extra)
        nodes[n] = d['node']['nodeId']
    ids = sum((['--node-id', v] for v in nodes.values()), [])
    quote = cli('node', 'quote', '--project-id', PROJECT, *ids)['totalMaxCredits']
    print('quote', quote, 'ceiling', ceiling, flush=True)
    if quote > ceiling:
        sys.exit('over the ceiling, nothing run')
    out = cli('node', 'run', '--project-id', PROJECT, *ids)
    for it in out['items']:
        n = [a for a, b in nodes.items() if b == it['nodeId']][0]
        w = json.loads(subprocess.run(['dreamina-canvas', 'operation', 'wait', it['submitId'], '--format', 'json'], capture_output=True, text=True).stdout)
        state = w.get('data', {}).get('state')
        if state == 'succeeded':
            subprocess.run(['dreamina-canvas', 'resource', 'download', it['resources'][0]['resourceId'], '--project-id', PROJECT, '-o', f'{HERE}/crowd_{n}.jpg', '--format', 'json'], capture_output=True, text=True)
        print(n, state, flush=True)
    return quote


if __name__ == '__main__':
    main()
