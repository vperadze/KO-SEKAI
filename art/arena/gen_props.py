#!/usr/bin/env python3
"""The arena's 3D set pieces, made by Meshy from text: a kit every arena uses (stands, truss, lamps, ring posts,
barriers, loudspeakers, scoreboard) and one landmark for each themed arena.

    python3 art/arena/gen_props.py quote [name ...]          what is still to buy and the credits, no spend
    python3 art/arena/gen_props.py run <ceiling> [name ...]  buy what is missing, never more than <ceiling> credits
    python3 art/arena/gen_props.py redo <ceiling> <name>     throw one result away and buy it again

A model is two paid steps: the shape (20 credits) and its textures (10). Each step's task id is written to
art/models/_props/<name>/state.json as soon as it is known, so a rerun picks up where it stopped and never pays twice.
The finished model is art/models/_props/<name>/model.glb with thumbnail.png beside it: look at every thumbnail
before importing. art/tools/import_props.py brings them into the game (headless); KSWorld.cpp places them by name.
"""
import json, os, shutil, subprocess, sys, threading
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
STORE = os.path.join(ROOT, 'art', 'models', '_props')
SHAPE, PAINT = 20, 10

# Neutral dark metal for the kit: the arena's own neon colours are added in code, so one kit suits all eleven.
KIT = 'Game prop, single object, centred, plain background. Matte black and dark gunmetal steel with chrome details. No text, no logo, no people.'
LAND = 'Game prop, single object, centred, plain background, stylised 1980s arcade look, rich saturated colours. No text, no letters, no logo, no people.'
PROPS = {
    # --- the kit ---
    'Podium':     'A modular concert stage riser platform section: a low rectangular deck two metres wide, one metre deep and forty centimetres high, flat black non-slip top, closed sides of perforated black steel panels in an aluminium frame, a chrome trim rail along the top front edge. Perfectly box-shaped, flat top. ' + KIT,
    'Tower':      'A round raised VIP podium: a cylindrical drum stage three metres across and two metres high with a completely flat round top, sides clad in black quilted padded panels with chrome bands and round studio lamps set into the side, a short flight of steps at one side. No railing. ' + KIT,
    # The first try came back as a cube of truss; it has to be a beam, hence the proportions spelled out.
    'Truss':      'A very long and thin straight lighting truss beam, like a ladder lying flat: ten times longer than it is thick. Four long parallel aluminium tubes run its whole length, joined by many short diagonal zigzag braces. A long narrow horizontal girder with square ends. ' + KIT,
    'Lamp':       'A stage PAR can spotlight standing on its back with the lens pointing straight up: a short wide black metal cylinder with cooling fins, a glass lens with a chrome ring on top, a U-shaped yoke bracket around it, barn-door flaps open around the lens. ' + KIT,
    'MovingHead': 'A professional moving-head beam stage light standing upright on the floor: a flat square base with a small display, a U-shaped yoke arm, a barrel-shaped head held in the yoke with a large round glass lens pointing straight up. ' + KIT,
    'Post':       'A boxing ring corner post: a thick vertical steel post one and a half metres tall on a square base plate, three padded turnbuckle cushions stacked up one side, three chrome turnbuckle hooks, a round cap on top. ' + KIT,
    'Barrier':    'One section of a concert crowd-control barricade: a low solid steel front panel with a rounded padded black top rail, a step plate at the back and angled braces, two and a half metres wide and one metre high. ' + KIT,
    'Speakers':   'A tall concert loudspeaker tower: eight line-array speaker cabinets hung in a curved column from a steel truss frame, two large subwoofer cabinets stacked at the bottom, front grilles of black mesh. ' + KIT,
    'Scoreboard': 'A four-sided arena scoreboard cube that hangs from the roof: four big blank dark glass screens facing outward in a chunky frame with rows of small round bulbs around every screen, a ring of loudspeakers underneath, steel cables on top. ' + KIT,
    # --- one landmark for each arena (keys of KSArenaData.h) ---
    'Space':      'A retro 1950s science-fiction rocket ship standing upright on three swept fins on a small launch pad: polished chrome hull with red and white bands, round portholes, a pointed nose cone, a gantry ladder beside it. ' + LAND,
    'Beach':      'A wooden beach lifeguard tower on stilts: a small cabin with a sloped roof and a railed deck, a ramp down to the sand, weathered white planks with turquoise and orange trim, a surfboard and a life ring leaning on it, a palm tree beside it. ' + LAND,
    'Bar':        'A classic 1950s diner jukebox with an arched top: glowing bubble tubes in orange, green and red around the arch, a chrome grille, a window showing a row of records, polished dark wood sides. ' + LAND,
    'Rooftop':    'An old city rooftop water tower: a round wooden barrel tank with a conical roof on a tall riveted steel frame with cross braces and a ladder, a pink and cyan neon tube ring around the tank, an air-conditioning unit and a satellite dish at its foot. ' + LAND,
    'Dojo':       'A Japanese torii shrine gate: two thick red lacquered wooden pillars on stone bases, a curved black top beam with upturned ends, a second crossbeam below, a thick rope with paper tassels hung across, two stone lanterns at its feet. ' + LAND,
    'Castle':     'A gothic cathedral pipe organ: tall rows of tarnished silver pipes rising in pointed arches, a console of dark carved wood with three keyboards, bat wing carvings, candelabras with melted candles on both sides, purple velvet. ' + LAND,
    'Gym':        'A giant 1980s boombox cassette player: silver and hot pink body, two big round speakers with chrome rings, a cassette door in the middle, rows of buttons and sliders, a carry handle and a telescopic aerial on top. ' + LAND,
    'Scrapyard':  'A tall stack of four crushed wrecked cars piled on top of each other, rusty and dented in faded red, yellow, blue and green paint, broken windows, a big round crane electromagnet resting on the top car with a chain. ' + LAND,
    'Volcano':    'A huge ancient stone idol head statue carved from black basalt: a scowling face with heavy brows, an open mouth and eyes glowing with orange lava, cracks of glowing lava running down it, standing on a rough rock base. ' + LAND,
    'Studio':     'A 1980s television studio camera on a rolling pedestal: a big boxy broadcast camera with a long zoom lens and a lens hood, a viewfinder on top, a red tally light, cables, mounted on a heavy three-wheeled studio pedestal with a steering ring. ' + LAND,
}

lock = threading.Lock()
spent = 0


def folder(name): return os.path.join(STORE, name)
def model(name): return os.path.join(folder(name), 'model.glb')


def state(name):
    try: return json.load(open(os.path.join(folder(name), 'state.json')))
    except (OSError, ValueError): return {}


def save(name, st):
    os.makedirs(folder(name), exist_ok=True)
    json.dump(st, open(os.path.join(folder(name), 'state.json'), 'w'), indent=1)


def owed(name):
    if os.path.exists(model(name)): return 0
    return PAINT if state(name).get('shape_ok') else SHAPE + PAINT


def reserve(credits, ceiling):
    global spent
    with lock:
        if spent + credits > ceiling: return False
        spent += credits
        return True


def meshy(args):
    r = subprocess.run(['meshy'] + args, capture_output=True, text=True)
    try: out = json.loads(r.stdout[r.stdout.index('{'):])
    except ValueError: out = {'ok': False, 'error': {'message': (r.stdout + r.stderr)[-400:]}}
    return out


def make(name, ceiling):
    st = state(name); st.setdefault('try', 1)
    if not st.get('shape_ok'):
        if not reserve(SHAPE, ceiling): return name, 'SKIPPED: over the ceiling'
        out = meshy(['text-to-3d', 'create', '--mode', 'preview', '--prompt', PROPS[name], '--timeout', '1500',
                     '--operation-id', 'ks-prop-%s-shape-%d' % (name, st['try'])])
        task = ((out.get('result') or {}).get('task') or {})
        if task.get('task_id'): st['shape'] = task['task_id']; save(name, st)
        if not out.get('ok') or task.get('status') != 'SUCCEEDED': return name, 'FAILED shape: %s' % json.dumps(out.get('error') or task.get('status'))[:300]
        st['shape_ok'] = True; save(name, st)
    if not reserve(PAINT, ceiling): return name, 'SKIPPED: over the ceiling'
    tmp = os.path.join(folder(name), 'out')
    shutil.rmtree(tmp, ignore_errors=True)
    out = meshy(['text-to-3d', 'create', '--mode', 'refine', '--preview-task-id', st['shape'], '--enable-pbr', 'true',
                 '--timeout', '1500', '-o', tmp, '--operation-id', 'ks-prop-%s-paint-%d' % (name, st['try'])])
    task = ((out.get('result') or {}).get('task') or {})
    if task.get('task_id'): st['paint'] = task['task_id']; save(name, st)
    glb = os.path.join(tmp, 'model.glb')
    if not os.path.exists(glb): return name, 'FAILED paint: %s' % json.dumps(out.get('error') or task.get('status'))[:300]
    for f in os.listdir(tmp): os.replace(os.path.join(tmp, f), os.path.join(folder(name), f))
    os.rmdir(tmp)
    return name, 'ok'


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'quote'
    rest = sys.argv[2:]
    ceiling = 0
    if mode in ('run', 'redo'): ceiling = int(rest[0]); rest = rest[1:]
    names = rest or list(PROPS)
    for n in names: assert n in PROPS, 'no such prop: ' + n
    for n in names: assert len(PROPS[n]) <= 800, (n, len(PROPS[n]))
    if mode == 'redo':
        assert len(names) == 1 and rest
        st = state(names[0])
        old = os.path.join(folder(names[0]), 'try%d' % st.get('try', 1))
        os.makedirs(old, exist_ok=True)
        for f in os.listdir(folder(names[0])):
            p = os.path.join(folder(names[0]), f)
            if os.path.isfile(p) and f != 'state.json': os.replace(p, os.path.join(old, f))
        save(names[0], {'try': st.get('try', 1) + 1})
    todo = [n for n in names if owed(n)]
    total = sum(owed(n) for n in todo)
    print('%d of %d to buy, %d credits' % (len(todo), len(names), total))
    for n in todo: print('  %-11s %d' % (n, owed(n)))
    if mode == 'quote' or not todo: return
    if total > ceiling: print('over the ceiling of %d: buying in the order listed until it is reached' % ceiling)
    with ThreadPoolExecutor(6) as pool:
        for name, result in pool.map(lambda n: make(n, ceiling), todo): print('%-11s %s' % (name, result), flush=True)
    print('reserved %d credits' % spent)


if __name__ == '__main__': main()
