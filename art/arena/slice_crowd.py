"""Cuts the six-pose Dreamina sheets (crowd_<name>.jpg, 3x2 on green) into game textures.

    art/tracker-model/venv/bin/python art/arena/slice_crowd.py [name ...]     # no names: every sheet (needs OpenCV)

Writes T_KS_Crowd_<name>.png: a 4x2 grid of cells, poses 0-5 (idle, cheer, wince, shout, fist pump, boo), the figure
standing on the bottom edge of its cell, all six at one scale. Cells are 256x512 for 'tall' characters and 512x512 for
'wide' ones. Also rewrites unreal/Source/KoSekai/KSCrowdData.h, the list of spectators the game seats, and a contact
sheet (crowd_contact.jpg) to look the result over. Import with art/tools/import_crowd.py.
"""
import os, sys
import numpy as np
from PIL import Image
import cv2
from crowd_roster import NEW, OLD_BUILD

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, '..', '..', 'unreal', 'Source', 'KoSekai', 'KSCrowdData.h')


def background(rgb):
    """True where the pixel is the green backdrop or the darker green blob drawn under the feet."""
    f = rgb.astype(np.float32)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    # The backdrop's own colour, from the frame of the picture; a pixel belongs to it by hue, not brightness.
    edge = np.concatenate([f[:12].reshape(-1, 3), f[-12:].reshape(-1, 3), f[:, :12].reshape(-1, 3), f[:, -12:].reshape(-1, 3)])
    ref = np.median(edge, axis=0); ref = ref / ref.sum()
    total = r + g + b + 1.0
    near = np.abs(r / total - ref[0]) + np.abs(g / total - ref[1]) + np.abs(b / total - ref[2])
    greenish = (g > 60) & (g > r * 1.25) & (g > b * 1.25)
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
        # A big separate piece is part of the drawing (a flame, a lamp); a small one only if it sits within the body's box.
        if sizes[i] > 0.04 * sizes[main] or (inside and sizes[i] > 0.015 * sizes[main]): keep |= labels == i
    return keep


def cut(sums, at, span):
    """The emptiest line near a nominal grid line."""
    lo, hi = int(at - span), int(at + span)
    return lo + int(np.argmin(sums[lo:hi]))


def slice_sheet(name, wide):
    rgb = np.array(Image.open(f'{HERE}/crowd_{name}.jpg').convert('RGB'))
    h, w = rgb.shape[:2]
    alpha = ~background(rgb)
    alpha = cv2.morphologyEx(alpha.astype(np.uint8), cv2.MORPH_OPEN, np.ones((3, 3), np.uint8)).astype(bool)
    cols, rows = alpha.sum(axis=0), alpha.sum(axis=1)
    xs = [0, cut(cols, w / 3, w * 0.07), cut(cols, 2 * w / 3, w * 0.07), w]
    ys = [0, cut(rows, h / 2, h * 0.08), h]
    figures = []
    for row in range(2):
        for col in range(3):
            a = figure(alpha[ys[row]:ys[row + 1], xs[col]:xs[col + 1]])
            c = rgb[ys[row]:ys[row + 1], xs[col]:xs[col + 1]]
            yy, xx = np.where(a)
            if len(yy) < 500: return None
            a, c = a[yy.min():yy.max() + 1, xx.min():xx.max() + 1], c[yy.min():yy.max() + 1, xx.min():xx.max() + 1].copy()
            # Green that leaked onto the outline pixels.
            spill = a & (c[..., 1] > c[..., 0] * 1.3) & (c[..., 1] > c[..., 2] * 1.3) & ~cv2.erode(a.astype(np.uint8), np.ones((3, 3), np.uint8), iterations=3).astype(bool)
            c[spill, 1] = np.maximum(c[spill, 0], c[spill, 2])
            c[~a] = 0
            figures.append(Image.fromarray(np.dstack([c, a.astype(np.uint8) * 255]), 'RGBA'))
    cw, ch = (512, 512) if wide else (256, 512)
    scale = min(min((cw - 4) / f.width, (ch - 2) / f.height) for f in figures)
    sheet = Image.new('RGBA', (cw * 4, ch * 2), (0, 0, 0, 0))
    for i, f in enumerate(figures):
        f = f.resize((max(1, round(f.width * scale)), max(1, round(f.height * scale))), Image.LANCZOS)
        px = np.array(f); px[..., 3] = np.where(px[..., 3] > 110, 255, 0); f = Image.fromarray(px, 'RGBA')
        sheet.paste(f, ((i % 4) * cw + (cw - f.width) // 2, (i // 4) * ch + ch - f.height))
    sheet.save(f'{HERE}/T_KS_Crowd_{name}.png')
    return sheet


def main():
    info = {n: (v[1] == 'wide', v[2]) for n, v in NEW.items()}
    info.update({n: (False, b) for n, b in OLD_BUILD.items()})
    names = sys.argv[1:] or [n for n in info if os.path.exists(f'{HERE}/crowd_{n}.jpg')]
    for n in names:
        print(n, 'ok' if slice_sheet(n, info[n][0]) else 'FAILED: a pose is missing', flush=True)
    done = [n for n in info if os.path.exists(f'{HERE}/T_KS_Crowd_{n}.png')]
    with open(HEADER, 'w') as f:
        f.write('// Generated by art/arena/slice_crowd.py from art/arena/crowd_roster.py. Do not edit by hand.\n#pragma once\n\n')
        f.write('enum class EKSBuild : uint8 { Tiny, Small, Normal, Tall, Giant, Flyer };\n')
        f.write('// A spectator sheet /Game/KS/Arena/T_KS_Crowd_<Name>: 4x2 cells, poses idle, cheer, wince, shout, fist pump, boo.\n')
        f.write('struct FKSCrowdSheet { const TCHAR* Name; bool bWide; EKSBuild Build; };\n')
        f.write('static const FKSCrowdSheet KSCrowdSheets[] = {\n')
        for n in done:
            f.write('\t{ TEXT("%s"), %s, EKSBuild::%s },\n' % (n, 'true' if info[n][0] else 'false', info[n][1].capitalize()))
        f.write('};\n')
    # Contact sheets on grey (the six used cells of each), to check keying and poses by eye.
    tw, th, per = 960, 300, 2
    for page in range(0, len(done), 20):
        part = done[page:page + 20]
        out = Image.new('RGB', (per * tw, ((len(part) + per - 1) // per) * th), (70, 70, 90))
        for i, n in enumerate(part):
            t = Image.open(f'{HERE}/T_KS_Crowd_{n}.png')
            cw, ch = t.width // 4, t.height // 2
            for k in range(6):
                cell = t.crop(((k % 4) * cw, (k // 4) * ch, (k % 4 + 1) * cw, (k // 4 + 1) * ch))
                cell.thumbnail((tw // 6, th))
                out.paste(cell, ((i % per) * tw + k * (tw // 6) + (tw // 6 - cell.width) // 2, (i // per) * th + th - cell.height), cell)
        out.save(f'{HERE}/crowd_contact_{page // 20 + 1}.jpg', quality=88)
    print(len(done), 'sheets in', os.path.normpath(HEADER))


if __name__ == '__main__':
    main()
