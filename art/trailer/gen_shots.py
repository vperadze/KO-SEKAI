"""Three extra shots for the trailer, made with Dreamina video in the look of the opening film (whose code this
uses: art/cinematic/gen_cinematic.py).

    python3 art/trailer/gen_shots.py quote [shot ...]            # create the canvas nodes, print the cost; spends nothing
    python3 art/trailer/gen_shots.py run <ceiling> [shot ...]    # generate (never more than <ceiling> credits), download

Clips land in art/trailer/dreamina/<shot>.mp4. The canvas is "KO-SEKAI trailer", which also holds the trailer, the stills and the material it was cut from.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'cinematic'))
import gen_cinematic as g

g.RAW = os.path.join(HERE, 'dreamina')
g.NODES = os.path.join(HERE, 'shot_nodes.json')
g.CANVAS = 'KO-SEKAI trailer'
g.SHOTS = {   # name: (seconds, what happens). No harm to people in the wording: the service refuses it (docs/LESSONS.md).
    'tape':   (4, "Extreme close-up in a dark wood-panelled living room, 1987. A hand pushes a black video cassette with a blank "
                  "label into a chunky silver video recorder; the machine swallows it with a jolt, its green display lights up, and "
                  "the big television above bursts into static and then blinding magenta and cyan light that floods the room. "
                  "The camera pushes fast into the glowing screen."),
    'webcam': (5, "A teenager's bedroom at night in 1987, lit only by a huge glowing television. A muscular young man in a red "
                  "headband and a sleeveless denim jacket stands in front of the television and throws fast boxing punches at the "
                  "air, grinning. A small video camera sits on top of the television, pointed at him, its red light on. On the "
                  "screen a cartoon boxer in a neon ring throws exactly the same punches at the same instant. Sparks fly out of the "
                  "screen with every punch. The camera circles him quickly, low to the floor."),
    'flex':   (5, "Seven video game fighters stand in a row on a chrome stage and flex their muscles like bodybuilders, striking "
                  "one proud pose after another: a huge soldier with a blond mullet and a red headband, a muscular knight with a "
                  "white unicorn's head, a woman in a neon aerobics leotard with enormous hair, a tiny furious grandmother in boxing "
                  "gloves, a chrome robot, a pale vampire with a towering black hairdo, and a man with a television set for a head. "
                  "Fireworks and columns of flame go off behind them, laser beams sweep the smoke, a crowd's hands wave in the "
                  "foreground. The camera cranes up from the floor and pushes in."),
}

if __name__ == '__main__':
    g.main()
