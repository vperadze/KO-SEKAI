#!/usr/bin/env python3
"""Painted portraits of the fighters for the select screen, redrawn by Dreamina from photographs of the game's models.

    python3 art/ui/gen_avatars.py quote [Folder ...]          # upload the photographs, make the nodes, print the cost; spends nothing
    python3 art/ui/gen_avatars.py run <ceiling> [Folder ...]  # generate (never more than <ceiling> credits) and download

Reads unreal/Saved/Screenshots/avatar_<Folder>.png (the game run with -KSPortraits), writes art/ui/raw/av_<Folder>.jpg.
Then art/ui/make_avatars.py cuts them to size and art/ui/import_ui.py brings them in.
"""
import json, os, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_ui import RAW, cli, load

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'unreal', 'Saved', 'Screenshots')
NODES = os.path.join(HERE, 'avatar_nodes.json')

STYLE = ("Redraw the character in the reference picture {{res:%s}} as a character-select portrait for a 1987 arcade fighting game: "
         "hand-painted 1980s arcade cabinet art, bold airbrushed colours, thick dark ink outlines, hard white highlights, dramatic rim light "
         "in magenta and cyan. Head and shoulders, turned a little to one side, filling the square. Keep exactly this character: %s. "
         "Expression: %s. Background: a flat %s burst of comic speed lines, nothing else. No text, no letters, no border, no watermark.")
WHO = {
    'SergeantMulletV2': ("a muscular soldier with a huge blond mullet, a red headband, a thick moustache and a grey tank top", "a cocky sneer", "orange and red"),
    'ChannelZero': ("a fighter whose head is an old beige CRT television set showing a grinning orange face on its screen", "the screen face grinning wickedly", "green and black"),
    'SirLasercorn': ("a muscular man with a white unicorn head, a spiral pink horn and a rainbow mane", "proud and majestic, nostrils flared", "pink and sky blue"),
    'AerobicsAnnihilator': ("a tanned woman with enormous teased blonde hair, a pink sweatband and a purple leotard", "a fierce toothy smile", "hot pink and yellow"),
    'GrandmaThunder': ("a small old lady with a lilac permed bun, round glasses, a grey knitted cardigan and golden boxing gloves", "a stern glare over her glasses", "yellow and purple"),
    'Turbotron': ("a boxing robot with a dark dome helmet, a face of red LED dots on a black visor and red shoulder armour", "angry red LED eyes", "orange and steel blue"),
    'CountHairspray': ("a pale vampire with a gigantic black glam-rock hairdo, dark eye make-up, fangs and a studded black leather jacket", "a smug fanged grin", "purple and blood red"),
}


def main():
    mode = sys.argv[1]
    rest = sys.argv[2:]
    ceiling = int(rest.pop(0)) if mode == 'run' else 0
    names = rest or [n for n in WHO if not os.path.exists(f'{RAW}/av_{n}.jpg')]
    if not names: sys.exit('nothing to make')
    project = load()['project']
    state = json.load(open(NODES)) if os.path.exists(NODES) else {}
    for n in names:
        entry = state.setdefault(n, {})
        if not entry.get('resource'):
            # The middle square of the photograph: that is where the head is.
            im = Image.open(f'{SHOTS}/avatar_{n}.png').convert('RGB'); w, h = im.size
            ref = f'{RAW}/av_{n}.ref.png'
            im.crop(((w - h) // 2, 0, (w + h) // 2, h)).save(ref)
            up = cli('resource', 'upload', '--project-id', project, '--file', ref, '--name', 'avatar ref ' + n)
            entry['resource'] = up.get('resourceId') or up['resource']['resourceId']
            json.dump(state, open(NODES, 'w'), indent=1)
        prompt = STYLE % ((entry['resource'],) + WHO[n])
        if not entry.get('node') or entry.get('prompt') != prompt:
            d = cli('node', 'create', 'image', '--project-id', project, '--mode', 'i2i', '--model', 'seedream_4.7', '--ratio', '1:1',
                    '--resolution', '2K', '--title', 'Avatar ' + n, '--prompt', prompt, '--ref', 'res:' + entry['resource'])
            entry['node'] = d['node']['nodeId']; entry['prompt'] = prompt
            json.dump(state, open(NODES, 'w'), indent=1)
    ids = sum((['--node-id', state[n]['node']] for n in names), [])
    cost = cli('node', 'quote', '--project-id', project, *ids)['totalMaxCredits']
    print('portraits', len(names), 'quote', cost, flush=True)
    if mode != 'run': return
    if cost > ceiling: sys.exit(f'quote {cost} is over the ceiling {ceiling}; nothing was spent')
    out = cli('node', 'run', '--project-id', project, *ids)
    for it in out['items']:
        n = next(k for k, v in state.items() if v.get('node') == it['nodeId'])
        w = cli('operation', 'wait', it['submitId'], '--project-id', project)
        if w.get('state') == 'succeeded':
            res = w.get('resources') or it.get('resources') or []
            fresh = f'{RAW}/av_{n}.new.jpg'
            if os.path.exists(fresh): os.remove(fresh)
            cli('resource', 'download', res[0]['resourceId'], '--project-id', project, '-o', fresh)
            os.replace(fresh, f'{RAW}/av_{n}.jpg')
        print(n, w.get('state'), flush=True)


if __name__ == '__main__':
    main()
