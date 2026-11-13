"""Cuts the Dreamina interface pictures (art/ui/raw/<name>.jpg, drawn on flat green) into game textures.

    art/tracker-model/venv/bin/python art/ui/cut_ui.py [name ...]      # no names: every picture (needs OpenCV)

Writes art/ui/out/T_UI_<name>.png (green removed, cropped to the drawing, shrunk to the size the game needs), a
contact sheet on a dark and a light ground (art/ui/out/contact.jpg) to look the cut-outs over, and
unreal/Source/KoSekai/KSUIData.h: where the hollow of the health bar, the screen of the timer, the inside of the
panel and the writing space of the speech bubble are (rebuild after it changes). Import with art/ui/import_ui.py.
"""
import glob, os, sys
import numpy as np
from PIL import Image
import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'out')
HEADER = os.path.join(HERE, '..', '..', 'unreal', 'Source', 'KoSekai', 'KSUIData.h')
# Width in pixels of the finished texture. The game never shows them larger than this on a 1080p screen.
WIDTH = {'title_bg': 1920, 'logo': 1400, 'hpbar': 1024, 'timer': 400, 'panel': 1024, 'banner': 900, 'vs': 512, 'glove': 256,
         'bubble': 900, 'burst_hit': 512, 'burst_hurt': 512}
DEFAULT = {'w_': 640, 't_': 1100}
HOLLOW = {'hpbar'}          # green inside the drawing is background too


def background(rgb, hollow):
    """Alpha 0..1: the green backdrop (and its darker shadows) removed, with a soft edge."""
    f = rgb.astype(np.float32)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    k = (g - np.maximum(r, b)) / (g + 1.0)            # how much greener than anything else: 0.5-0.8 on the backdrop
    green = (k > 0.28) & (g > 35)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(green.astype(np.uint8), connectivity=4)
    edge = np.unique(np.concatenate([labels[0], labels[-1], labels[:, 0], labels[:, -1]]))
    is_bg = np.zeros(count, bool); is_bg[edge] = True
    # Green shut inside the drawing is backdrop too: the hollow of the health bar, the inside of an O.
    is_bg |= stats[:, cv2.CC_STAT_AREA] > (0.004 if hollow else 0.0003) * green.size
    is_bg[0] = False
    bg = is_bg[labels]
    # The soft shadow some pictures have under the object: dull green-teal, touching the backdrop.
    murky = (g > r * 1.15) & (g > b * 1.04) & (k > 0.03) & cv2.dilate(bg.astype(np.uint8), np.ones((61, 61), np.uint8)).astype(bool)
    count, labels = cv2.connectedComponents((bg | murky).astype(np.uint8), connectivity=4)
    touched = np.zeros(count, bool); touched[np.unique(labels[bg])] = True; touched[0] = False
    bg = touched[labels]
    near = cv2.dilate(bg.astype(np.uint8), np.ones((7, 7), np.uint8)).astype(bool)
    soft = 1.0 - np.clip((k - 0.08) / 0.22, 0.0, 1.0)
    return np.where(bg, 0.0, np.where(near, soft, 1.0)).astype(np.float32), near


def cut(name):
    rgb = np.array(Image.open(f'{HERE}/raw/{name}.jpg').convert('RGB'))
    width = WIDTH.get(name) or DEFAULT[name[:2]]
    if name == 'title_bg':
        return np.dstack([np.array(Image.fromarray(rgb).resize((width, width * 9 // 16), Image.LANCZOS)), np.full((width * 9 // 16, width), 255, np.uint8)])
    alpha, near = background(rgb, name in HOLLOW)
    # Loose specks away from the drawing go; an exclamation mark's dot or a spark that is part of it stays.
    count, labels, stats, _ = cv2.connectedComponentsWithStats((alpha > 0.5).astype(np.uint8), connectivity=8)
    sizes = stats[:, cv2.CC_STAT_AREA].astype(np.float64); sizes[0] = 0
    keep = (sizes >= 0.004 * sizes.max())[labels]
    alpha = alpha * cv2.dilate(keep.astype(np.uint8), np.ones((5, 5), np.uint8))
    # Green that leaked onto the edge pixels.
    c = rgb.astype(np.float32)
    limit = np.maximum(c[..., 0], c[..., 2]) * 1.02
    c[..., 1] = np.where(near, np.minimum(c[..., 1], limit), c[..., 1])
    yy, xx = np.where(alpha > 0.5)
    y0, y1, x0, x1 = max(0, yy.min() - 6), yy.max() + 7, max(0, xx.min() - 6), xx.max() + 7
    c, alpha = c[y0:y1, x0:x1], alpha[y0:y1, x0:x1]
    # Shrink with the colour weighted by its alpha, so the transparent surround does not darken the edges.
    h = int(round(width * c.shape[0] / c.shape[1] / 4)) * 4
    pre = cv2.resize(c * alpha[..., None], (width, h), interpolation=cv2.INTER_AREA)
    a = cv2.resize(alpha, (width, h), interpolation=cv2.INTER_AREA)
    c = pre / np.maximum(a[..., None], 1e-3)
    # Carry the edge colour out into the transparent pixels: the game filters textures, and black there would fringe.
    solid = a > 0.02
    for _ in range(12):
        grown = cv2.dilate(solid.astype(np.uint8), np.ones((3, 3), np.uint8)).astype(bool)
        spread = cv2.blur(c * solid[..., None], (3, 3)) / np.maximum(cv2.blur(solid.astype(np.float32), (3, 3))[..., None], 1e-3)
        c = np.where((grown & ~solid)[..., None], spread, c); solid = grown
    c[~solid] = 0
    if name == 'timer':
        # Dreamina wrote a year on the badge. The clock goes there: paint the inside of the rim flat.
        k = int(width * 0.11) * 2 + 1
        inside = cv2.erode((a > 0.9).astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k, k))).astype(bool)
        shade = np.linspace(1.25, 0.8, h)[:, None, None]
        c = np.where(inside[..., None], np.array([34.0, 14.0, 66.0]) * shade, c)
    return np.dstack([np.clip(c, 0, 255), a * 255]).astype(np.uint8)


def inner(mask, shrink_to=0.97):
    """A rectangle (x0, y0, x1, y1 as fractions) that lies inside a blob: its bounding box, pulled in until it is nearly all blob."""
    count, labels, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), connectivity=4)
    if count < 2: return (0.1, 0.1, 0.9, 0.9)
    h, w = mask.shape
    best = labels[h // 2, w // 2] or 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    blob = labels == best
    x0, y0 = stats[best, cv2.CC_STAT_LEFT], stats[best, cv2.CC_STAT_TOP]
    x1, y1 = x0 + stats[best, cv2.CC_STAT_WIDTH], y0 + stats[best, cv2.CC_STAT_HEIGHT]
    step = max(1, w // 300)
    while blob[y0:y1, x0:x1].mean() < shrink_to and x1 - x0 > 8 * step and y1 - y0 > 8 * step:
        strips = {'l': blob[y0:y1, x0:x0 + step].mean(), 'r': blob[y0:y1, x1 - step:x1].mean(), 't': blob[y0:y0 + step, x0:x1].mean(), 'b': blob[y1 - step:y1, x0:x1].mean()}
        side = min(strips, key=strips.get)
        if side == 'l': x0 += step
        elif side == 'r': x1 -= step
        elif side == 't': y0 += step
        else: y1 -= step
    return (x0 / w, y0 / h, x1 / w, y1 / h)


def measure():
    """The places inside four of the pictures that the game draws into."""
    def load(n): return np.array(Image.open(f'{OUT}/T_UI_{n}.png')).astype(np.int32)
    out = {}
    p = load('hpbar'); out['HpHole'] = inner(p[..., 3] < 60, 0.985)
    for key, n in (('TimerScreen', 'timer'), ('PanelInside', 'panel')):
        p = load(n); out[key] = inner((p[..., 3] > 200) & (p[..., :3].max(axis=2) < 125) & (p[..., 2] >= p[..., 1]), 0.93)
    p = load('bubble'); out['BubbleInside'] = inner((p[..., 3] > 200) & (p[..., :3].min(axis=2) > 205), 0.985)
    lines = ['// Where the game draws into the interface pictures: left, top, right, bottom as fractions of the picture.',
             '// Written by art/ui/cut_ui.py from the cut-out art; do not edit by hand.', '#pragma once', '', 'namespace KSUI', '{']
    for key, box in out.items():
        lines.append('\tconst float %s[4] = { %.4ff, %.4ff, %.4ff, %.4ff };' % ((key,) + tuple(box)))
        print(key, ['%.3f' % v for v in box])
    lines += ['}', '']
    text = '\n'.join(lines)
    if not os.path.exists(HEADER) or open(HEADER).read() != text: open(HEADER, 'w').write(text)


def contact(names):
    cells = []
    for n in names:
        p = Image.open(f'{OUT}/T_UI_{n}.png').convert('RGBA'); p.thumbnail((300, 190))
        for ground in ((28, 10, 48), (235, 235, 235)):
            cell = Image.new('RGBA', (310, 200), ground + (255,)); cell.alpha_composite(p, ((310 - p.width) // 2, (200 - p.height) // 2)); cells.append(cell)
    per = 6
    sheet = Image.new('RGB', (per * 310, (len(cells) + per - 1) // per * 200), 'black')
    for i, cell in enumerate(cells): sheet.paste(cell.convert('RGB'), ((i % per) * 310, (i // per) * 200))
    sheet.save(f'{OUT}/contact.jpg', quality=88)


def main():
    os.makedirs(OUT, exist_ok=True)
    every = sorted(os.path.splitext(os.path.basename(f))[0] for f in glob.glob(f'{HERE}/raw/*.jpg'))
    for n in sys.argv[1:] or every:
        p = cut(n)
        Image.fromarray(p, 'RGBA').save(f'{OUT}/T_UI_{n}.png')
        print('%-14s %4d x %4d' % (n, p.shape[1], p.shape[0]))
    measure()
    contact(every)


if __name__ == '__main__':
    main()
