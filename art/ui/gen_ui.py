"""Makes the game's interface art with Dreamina (seedream 4.7, 2K).

    python3 art/ui/gen_ui.py quote [name ...]            # create the canvas nodes and print what they would cost; spends nothing
    python3 art/ui/gen_ui.py run <ceiling> [name ...]    # generate (spends credits, never more than <ceiling>) and download

No names: every picture that has no art/ui/raw/<name>.jpg yet. Everything except the title background is drawn on
flat green and cut out by art/ui/cut_ui.py, which also writes the PNGs the game imports.
"""
import json, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, 'raw')
NODES = os.path.join(HERE, 'ui_nodes.json')
CANVAS = 'KO-SEKAI UI'

CHROME = ("Video game user-interface art for a 1987 arcade boxing game, glossy airbrushed 1980s style: polished chrome with blue-to-pink horizon "
          "reflections, hot magenta, electric cyan and sunset yellow on deep purple, bold clean shapes, crisp hard edges, thick dark outline, "
          "like the painted marquee of an arcade cabinet. ")
LETTERS = ("Flat 2D video game logo lettering from 1987, glossy airbrushed 1980s style: polished chrome with blue-to-pink horizon reflections, hot magenta, "
           "electric cyan and sunset yellow, bold clean shapes, crisp hard edges, thick dark purple outline. ")
COMIC = ("1980s comic-book sound-effect art: thick black ink outlines, flat bright colours, halftone dot shading, hard white highlights, "
         "dynamic slanted hand-lettering, like a panel from a printed vintage action comic. ")
KEY = (" The object is alone, centred, seen straight on, and fills most of the picture. Everything around it is a flat solid pure green #00FF00 "
       "background with no gradient. No green anywhere on the object, no outer glow, no shadow, no floor, no sparks or loose bits around it, "
       "no other objects, no watermark.")
NOTEXT = " No text, no letters, no numbers anywhere."


def word(text, look):
    return (COMIC + f"A single comic sound-effect badge with the lettering \"{text}\" spelled exactly like that, in huge fat capital letters, {look}, "
            "the letters sitting on a spiky explosion burst shape with a thick black outline. Only this lettering, no other text." + KEY)


def title(text, look):
    return (LETTERS + f"A single flat 2D game logo: the words \"{text}\" spelled exactly like that, in huge heavy italic capital letters, {look}. "
            "Only these letters with their thick dark outline: no plate, no sign, no screen, no cabinet, no machine and no frame behind or around them, "
            "no other text." + KEY)


# name: (ratio, prompt)
ART = {
    'title_bg': ('16:9', CHROME + "Background painting for the start menu, no text at all. Night. A boxing ring seen from a low angle in the lower third, "
                 "its ropes glowing neon magenta and cyan, standing on an endless glowing magenta grid floor that runs to the horizon. Behind it a huge "
                 "striped retro sunset sun, yellow at the top to hot pink at the bottom, with dark palm trees and a far city skyline in silhouette. "
                 "Deep purple starry sky with a few laser beams. The upper half of the picture is calm dark sky, left empty for a logo. "
                 "No people, no letters, no numbers, no logo, no watermark."),
    'logo': ('16:9', title('KO-SEKAI', "polished chrome letters with a sunset reflection and a hot magenta rim, a yellow lightning bolt striking through the hyphen, "
                           "a small red boxing glove punching out from behind the last letter")),
    'hpbar': ('21:9', CHROME + "One health bar frame for a fighting game: a very long, thin, perfectly straight horizontal chrome frame with a magenta neon "
              "inner rim, sharp rectangular ends, a small chrome lightning-bolt emblem on its left end. The long window inside the frame is completely empty and "
              "filled with the same flat pure green as the background, so the frame is hollow." + NOTEXT + KEY),
    'timer': ('1:1', LETTERS + "One flat 2D interface badge, drawn straight on like a sticker: a wide octagon with a thick polished chrome rim and a thin "
              "magenta neon line inside the rim, filled with one large empty flat dark purple area. It is only a flat badge: not a machine, not a cabinet, "
              "no buttons, no joystick, no stand, no wings." + NOTEXT + KEY),
    'panel': ('16:9', CHROME + "One empty rectangular menu panel: a flat dark navy-purple rectangle with a faint perspective grid inside, framed by a "
              "thick chrome border with a cyan neon inner line, rounded bolted chrome corners. Completely empty inside." + NOTEXT + KEY),
    'banner': ('21:9', CHROME + "One empty name plate: a long horizontal plate with slanted ends like a parallelogram, hot magenta to purple "
               "gradient face with faint horizontal speed lines, a thick chrome border and a yellow stripe along the bottom edge. Completely empty face." + NOTEXT + KEY),
    'vs': ('1:1', COMIC + "A single emblem with the letters \"VS\" in huge fat yellow-to-red italic capitals with a chrome edge, on a jagged cyan and magenta "
           "lightning explosion burst with a thick black outline. Only these two letters, no other text." + KEY),
    'glove': ('1:1', "A clean flat vector sticker illustration of exactly one shiny red boxing glove and nothing else: seen from the side, the fist "
              "pointing to the right, a white cuff on the left, thick black outline, two simple white highlights, flat cel shading. "
              "No lettering, no words, no speech marks, no burst, no stars, no motion lines, no impact marks, no shadow." + NOTEXT + KEY),
    'bubble': ('16:9', COMIC + "One empty shouting speech bubble: a wide jagged spiky balloon with a flat pure white inside, a very thick black outline, "
               "a thin yellow second outline outside the black one. From the bottom left of the balloon hangs one long, large, curved pointed speech tail, "
               "like the tail of a comic speech balloon, reaching down and to the left well below the balloon, white inside with the same black outline. "
               "Completely empty inside." + NOTEXT + KEY),
    'burst_hit': ('1:1', COMIC + "One empty comic explosion burst: a spiky star-shaped bang with a flat bright yellow middle, orange halftone dots towards "
                  "the spikes, red spike tips and a thick black outline. Completely empty middle." + NOTEXT + KEY),
    'burst_hurt': ('1:1', COMIC + "One empty comic explosion burst: a spiky star-shaped bang with a flat white middle, hot magenta halftone dots towards "
                   "the spikes, dark red spike tips and a thick black outline. Completely empty middle." + NOTEXT + KEY),
    'w_counter': ('16:9', word('COUNTER!', 'yellow letters with red drop shadow on a red and orange burst')),
    'w_dodge': ('16:9', word('DODGE!', 'white letters with a cyan drop shadow on a cyan and blue burst with speed lines')),
    'w_blocked': ('16:9', word('BLOCKED!', 'white letters with a purple drop shadow on a steel grey and lilac shield-shaped burst')),
    'w_fake': ('16:9', word('FAKE!', 'orange letters with a black drop shadow on a purple and pink burst')),
    'w_guardbreak': ('16:9', word('GUARD BREAK!', 'red letters with a yellow edge, the words stacked on two lines, on a cracked white and yellow burst')),
    'w_pow': ('1:1', word('POW!', 'red letters with a yellow edge on a yellow and orange burst')),
    'w_bam': ('1:1', word('BAM!', 'yellow letters with a red edge on a magenta and purple burst')),
    'w_wham': ('1:1', word('WHAM!', 'white letters with a blue edge on a cyan and yellow burst')),
    'w_krak': ('1:1', word('KRAK!', 'cyan letters with a dark blue edge on a red and orange cracked burst')),
    't_fight': ('16:9', title('FIGHT!', 'yellow-to-red flame gradient letters with a chrome edge, slanted speed streaks trailing off the letters to the left')),
    't_ko': ('16:9', title('K.O.!', 'giant red-to-yellow gradient letters with a chrome edge and cracks running through them')),
    't_youwin': ('16:9', title('YOU WIN', 'gold and chrome letters with a sunset reflection, a small star at each end')),
    't_youlose': ('16:9', title('YOU LOSE', 'cold blue and purple chrome letters, slightly cracked')),
    't_knockedout': ('16:9', title('KNOCKED OUT', 'dark red chrome letters with a magenta rim, the two words stacked on two lines, cracked')),
    't_time': ('16:9', title('TIME!', 'cyan chrome letters with a yellow rim and a small stopwatch replacing the dot of the exclamation mark')),
    't_down': ('16:9', title('DOWN!', 'orange-to-magenta gradient letters with a chrome edge, tilted as if falling')),
    't_continue': ('16:9', title('CONTINUE?', 'yellow chrome letters with a magenta rim')),
    't_paused': ('16:9', title('PAUSED', 'lilac and cyan chrome letters, calm and level')),
}


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
                '--resolution', '2K', '--title', 'UI ' + n, '--prompt', prompt)
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
