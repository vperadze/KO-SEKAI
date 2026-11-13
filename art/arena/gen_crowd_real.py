#!/usr/bin/env python3
"""Redraws the fifty spectators as people of the fighters' world: rendered 3D characters, not arcade pixels.

    art/tracker-model/venv/bin/python art/arena/gen_crowd_real.py quote [name ...]          # make the nodes, print the cost; spends nothing
    art/tracker-model/venv/bin/python art/arena/gen_crowd_real.py run <ceiling> [name ...]  # generate (never above <ceiling> credits)

No names: every character without a new sheet yet. Each character is drawn from his description in crowd_roster.py
(or OLD_WHO below) and nothing else: with his old pixel sprite as a reference the result stayed a pixel doll. The new sheet real_<name>.jpg is a 4x2 grid on green with eight poses:
idle, cheer, wince, shout / fist pump, boo, clap, dance. Slice with slice_crowd.py, import with art/tools/import_crowd.py.
2 credits a sheet. Nodes are remembered in real_nodes.json, so a quote followed by a run makes each node once.
"""
import json, os, sys
from gen_crowd import PROJECT, cli
from crowd_roster import NEW, OLD, MORE

HERE = os.path.dirname(os.path.abspath(__file__))
NODES = os.path.join(HERE, 'real_nodes.json')

# What the old twenty are, in words (their pixel sheets alone leave too much to guess at this level of detail).
OLD_WHO = {
    'kid': "a small boy of about nine in a striped t-shirt, shorts and a baseball cap", 'robot': "a boxy tin toy robot with an antenna and dial chest",
    'mom': "a suburban mother with big permed hair, a pastel sweater and high-waisted jeans", 'luchador': "a masked Mexican wrestler in a cape and bright tights",
    'punk': "a punk with a tall mohawk, a studded leather jacket and tartan trousers", 'alien': "a small grey-green alien with big black eyes in a tourist shirt",
    'nerd': "a skinny nerd with thick glasses, a pocket protector, a short-sleeved shirt and high trousers", 'rocker': "a long-haired heavy metal fan in a denim vest and band t-shirt",
    'skater': "a skater kid with a helmet, knee pads, baggy shorts and a skateboard", 'boss': "a businessman in a wide-shouldered power suit with a brick mobile phone",
    'bboy': "a breakdancer in a tracksuit, a bucket hat and a gold chain", 'oldman': "a small bald old man with a white moustache, braces and a walking stick",
    'ninja': "a ninja in black with a red headband", 'astro': "an astronaut in a white space suit with the visor up",
    'vampire': "a vampire in a high-collared black and red cape", 'chef': "a fat chef in whites with a tall hat and a moustache",
    'cheer': "a cheerleader with pom-poms and a high ponytail", 'cowboy': "a cowboy with a big hat, a fringed shirt and boots",
    'prof': "a wild-haired professor in a lab coat with goggles", 'wizard': "an old wizard with a long beard, a pointed hat and a starry robe",
}

STYLE = ("A character turnaround sheet for a modern 3D fighting video game set in the 1980s. Photorealistic 3D render of a real adult "
         "person: true human proportions, eight heads tall, a small normal-sized head, long legs, real anatomy, realistic skin with pores, "
         "real hair strands, real fabric with folds, cinematic studio lighting. The person is funny because of the outfit, the hair and the "
         "over-the-top acting, not because of the drawing style. NOT a cartoon, NOT pixel art, NOT chibi, NOT a toy or figurine, NOT clay, "
         "NOT an animated film character, no big head, no big eyes, no ink outlines. ")
WHO = "The character: %s. "
# Those that are not people: the "real adult person" wording above put a man into their sheets (a man with a dinosaur's
# legs, a man beside the saucer). They get a creature's style line, and the alien is grey: green skin is keyed away.
BEAST = ("A character turnaround sheet for a modern 3D fighting video game set in the 1980s. Photorealistic 3D render of one creature, the "
         "same creature in every cell and nothing else: no human, no person, no man anywhere. Realistic skin, scales, fur or metal, cinematic "
         "studio lighting, funny through its acting. NOT a cartoon, NOT pixel art, NOT a toy, no ink outlines. ")
BEASTS = {
    'trex': "a huge orange tyrannosaurus rex with tiny arms, a mouth full of teeth, wearing a tiny baseball cap; it is a dinosaur in all eight poses",
    'kraken': "a big teal octopus sea monster with many curling tentacles and angry yellow eyes, sitting in a metal washtub; it acts with its tentacles",
    'ufo': "a small silver flying saucer with coloured lights and a glass dome, a little grey alien pilot inside the dome; the saucer hovers and tilts, the pilot inside does the acting with his arms",
    'phoenix': "a flaming phoenix bird with orange and yellow fire feathers, a beak and spread wings; a bird in all eight poses, it acts with its wings",
    'robot': "a boxy orange tin toy robot the size of a man, with a square head, an antenna, a dial on its chest and clamp hands; a robot in all eight poses",
    'pug': None,       # described in crowd_roster.MORE; listed here for the creature's style line
    'alien': "a small alien with pale GREY skin (not green), a big bald head and huge black eyes, in a loud tourist shirt, shorts and sandals",
}
GRID = ("The SAME character shown exactly eight times in a strict grid of four columns and two rows, front view. FULL BODY every "
        "time: the complete figure from the top of the hair down to the shoes, both feet fully visible standing on nothing, with clear "
        "empty green space above the head, below the feet and on both sides; the camera stands far enough back for a tall adult, whose head "
        "is only one eighth of the body height, to fit whole; never cropped by the edge of the picture or by its neighbours. Facing the viewer, all eight the same size, each one centred in its own cell with wide empty gaps between "
        "them and nothing overlapping or touching. "
        "Top row, left to right: 1) standing relaxed, watching; 2) cheering wildly, both arms thrown high, mouth wide open; 3) wincing in "
        "shock, recoiling, both hands on the cheeks; 4) shouting, leaning forward with hands cupped around a wide open mouth. "
        "Bottom row, left to right: 5) punching one fist high in the air, grinning; 6) booing angrily with a thumb pointed down and a furious "
        "scowl; 7) clapping both hands together in front of the chest, laughing; 8) a silly victory dance, one knee lifted, elbows out. "
        "Adapt each pose to the body this character has. ")
END = ("Flat solid pure green #00FF00 background everywhere, no green on the character, no shadows on the ground, no floor, no text, no numbers, "
       "no labels, no grid lines, no borders, no watermark.")


def who(n):
    return NEW[n][0] if n in NEW else MORE[n][0] if n in MORE else OLD_WHO[n]


def main():
    mode, rest = sys.argv[1], sys.argv[2:]
    ceiling = int(rest.pop(0)) if mode == 'run' else 0
    names = rest or [n for n in list(NEW) + OLD + list(MORE) if not os.path.exists(f'{HERE}/real_{n}.jpg')]
    if not names: sys.exit('nothing to make')
    state = json.load(open(NODES)) if os.path.exists(NODES) else {}
    spent = 0
    for at in range(0, len(names), 20):                   # the service takes at most 20 nodes in one request
        spent += batch(mode, names[at:at + 20], ceiling - spent, state)
    print('total', spent, flush=True)


def batch(mode, names, ceiling, state):
    for n in names:
        entry = state.setdefault(n, {})
        prompt = (BEAST + WHO % (BEASTS[n] or who(n)) if n in BEASTS else STYLE + WHO % who(n)) + GRID + END
        if not entry.get('node') or entry.get('prompt') != prompt:
            d = cli('node', 'create', 'image', '--project-id', PROJECT, '--mode', 't2i', '--model', 'seedream_4.7', '--ratio', '16:9' if n in NEW and NEW[n][1] == 'wide' else '4:3',
                    '--resolution', '2K', '--title', 'real ' + n, '--prompt', prompt)
            entry['node'] = d['node']['nodeId']; entry['prompt'] = prompt
            json.dump(state, open(NODES, 'w'), indent=1)
    ids = sum((['--node-id', state[n]['node']] for n in names), [])
    cost = cli('node', 'quote', '--project-id', PROJECT, *ids)['totalMaxCredits']
    print('sheets', len(names), 'quote', cost, flush=True)
    if mode != 'run': return cost
    if cost > ceiling: sys.exit(f'quote {cost} is over what is left of the ceiling ({ceiling}); this batch was not run')
    out = cli('node', 'run', '--project-id', PROJECT, *ids)
    for it in out['items']:
        n = next(k for k, v in state.items() if v.get('node') == it['nodeId'])
        w = cli('operation', 'wait', it['submitId'], '--project-id', PROJECT)
        if w.get('state') == 'succeeded':
            res = w.get('resources') or it.get('resources') or []
            fresh = f'{HERE}/real_{n}.new.jpg'
            if os.path.exists(fresh): os.remove(fresh)
            cli('resource', 'download', res[0]['resourceId'], '--project-id', PROJECT, '-o', fresh)
            os.replace(fresh, f'{HERE}/real_{n}.jpg')
        print(n, w.get('state'), flush=True)
    return cost


if __name__ == '__main__':
    main()
