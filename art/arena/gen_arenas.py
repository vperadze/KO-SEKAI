"""Paints each arena's backdrop and ring mat with Dreamina (docs/ARENAS.md). Modelled on art/ui/gen_ui.py.

    python3 art/arena/gen_arenas.py quote [name ...]            # create the canvas nodes, print the cost; spends nothing
    python3 art/arena/gen_arenas.py run <ceiling> [name ...]    # generate (never more than <ceiling> credits) and download

No names: every picture that has no file yet. Names are bg_<arena> (21:9, the wall behind the crowd) and
mat_<arena> (1:1, the ring canvas seen from straight above). Results land in art/arena/raw/<name>.jpg;
art/tools/import_arenas.py brings them into the game as T_KS_Bg_<Arena> and T_KS_Mat_<Arena>.

The game shows a backdrop whole on the far wall and mirrored on the two side walls, so its left and right edges
only have to be calm, not to match each other. The lowest third is hidden behind the stands and the crowd.
"""
import json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, 'raw')
NODES = os.path.join(HERE, 'arena_nodes.json')
CANVAS = 'KO-SEKAI arenas'

# The look of the present arena picture (art/arena/crowd.jpg): a 1980s anime cel painting in neon colours.
BG = ("A wide panoramic background painting for a 1980s arcade fighting game, in the style of a 1980s Japanese anime cel: bold black ink "
      "outlines, flat saturated colours with hard-edged shading, glowing neon accents, deep dark shadows. Seen from ground level looking "
      "straight ahead, the horizon about one third up from the bottom. {scene} The bottom third of the picture is plain dark ground with "
      "little detail. Nobody in the foreground, no boxing ring, no fighters, no text, no letters, no logos, no watermark, no border.")
MAT = ("A boxing ring canvas seen from exactly straight above, filling the whole square picture edge to edge, flat, no perspective: "
       "worn painted canvas with scuffs, tape marks and faded patches, a coloured border stripe running round all four edges, and one big "
       "round emblem painted in the centre. {scene} Flat even light, no ropes, no posts, no people, no shadows of objects, no text, no "
       "letters, no numbers, no watermark.")

SCENES = {
    'space': ("The view from the open deck of a space station: a huge blue planet with glowing atmosphere filling the left of the sky, a "
              "ringed planet far off, thousands of stars, a magenta and violet nebula, station arms with rows of lit windows and blinking "
              "lights reaching in from both sides, distant spaceships with engine trails. Colours: deep blue, violet, cyan, magenta.",
              "Deep space blue canvas with cyan and violet border stripes; the emblem is a ringed planet with a starburst behind it, in cyan, violet and white."),
    'beach': ("A tropical beach at sunset: an enormous striped orange and pink sun half sunk in the sea, the sea glittering, tall palm trees "
              "in black silhouette, a long wooden pier with strings of coloured light bulbs, beach huts with neon signs shaped like cocktails "
              "and flamingos, a lifeguard tower, surfboards stuck in the sand, sailboats. Colours: orange, hot pink, turquoise, purple.",
              "Turquoise canvas with hot pink and orange border stripes; the emblem is a striped setting sun between two palm trees, in orange, pink and yellow."),
    'bar': ("The inside of a rowdy 1980s roadside bar seen from the middle of the room: a long wooden bar counter with a wall of bottles lit "
            "from behind, glowing neon signs shaped like beer mugs, cacti, arrows and stars, a jukebox glowing with coloured tubes, a pool table "
            "under a green lamp, a dart board, swinging saloon doors, ceiling fans, wooden beams, a moose head, hanging lamps in smoky air. "
            "Colours: warm amber, green neon, red neon, dark brown.",
            "Dark green felt canvas with amber and red border stripes; the emblem is a frothing beer mug crossed with two pool cues, in amber, red and cream."),
    'rooftop': ("A city rooftop at night in the rain: a skyline of skyscrapers with thousands of lit windows, giant glowing neon billboards "
                "with abstract shapes and no words, a huge full moon behind the towers, searchlights sweeping the clouds, water towers, "
                "antennas with red lights, steam rising from vents, a helicopter far off, wet surfaces reflecting pink and cyan. "
                "Colours: hot pink, cyan, deep blue, purple.",
                "Wet dark asphalt-grey canvas with hot pink and cyan border stripes; the emblem is a city skyline inside a full moon, in pink, cyan and white."),
    'dojo': ("An open wooden martial arts hall high in the mountains at dawn: red lacquered pillars and a curved tiled roof edge framing the view, "
             "paper lanterns glowing, a huge bronze gong on a stand, weapon racks, paper screens, cherry trees in full pink blossom, misty "
             "blue mountain peaks and a pagoda far away, a great red sun rising, banners with abstract brush strokes and no writing. "
             "Colours: red, gold, pink, pale blue.",
             "Straw-yellow woven mat canvas with red and black border stripes; the emblem is a red rising sun with a gold dragon coiled round it."),
    'castle': ("The great hall of a vampire's gothic castle at night: tall pointed stained-glass windows glowing purple and green, a towering "
               "pipe organ with gleaming pipes, iron candelabras with hundreds of dripping candles, stone arches and gargoyles, red velvet "
               "curtains, suits of armour, cobwebs, bats flying, a full moon through a broken window, green mist along the floor. "
               "Colours: purple, poison green, candle orange, blood red.",
               "Blood-red canvas with purple and gold border stripes; the emblem is a black bat with spread wings in front of a full yellow moon."),
    'gym': ("A 1980s aerobics studio: a wall of tall mirrors with glowing neon tube frames, a wall painted with huge zigzags, triangles and "
            "squiggles in the Memphis design style, ballet bars, step boards, dumbbell racks, exercise bikes, big boom boxes, potted palms, "
            "glass block windows glowing, a mirror ball, ceiling spotlights. Colours: hot pink, lemon yellow, aqua, white.",
            "Aqua canvas with hot pink and lemon yellow border stripes; the emblem is a pink dumbbell over a yellow zigzag lightning bolt inside a circle, Memphis design style."),
    'scrapyard': ("A scrapyard at night: towering walls of crushed and stacked cars, a huge crane with an electromagnet holding a car in the air, "
                  "a car crusher, piles of tyres, a conveyor belt, oil drums with fires burning in them, showers of welding sparks, sodium "
                  "floodlights on tall masts cutting through smoke, chain-link fence, factory chimneys and a dirty orange sky behind. "
                  "Colours: orange, yellow, rust, steel blue.",
                  "Rusty steel-plate canvas with yellow and black hazard border stripes; the emblem is a big orange cogwheel with a wrench and a lightning bolt inside it."),
    'volcano': ("The inside of a volcano crater at night: a great cone erupting in the centre with a fountain of lava, rivers and waterfalls "
                "of glowing lava running down black rock, a lake of lava, jagged black rock spires, glowing cracks in the ground, falling "
                "embers, towering smoke clouds lit red from below, lightning in the ash cloud. Colours: red, orange, yellow, black.",
                "Black volcanic rock canvas with glowing orange cracks and red and yellow border stripes; the emblem is an erupting volcano inside a ring of fire, in red, orange and yellow."),
    'studio': ("The set of a 1980s television game show: a giant spinning prize wheel with coloured segments, a wall of dozens of television "
               "screens showing colour bars and static, a glittering curtain, a staircase edged with rows of light bulbs, big studio cameras "
               "on wheeled stands, hanging studio lights, an applause sign shape with bulbs and no words, podiums with buttons, a shiny "
               "prize car on a turntable. Colours: yellow, red, blue, magenta.",
               "Royal blue canvas with yellow and red border stripes; the emblem is a television set showing colour bars inside a ring of yellow light bulbs."),
}
ART = {}
for name, (bg, mat) in SCENES.items():
    ART['bg_' + name] = ('21:9', BG.format(scene=bg))
    ART['mat_' + name] = ('1:1', MAT.format(scene=mat))


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


def load():
    return json.load(open(NODES)) if os.path.exists(NODES) else {'project': None, 'nodes': {}, 'prompts': {}}


def ensure_nodes(names):
    """A canvas node per picture, made again when its prompt has changed. Costs nothing."""
    state = load()
    if not state['project']:
        made = cli('canvas', 'create', CANVAS)
        state['project'] = made.get('projectId') or made['project']['projectId']
        json.dump(state, open(NODES, 'w'), indent=1)
    for n in names:
        ratio, prompt = ART[n]
        if state['nodes'].get(n) and state['prompts'].get(n) == prompt: continue
        d = cli('node', 'create', 'image', '--project-id', state['project'], '--mode', 't2i', '--model', 'seedream_4.7', '--ratio', ratio,
                '--resolution', '2K', '--title', 'arena ' + n, '--prompt', prompt)
        state['nodes'][n] = d['node']['nodeId']; state['prompts'][n] = prompt
        json.dump(state, open(NODES, 'w'), indent=1)
    return state


def quote(state, names):
    total = 0
    for at in range(0, len(names), 20):      # the service takes at most 20 nodes in one request
        ids = sum((['--node-id', state['nodes'][n]] for n in names[at:at + 20]), [])
        total += cli('node', 'quote', '--project-id', state['project'], *ids)['totalMaxCredits']
    return total


def download(state, resource, name):
    """The CLI will not write over a file, so a redone picture replaces the old one by way of a new name."""
    fresh = f'{RAW}/{name}.new.jpg'
    if os.path.exists(fresh): os.remove(fresh)
    cli('resource', 'download', resource, '--project-id', state['project'], '-o', fresh)
    os.replace(fresh, f'{RAW}/{name}.jpg')


def main():
    mode = sys.argv[1]
    rest = sys.argv[2:]
    ceiling = int(rest.pop(0)) if mode == 'run' else 0
    os.makedirs(RAW, exist_ok=True)
    names = rest or [n for n in ART if not os.path.exists(f'{RAW}/{n}.jpg')]
    if not names: sys.exit('nothing to make')
    state = ensure_nodes(names)
    cost = quote(state, names)
    print('pictures', len(names), 'quote', cost, flush=True)
    if mode != 'run': return
    if cost > ceiling: sys.exit(f'quote {cost} is over the ceiling {ceiling}; nothing was spent')
    for at in range(0, len(names), 20):
        part = names[at:at + 20]
        ids = sum((['--node-id', state['nodes'][n]] for n in part), [])
        out = cli('node', 'run', '--project-id', state['project'], *ids)
        for it in out['items']:
            n = next(k for k, v in state['nodes'].items() if v == it['nodeId'])
            w = cli('operation', 'wait', it['submitId'], '--project-id', state['project'])
            if w.get('state') == 'succeeded':
                res = w.get('resources') or it.get('resources') or []
                download(state, res[0]['resourceId'], n)
            print(n, w.get('state'), flush=True)


if __name__ == '__main__':
    main()
