#!/usr/bin/env python3
"""Extra animation clips for the fighters, from Meshy's library: fun idles and a "picked" move for the select screen,
a special-ability move, a win celebration, and a stagger for an attacker whose punch was blocked.

    python3 art/tools/add_moves.py quote          what is still to buy, and the credits (3 a clip), no spend
    python3 art/tools/add_moves.py run <ceiling>  buy what is missing (never more than <ceiling> credits) and build the files
    python3 art/tools/add_moves.py build          only rebuild the per-fighter files from clips already bought

A clip is bought once, on Turbotron's rig, and kept in art/models/_moves/<action id>/. Every Meshy rig has the same
26 bones, so art/tools/transplant_anim.py copies it onto each fighter (rotations only: proportions stay the fighter's).
Writes art/models/<fighter>/extra/extra.glb with the clips named by key (Fun_A, Fun_B, Picked, Special, Win, Stagger,
Shocked, Blown);
art/tools/import_moves.py brings them into the game (headless), and KSDummy.cpp loads them by those names.
"""
import json, os, struct, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
MODELS = os.path.join(ROOT, 'art', 'models')
STORE = os.path.join(MODELS, '_moves')
RIG = '01a1d056-2301-766b-bffb-b01a01517cea'        # Turbotron's rigging task
COST = 3

# Meshy library action ids (meshy animation-catalog list). The names are Meshy's.
STAGGER = 404                                         # Crouch and Step Back
# Hit by a special ability (AKSGameMode::SpecialHitClip): jolted rigid by anything electric, thrown back by the rest.
SHOCKED, BLOWN = 172, 178                             # Electrocution Reaction, Hit Reaction
MOVES = {
    'sergeant-mullet-v2':   {'Fun_A': 385, 'Fun_B': 388, 'Picked': 51,  'Special': 127, 'Win': 403},   # Boxing Warm-up, Show Both Arm Muscles, Shouting Angrily, Charged Ground Slam, Victory Fist Pump
    'channel-zero':         {'Fun_A': 36,  'Fun_B': 79,  'Picked': 408, 'Special': 125, 'Win': 74},    # Confused Scratch, OMG Groove, Jazz Hands, Charged Spell Cast, Gangnam Groove
    'sir-lasercorn':        {'Fun_A': 415, 'Fun_B': 77,  'Picked': 27,  'Special': 129, 'Win': 405},   # Happy Sway Standing, Magic Genie, Big Heart Gesture, Mage Spell Cast, Joyful Dance with Hand Sway
    'aerobics-annihilator': {'Fun_A': 326, 'Fun_B': 70,  'Picked': 44,  'Special': 397, 'Win': 82},    # Jumping Jacks, Cardio Dance, Happy Jump Female, 360 Power Spin Jump, Shake It Off Dance
    'grandma-thunder':      {'Fun_A': 38,  'Fun_B': 24,  'Picked': 409, 'Special': 126, 'Win': 596},   # Dozing Elderly, Funny Dancing 3, Finger Wag No, Charged Spell Cast 1, Ymca Dance
    'turbotron':            {'Fun_A': 22,  'Fun_B': 73,  'Picked': 387, 'Special': 398, 'Win': 395},   # Funny Dancing 1, Fast Lightning, Half Squat with Thumb Up, Crouch Charge and Throw, Breakdance 1990
    'count-hairspray':      {'Fun_A': 48,  'Fun_B': 595, 'Picked': 411, 'Special': 130, 'Win': 592},   # Mirror Viewing, Jazz Dance, Neck Slashing Gesture, Mage Spell Cast 1, Hip Hop Dance 1
}
for table in MOVES.values(): table.update(Stagger=STAGGER, Shocked=SHOCKED, Blown=BLOWN)


def clip_path(action): return os.path.join(STORE, str(action), 'animation_glb.glb')


def wanted():
    return sorted({a for table in MOVES.values() for a in table.values()})


def buy(actions):
    """One Meshy call per clip: a failed one costs nothing and the rest are kept."""
    for action in actions:
        out = os.path.join(STORE, str(action))
        r = subprocess.run(['meshy', 'animate', 'create', '--rig-task-id', RIG, '--action-ids', str(action), '-o', out,
                            '--operation-id', 'ks-move-%d' % action], capture_output=True, text=True)
        print('%4d  %s' % (action, 'ok' if os.path.exists(clip_path(action)) else 'FAILED ' + (r.stdout + r.stderr)[-300:]), flush=True)


def rename(path, names):
    """Gives the clips in a GLB their key names, in order, and keeps every clip on the spot.

    Meshy's dances and warm-ups wander: the hips travel a metre or more, which walked the fighters out of the picture
    on the select screen. The slow travel of the hips is taken out (a moving average over 1.2 s, sideways and
    forwards only); the quick sway and every up-and-down stay. Stagger keeps a quarter of its step back.
    """
    import numpy as np
    raw = open(path, 'rb').read()
    n, _ = struct.unpack('<I4s', raw[12:20]); j = json.loads(raw[20:20 + n])
    rest = bytearray(raw[20 + n:]); base = 8                                  # the binary chunk's own 8-byte header
    assert len(j['animations']) == len(names), (path, len(j['animations']), names)
    hips = next(i for i, nd in enumerate(j['nodes']) if nd.get('name') == 'Hips')

    def span(index):
        acc = j['accessors'][index]; view = j['bufferViews'][acc['bufferView']]
        return base + view.get('byteOffset', 0) + acc.get('byteOffset', 0), acc['count']

    for a, name in zip(j['animations'], names):
        a['name'] = name
        for ch in a['channels']:
            if ch['target']['node'] != hips or ch['target']['path'] != 'translation': continue
            sm = a['samplers'][ch['sampler']]
            at, count = span(sm['output']); t_at, _ = span(sm['input'])
            pos = np.frombuffer(bytes(rest[at:at + count * 12]), '<f4').reshape(count, 3).astype(np.float64)
            times = np.frombuffer(bytes(rest[t_at:t_at + count * 4]), '<f4')
            up = int(np.argmax(np.abs(pos).mean(axis=0)))                     # the axis the hips' height is on
            width = max(3, int(1.2 / max(1e-3, float(np.median(np.diff(times)))) if count > 1 else 3) | 1)
            for axis in range(3):
                if axis == up or count < 3: continue
                if name == 'Stagger':
                    # He is meant to give ground, but Meshy's clip backs off 1.6 m and the game then snaps him home.
                    pos[:, axis] = pos[0, axis] + (pos[:, axis] - pos[0, axis]) * 0.25
                    continue
                padded = np.pad(pos[:, axis], width // 2, mode='edge')
                slow = np.convolve(padded, np.ones(width) / width, 'valid')
                pos[:, axis] = pos[:, axis] - slow + pos[0, axis]
            rest[at:at + count * 12] = pos.astype('<f4').tobytes()
            for key in ('min', 'max'): j['accessors'][sm['output']].pop(key, None)
    js = json.dumps(j, separators=(',', ':')).encode(); js += b' ' * ((4 - len(js) % 4) % 4)
    open(path, 'wb').write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + len(rest)) + struct.pack('<I4s', len(js), b'JSON') + js + bytes(rest))


def build():
    for fighter, table in MOVES.items():
        keys = [k for k, a in table.items() if os.path.exists(clip_path(a))]
        if not keys: continue
        out = os.path.join(MODELS, fighter, 'extra'); os.makedirs(out, exist_ok=True)
        target = os.path.join(MODELS, fighter, 'moves-smooth', 'animation_glb.glb')
        subprocess.run([sys.executable, os.path.join(ROOT, 'art', 'tools', 'transplant_anim.py'), target, os.path.join(out, 'extra.glb')]
                       + [clip_path(table[k]) for k in keys], check=True, capture_output=True)
        rename(os.path.join(out, 'extra.glb'), keys)
        json.dump(keys, open(os.path.join(out, 'extra.json'), 'w'))
        print('%-22s %s' % (fighter, ' '.join(keys)))


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'quote'
    missing = [a for a in wanted() if not os.path.exists(clip_path(a))]
    print('%d clips wanted, %d to buy: %d credits' % (len(wanted()), len(missing), len(missing) * COST))
    if mode == 'run':
        ceiling = int(sys.argv[2])
        if len(missing) * COST > ceiling: sys.exit('over the ceiling of %d credits: nothing bought' % ceiling)
        buy(missing)
    if mode in ('run', 'build'): build()


if __name__ == '__main__':
    main()
