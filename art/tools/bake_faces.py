#!/usr/bin/env python3
"""Paints redrawn faces back onto a fighter's texture: one full texture per expression.

    python3 art/tools/bake_faces.py front <fighter-dir>             head seen from the front -> <work>/<fighter>.png
    python3 art/tools/bake_faces.py bake  <fighter-dir> [expr ...]  <work>/<fighter>_<expr>.png -> faces/T_<Name>_Face_<expr>.png
    python3 art/tools/bake_faces.py led   turbotron talk blink      LED faces drawn by ledface.py, painted on the visor

<work> is $KS_FACE_WORK (default /tmp/ks-faces). The redrawn pictures are edits of the square, black-padded front
view (image-to-image, "change only the expression"), any size; they are scaled back onto the front view.
Only the face rectangle in FACE is painted, faded in at its edges, so hair and neck stay exactly as they were.
Output is 2048 px: the game caps fighter textures at 2K.
"""
import os, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facelib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
WORK = os.environ.get('KS_FACE_WORK', '/tmp/ks-faces')
# fighter dir -> (asset name, face rectangle in the front view as fractions: x0, y0, x1, y1)
FACE = {
    'sergeant-mullet-v2':   ('SergeantMullet',      (0.30, 0.40, 0.70, 0.92)),
    'sir-lasercorn':        ('SirLasercorn',        (0.14, 0.25, 0.84, 0.85)),
    'aerobics-annihilator': ('AerobicsAnnihilator', (0.33, 0.38, 0.66, 0.82)),
    'grandma-thunder':      ('GrandmaThunder',      (0.19, 0.35, 0.81, 0.92)),
    'count-hairspray':      ('CountHairspray',      (0.30, 0.40, 0.67, 0.92)),
    'turbotron':            ('Turbotron',           None),
}
SIZE = 1024
FEATHER = 0.08


def model(name):
    return facelib.Model(os.path.join(ROOT, 'art', 'models', name, 'moves-smooth', 'animation_glb.glb'))


def head_texels(M, view):
    """Every texel of the front of the head: where it is in the texture (xs, ys) and in the front view (u, v in 0-1)."""
    lo, hi, sc = view; th, tw = M.tex.shape[:2]
    W = (hi[0] - lo[0]) * sc; H = (hi[1] - lo[1]) * sc
    out = [[], [], [], []]
    for a, b, c in M.head_tris():
        q = [(M.UV[i][0] * tw, M.UV[i][1] * th) for i in (a, b, c)]
        r = facelib.bary_raster(q[0], q[1], q[2], tw, th)
        if r is None: continue
        xs, ys, wa, wb, wc = r
        X = wa * M.P[a][0] + wb * M.P[b][0] + wc * M.P[c][0]; Y = wa * M.P[a][1] + wb * M.P[b][1] + wc * M.P[c][1]
        out[0].append(xs); out[1].append(ys); out[2].append((X - lo[0]) * sc / W); out[3].append((hi[1] - Y) * sc / H)
    return [np.concatenate(o) for o in out]


def front(name):
    M = model(name); img, view = facelib.front_view(M, size=SIZE)
    h, w = img.shape[:2]; s = max(w, h)
    sq = Image.new('RGB', (s, s)); sq.paste(Image.fromarray(img), ((s - w) // 2, (s - h) // 2))
    os.makedirs(WORK, exist_ok=True); sq.save(os.path.join(WORK, name + '.png'))
    return M, view, (w, h)


def save(name, expr, tex):
    out = os.path.join(ROOT, 'art', 'models', name, 'faces', 'T_%s_Face_%s.png' % (FACE[name][0], expr))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    Image.fromarray(tex).resize((2048, 2048), Image.LANCZOS).save(out)
    print(os.path.relpath(out, ROOT))


def bake(name, exprs):
    M, view, (w, h) = front(name)
    xs, ys, u, v = head_texels(M, view)
    x0, y0, x1, y1 = FACE[name][1]
    s = (u - x0) / (x1 - x0); t = (v - y0) / (y1 - y0); ok = (s >= 0) & (s <= 1) & (t >= 0) & (t <= 1)
    xs, ys, u, v, s, t = xs[ok], ys[ok], u[ok], v[ok], s[ok], t[ok]
    alpha = np.clip(np.minimum(np.minimum(s, 1 - s), np.minimum(t, 1 - t)) / FEATHER, 0, 1)[:, None]
    side = max(w, h)
    for expr in exprs:
        pic = Image.open(os.path.join(WORK, '%s_%s.png' % (name, expr))).convert('RGB').resize((side, side), Image.LANCZOS)
        pic = np.asarray(pic.crop(((side - w) // 2, (side - h) // 2, (side - w) // 2 + w, (side - h) // 2 + h)), dtype=np.float64)
        px = np.clip((u * w).astype(int), 0, w - 1); py = np.clip((v * h).astype(int), 0, h - 1)
        tex = M.tex.copy()
        tex[ys, xs] = (tex[ys, xs] * (1 - alpha) + pic[py, px] * alpha).astype(np.uint8)
        save(name, expr, tex)


def led(name, exprs):
    """The visor is found from a face that is already painted: the texels that differ from the plain texture."""
    import ledface
    M, view, (w, h) = front(name)
    xs, ys, u, v = head_texels(M, view)
    old = np.asarray(Image.open(os.path.join(ROOT, 'art', 'models', name, 'faces', 'T_%s_Face_neutral.png' % FACE[name][0])).convert('RGB').resize(M.tex.shape[1::-1]), dtype=int)
    changed = np.abs(old[ys, xs] - M.tex[ys, xs].astype(int)).sum(1) > 60
    x0, x1 = np.percentile(u[changed], [0.5, 99.5]); y0, y1 = np.percentile(v[changed], [0.5, 99.5])
    print('visor', round(x0, 3), round(y0, 3), round(x1, 3), round(y1, 3))
    s = (u - x0) / (x1 - x0); t = (v - y0) / (y1 - y0); ok = (s >= 0) & (s <= 1) & (t >= 0) & (t <= 1)
    for expr in exprs:
        pic = np.asarray(ledface.led_face(expr)); ph, pw = pic.shape[:2]
        tex = np.asarray(old, dtype=np.uint8).copy()      # start from a painted face, so the gaps round the visor stay filled
        tex[ys[ok], xs[ok]] = pic[np.clip((t[ok] * ph).astype(int), 0, ph - 1), np.clip((s[ok] * pw).astype(int), 0, pw - 1)]
        save(name, expr, tex)


if __name__ == '__main__':
    cmd, name, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
    if cmd == 'front': front(name)
    elif cmd == 'bake': bake(name, rest or ['calm', 'talk', 'angry', 'blink', 'smug'])
    elif cmd == 'led': led(name, rest)
