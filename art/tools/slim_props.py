#!/usr/bin/env python3
"""Cuts the arena's Meshy models down to a triangle count a game can carry.

    art/tracker-model/venv/bin/python art/tools/slim_props.py [Name ...]

art/models/_props/<Name>/model.glb (0.1 to 2.7 million triangles, from art/arena/gen_props.py)
    -> art/models/_props/<Name>/model_slim.glb (TRIANGLES below), which art/tools/import_props.py brings into the game.

Why here and not in Unreal: its mesh reducer is an editor subsystem and does not exist in a run without a window,
and an imported asset keeps the whole source mesh (the first import was 956 MB). There is no Blender on this machine.

How: vertices that fall into the same small cube are merged into one (the average of them), and triangles that
collapse are dropped; the cube's size is searched until the count fits. Vertices only merge if they are also close
on the texture, so the seams of the painted skin stay where they were. Crude next to a real reducer, and it shows
on thin tubes seen close up; for stands and scenery behind a crowd it is enough. Needs numpy only.
"""
import glob, json, os, struct, sys
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
STORE = os.path.join(ROOT, 'art', 'models', '_props')
# The kit is used many times over in every arena (the riser about 110 times); a landmark stands once or twice.
TRIANGLES = {'Podium': 7000, 'Barrier': 3000, 'Truss': 12000, 'Lamp': 10000, 'MovingHead': 8000, 'Post': 8000,
             'Tower': 9000, 'Speakers': 9000, 'Scoreboard': 12000}
LANDMARK = 45000
UV_CELLS = 24          # vertices further apart than 1/24 of the texture never merge


def read(path):
    raw = open(path, 'rb').read()
    n = struct.unpack('<I', raw[12:16])[0]
    j = json.loads(raw[20:20 + n])
    m = struct.unpack('<I', raw[20 + n:24 + n])[0]
    return j, raw[28 + n:28 + n + m]


def accessor(j, blob, index):
    a = j['accessors'][index]; v = j['bufferViews'][a['bufferView']]
    dtype = {5120: 'i1', 5121: 'u1', 5122: '<i2', 5123: '<u2', 5125: '<u4', 5126: '<f4'}[a['componentType']]
    width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    size = np.dtype(dtype).itemsize * width
    stride = v.get('byteStride') or size
    start = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    rows = np.frombuffer(blob, np.uint8, a['count'] * stride if stride == size else (a['count'] - 1) * stride + size, start)
    if stride != size: rows = np.lib.stride_tricks.as_strided(rows, (a['count'], size), (stride, 1))
    return np.ascontiguousarray(rows).view(dtype).reshape(a['count'], width)


def merge(pos, uv, tris, cell):
    grid = np.floor((pos - pos.min(0)) / cell).astype(np.int64)
    skin = np.floor(np.mod(uv, 1.0) * UV_CELLS).astype(np.int64)
    key = (((grid[:, 0] * 4096 + grid[:, 1]) * 4096 + grid[:, 2]) * UV_CELLS + skin[:, 0]) * UV_CELLS + skin[:, 1]
    _, group = np.unique(key, return_inverse=True)
    t = group[tris]
    t = t[(t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])]
    _, first = np.unique(np.sort(t, axis=1), axis=0, return_index=True)      # the same triangle twice: once is enough
    return group, t[np.sort(first)]


def slim(name):
    src = os.path.join(STORE, name, 'model.glb')
    j, blob = read(src)
    assert len(j['meshes']) == 1 and len(j['meshes'][0]['primitives']) == 1, name
    prim = j['meshes'][0]['primitives'][0]
    pos = accessor(j, blob, prim['attributes']['POSITION']).astype(np.float64)
    uv = accessor(j, blob, prim['attributes']['TEXCOORD_0']).astype(np.float64)
    nrm = accessor(j, blob, prim['attributes']['NORMAL']).astype(np.float64) if 'NORMAL' in prim['attributes'] else None
    tris = accessor(j, blob, prim['indices']).astype(np.int64).reshape(-1, 3)
    want = TRIANGLES.get(name, LANDMARK)
    span = float((pos.max(0) - pos.min(0)).max())
    lo, hi = span / 4000.0, span / 6.0                      # cube sizes: lo keeps too much, hi too little
    for _ in range(22):
        mid = (lo * hi) ** 0.5
        if len(merge(pos, uv, tris, mid)[1]) > want: lo = mid
        else: hi = mid
    group, t = merge(pos, uv, tris, hi)
    used, t = np.unique(t, return_inverse=True); t = t.reshape(-1, 3)
    count = np.bincount(group, minlength=group.max() + 1).astype(np.float64)

    def mean(values):
        out = np.stack([np.bincount(group, weights=values[:, k], minlength=len(count)) for k in range(values.shape[1])], 1)
        return (out / count[:, None])[used]

    new_pos = mean(pos).astype('<f4')
    # The texture wraps: average within the cell the vertices share, so a cell on the edge does not average to the middle.
    new_uv = (mean(uv - np.floor(uv)) ).astype('<f4')
    if nrm is None:
        a, b, c = (new_pos[t[:, k]].astype(np.float64) for k in range(3))
        face = np.cross(b - a, c - a); new_nrm = np.zeros((len(new_pos), 3))
        for k in range(3): np.add.at(new_nrm, t[:, k], face)
    else: new_nrm = mean(nrm)
    length = np.linalg.norm(new_nrm, axis=1, keepdims=True); length[length < 1e-9] = 1.0
    new_nrm = (new_nrm / length).astype('<f4')
    index = t.astype('<u4')

    # A new file: the pictures as they were, then the new mesh.
    out = bytearray(); views = []

    def view(data, target=None):
        while len(out) % 4: out.append(0)
        views.append({'buffer': 0, 'byteOffset': len(out), 'byteLength': len(data)})
        if target: views[-1]['target'] = target
        out.extend(data)
        return len(views) - 1

    for image in j.get('images', []):
        old = j['bufferViews'][image['bufferView']]
        image['bufferView'] = view(blob[old.get('byteOffset', 0):old.get('byteOffset', 0) + old['byteLength']])
    j['accessors'] = [
        {'bufferView': view(new_pos.tobytes(), 34962), 'componentType': 5126, 'count': len(new_pos), 'type': 'VEC3',
         'min': new_pos.min(0).tolist(), 'max': new_pos.max(0).tolist()},
        {'bufferView': view(new_nrm.tobytes(), 34962), 'componentType': 5126, 'count': len(new_nrm), 'type': 'VEC3'},
        {'bufferView': view(new_uv.tobytes(), 34962), 'componentType': 5126, 'count': len(new_uv), 'type': 'VEC2'},
        {'bufferView': view(index.tobytes(), 34963), 'componentType': 5125, 'count': index.size, 'type': 'SCALAR'}]
    prim['attributes'] = {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2}; prim['indices'] = 3
    prim.pop('targets', None)
    j['bufferViews'] = views
    while len(out) % 4: out.append(0)
    j['buffers'] = [{'byteLength': len(out)}]
    for gone in ('animations', 'skins'): j.pop(gone, None)
    text = json.dumps(j, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    dst = os.path.join(STORE, name, 'model_slim.glb')
    with open(dst, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, 28 + len(text) + len(out)))
        f.write(struct.pack('<I4s', len(text), b'JSON')); f.write(text)
        f.write(struct.pack('<I4s', len(out), b'BIN\0')); f.write(out)
    print('%-11s %8d -> %6d triangles, %7d -> %6d vertices, cube %.1f cm, %.1f MB' % (
        name, len(tris), len(t), len(pos), len(new_pos), hi * 100, os.path.getsize(dst) / 1e6), flush=True)


if __name__ == '__main__':
    names = sys.argv[1:] or sorted(os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(STORE, '*', 'model.glb')))
    for n in names: slim(n)
