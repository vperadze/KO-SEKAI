"""Cuts the eight-pose Dreamina sheets (real_<name>.jpg from gen_crowd_real.py, 4x2 on green) into game textures.

    art/tracker-model/venv/bin/python art/arena/slice_crowd.py [name ...]     # no names: every sheet (needs OpenCV)

Writes T_KS_Crowd_<name>.png: a 4x2 grid of cells, poses 0-7 (idle, cheer, wince, shout, fist pump, boo, clap, dance),
the figure standing on the bottom edge of its cell, all eight at one scale. Cells are 256x512 for 'tall' characters and
512x512 for 'wide' ones. Also rewrites unreal/Source/KoSekai/KSCrowdData.h, the list of spectators the game seats (only
those with a realistic sheet: the old pixel sheets crowd_<name>.jpg are not used any more), and contact sheets
(crowd_contact_<n>.jpg) to look the result over. Import with art/tools/import_crowd.py.
"""
import os, sys
import numpy as np
from PIL import Image
import cv2
from crowd_roster import NEW, OLD_BUILD, MORE

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, '..', '..', 'unreal', 'Source', 'KoSekai', 'KSCrowdData.h')


LOOSE = {'phoenix'}      # sheets whose floor came out yellow-green: a wider idea of "green" for these only


def background(rgb, loose=False):
    """True where the pixel is the green backdrop or the darker green blob drawn under the feet."""
    f = rgb.astype(np.float32)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    # The backdrop's own colour, from the frame of the picture; a pixel belongs to it by hue, not brightness.
    edge = np.concatenate([f[:12].reshape(-1, 3), f[-12:].reshape(-1, 3), f[:, :12].reshape(-1, 3), f[:, -12:].reshape(-1, 3)])
    ref = np.median(edge, axis=0); ref = ref / ref.sum()
    total = r + g + b + 1.0
    near = np.abs(r / total - ref[0]) + np.abs(g / total - ref[1]) + np.abs(b / total - ref[2])
    greenish = (g > 60) & (g > r * (1.06 if loose else 1.25)) & (g > b * 1.25)
    strict = greenish & (near < 0.12)
    # The shadow blob is a different green, but it touches the backdrop; green inside a character's outline does not.
    count, labels = cv2.connectedComponents(greenish.astype(np.uint8), connectivity=4)
    touched = np.zeros(count, bool); touched[np.unique(labels[strict])] = True; touched[0] = False
    return touched[labels]


def figure(alpha):
    """Keeps the character and drops the loose bits around it: shock lines, sparkles far from the body."""
    count, labels, stats, _ = cv2.connectedComponentsWithStats(alpha.astype(np.uint8), connectivity=8)
    if count < 2: return alpha
    sizes = stats[:, cv2.CC_STAT_AREA].copy(); sizes[0] = 0
    main = int(np.argmax(sizes))
    keep = labels == main
    x0, y0 = stats[main, cv2.CC_STAT_LEFT], stats[main, cv2.CC_STAT_TOP]
    x1, y1 = x0 + stats[main, cv2.CC_STAT_WIDTH], y0 + stats[main, cv2.CC_STAT_HEIGHT]
    for i in range(1, count):
        if i == main or sizes[i] < 40: continue
        sx, sy = stats[i, cv2.CC_STAT_LEFT], stats[i, cv2.CC_STAT_TOP]
        inside = sy >= y0 and sy + stats[i, cv2.CC_STAT_HEIGHT] <= y1 and sx >= x0 and sx + stats[i, cv2.CC_STAT_WIDTH] <= x1
        # A flat bar wholly under the feet or above the head is a stand, a strip of floor, or the rim of the neighbour in
        # the other row. (Only flat ones: legs under a keyed-out green skirt are a loose piece below the body too.)
        sw, sh = stats[i, cv2.CC_STAT_WIDTH], stats[i, cv2.CC_STAT_HEIGHT]
        if (sy >= y1 - 2 or sy + sh <= y0) and sw > 3 * sh: continue
        # A big separate piece is part of the drawing (a flame, a lamp); a small one only if it sits within the body's box.
        if sizes[i] > 0.04 * sizes[main] or (inside and sizes[i] > 0.015 * sizes[main]): keep |= labels == i
    return keep


def cut(sums, at, span):
    """The emptiest line near a nominal grid line."""
    lo, hi = int(at - span), int(at + span)
    return lo + int(np.argmin(sums[lo:hi]))


def bleed(c, a, rounds=6):
    """Spreads the figure's edge colours into the cut-away pixels, so smooth texture filtering does not darken the rim."""
    c, known = c.copy(), a.copy()
    k = np.ones((3, 3), np.uint8)
    for _ in range(rounds):
        grown = cv2.dilate(known.astype(np.uint8), k).astype(bool)
        blur = cv2.blur(np.where(known[..., None], c, 0).astype(np.float32), (3, 3)) / np.maximum(cv2.blur(known.astype(np.float32), (3, 3)), 1e-3)[..., None]
        new = grown & ~known
        c[new] = np.clip(blur[new], 0, 255).astype(np.uint8)
        known = grown
    c[~known] = 0
    return c


def slice_sheet(name, wide):
    rgb = np.array(Image.open(f'{HERE}/real_{name}.jpg').convert('RGB'))
    h, w = rgb.shape[:2]
    alpha = ~background(rgb, name in LOOSE)
    alpha = cv2.morphologyEx(alpha.astype(np.uint8), cv2.MORPH_OPEN, np.ones((5, 5), np.uint8)).astype(bool)
    cols, rows = alpha.sum(axis=0), alpha.sum(axis=1)
    xs = [0] + [cut(cols, k * w / 4, w * 0.06) for k in (1, 2, 3)] + [w]
    ys = [0, cut(rows, h / 2, h * 0.08), h]
    figures = []
    for row in range(2):
        for col in range(4):
            a = figure(alpha[ys[row]:ys[row + 1], xs[col]:xs[col + 1]])
            c = rgb[ys[row]:ys[row + 1], xs[col]:xs[col + 1]]
            yy, xx = np.where(a)
            if len(yy) < 2000: return None
            # A figure that runs into the edge of its cell was cut off by the picture or shares a blob with its neighbour.
            if xx.min() == 0 or xx.max() == a.shape[1] - 1: print('  ', name, 'pose', row * 4 + col, 'touches the side of its cell')
            if row == 1 and yy.max() >= a.shape[0] - 2: print('  ', name, 'pose', row * 4 + col, 'runs off the bottom: feet cut off?')
            a, c = a[yy.min():yy.max() + 1, xx.min():xx.max() + 1], c[yy.min():yy.max() + 1, xx.min():xx.max() + 1].copy()
            # Green that leaked onto the outline pixels.
            rim = a & ~cv2.erode(a.astype(np.uint8), np.ones((3, 3), np.uint8), iterations=4).astype(bool)
            spill = rim & (c[..., 1] > c[..., 0] * 1.15) & (c[..., 1] > c[..., 2] * 1.15)
            c[spill, 1] = np.maximum(c[spill, 0], c[spill, 2])
            c = bleed(c, a)
            figures.append(Image.fromarray(np.dstack([c, a.astype(np.uint8) * 255]), 'RGBA'))
    cw, ch = (512, 512) if wide else (256, 512)
    scale = min(min((cw - 4) / f.width, (ch - 2) / f.height) for f in figures)
    sheet = Image.new('RGBA', (cw * 4, ch * 2), (0, 0, 0, 0))
    for i, f in enumerate(figures):
        f = f.resize((max(1, round(f.width * scale)), max(1, round(f.height * scale))), Image.LANCZOS)
        px = np.array(f); hard = px[..., 3] > 128
        px[..., :3] = bleed(px[..., :3], hard, 3); px[..., 3] = np.where(hard, 255, 0); f = Image.fromarray(px, 'RGBA')
        box = ((i % 4) * cw + (cw - f.width) // 2, (i // 4) * ch + ch - f.height)
        sheet.paste(f, box)
    sheet.save(f'{HERE}/T_KS_Crowd_{name}.png')
    return sheet


def main():
    info = {n: (v[1] == 'wide', v[2]) for n, v in list(NEW.items()) + list(MORE.items())}
    info.update({n: (False, b) for n, b in OLD_BUILD.items()})
    names = sys.argv[1:] or [n for n in info if os.path.exists(f'{HERE}/real_{n}.jpg')]
    for n in names:
        print(n, 'ok' if slice_sheet(n, info[n][0]) else 'FAILED: a pose is missing', flush=True)
    # Only the realistic ones are seated: a leftover pixel sheet has six poses and would stand out among them.
    done = [n for n in info if os.path.exists(f'{HERE}/real_{n}.jpg') and os.path.exists(f'{HERE}/T_KS_Crowd_{n}.png')
            and os.path.getmtime(f'{HERE}/T_KS_Crowd_{n}.png') >= os.path.getmtime(f'{HERE}/real_{n}.jpg')]
    with open(HEADER, 'w') as f:
        f.write('// Generated by art/arena/slice_crowd.py from art/arena/crowd_roster.py. Do not edit by hand.\n#pragma once\n\n')
        f.write('enum class EKSBuild : uint8 { Tiny, Small, Normal, Tall, Giant, Flyer };\n')
        f.write('// A spectator sheet /Game/KS/Arena/T_KS_Crowd_<Name>: 4x2 cells, poses idle, cheer, wince, shout, fist pump, boo, clap, dance.\n')
        f.write('struct FKSCrowdSheet { const TCHAR* Name; bool bWide; EKSBuild Build; };\n')
        f.write('static const FKSCrowdSheet KSCrowdSheets[] = {\n')
        for n in done:
            f.write('\t{ TEXT("%s"), %s, EKSBuild::%s },\n' % (n, 'true' if info[n][0] else 'false', info[n][1].capitalize()))
        f.write('};\n')
    # Contact sheets on grey (the eight cells of each), to check keying and poses by eye.
    tw, th, per = 1280, 300, 2
    for page in range(0, len(done), 20):
        part = done[page:page + 20]
        out = Image.new('RGB', (per * tw, ((len(part) + per - 1) // per) * th), (70, 70, 90))
        for i, n in enumerate(part):
            t = Image.open(f'{HERE}/T_KS_Crowd_{n}.png')
            cw, ch = t.width // 4, t.height // 2
            for k in range(8):
                cell = t.crop(((k % 4) * cw, (k // 4) * ch, (k % 4 + 1) * cw, (k // 4 + 1) * ch))
                cell.thumbnail((tw // 8, th))
                out.paste(cell, ((i % per) * tw + k * (tw // 8) + (tw // 8 - cell.width) // 2, (i // per) * th + th - cell.height), cell)
        out.save(f'{HERE}/crowd_contact_{page // 20 + 1}.jpg', quality=88)
    print(len(done), 'sheets in', os.path.normpath(HEADER))


if __name__ == '__main__':
    main()
