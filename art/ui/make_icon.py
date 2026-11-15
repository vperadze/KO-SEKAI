"""Makes the game's program icon: python art/ui/make_icon.py   (needs Pillow; no credits, no editor)

The logo (art/ui/out/T_UI_logo.png) is a wide word and cannot be read at the size of an icon, and its letters run
into each other, so two of them cannot be cut out cleanly. The icon is drawn here instead, in the logo's manner: "KO"
in the game's own font (Russo One), slanted, with the logo's chrome stripes and its pink, violet and cyan rims, the
lightning bolt behind, on a dark tile. Writes unreal/Build/Windows/Application.ico (what Unreal puts into the .exe
when it builds and packages) and art/ui/out/icon.png (the same picture at 1024, for the jam page).
"""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT = os.path.join(ROOT, 'art', 'ui', 'fonts', 'F_RussoOne.ttf')
ICO = os.path.join(ROOT, 'unreal', 'Build', 'Windows', 'Application.ico')
PNG = os.path.join(ROOT, 'art', 'ui', 'out', 'icon.png')
N = 1024
PINK, CYAN, VIOLET, DARK = (255, 43, 196), (0, 229, 255), (74, 22, 130), (22, 6, 44)


def gradient(stops, size=N):
    """A picture filled top to bottom through (position 0-1, colour) stops."""
    im = Image.new('RGBA', (size, size))
    d = ImageDraw.Draw(im)
    for y in range(size):
        t = y / (size - 1)
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                u = (t - t0) / max(1e-6, t1 - t0)
                d.line([(0, y), (size, y)], fill=tuple(round(a + (b - a) * u) for a, b in zip(c0, c1)) + (255,))
                break
    return im


def grow(mask, px):
    return mask.filter(ImageFilter.MaxFilter(2 * px + 1))


def flat(colour, mask):
    im = Image.new('RGBA', mask.size, colour + (255,))
    im.putalpha(mask)
    return im


# The tile: a rounded square, violet down to near black, with the logo's cyan and pink as its rim.
tile = Image.new('RGBA', (N, N), (0, 0, 0, 0))
shape = Image.new('L', (N, N), 0)
ImageDraw.Draw(shape).rounded_rectangle((24, 24, N - 24, N - 24), radius=190, fill=255)
tile.paste(gradient([(0, (66, 20, 112)), (1, (14, 4, 32))]), (0, 0), shape)
d = ImageDraw.Draw(tile)
d.rounded_rectangle((24, 24, N - 24, N - 24), radius=190, outline=CYAN + (255,), width=22)
d.rounded_rectangle((46, 46, N - 46, N - 46), radius=170, outline=PINK + (255,), width=16)

# The bolt, behind the letters: yellow to orange, with a dark rim.
bolt = Image.new('L', (N, N), 0)
ImageDraw.Draw(bolt).polygon([(640, 96), (368, 540), (500, 540), (388, 930), (690, 452), (552, 452)], fill=255)
tile.alpha_composite(flat(DARK, grow(bolt, 14)))
tile.alpha_composite(flat((255, 236, 150), grow(bolt, 4)))
fill = gradient([(0, (255, 232, 64)), (0.55, (255, 190, 30)), (1, (255, 120, 20))]); fill.putalpha(bolt)
tile.alpha_composite(fill)

# "KO": drawn upright, then slanted as the logo's letters are.
font = ImageFont.truetype(FONT, 470)
letters = Image.new('L', (N, N), 0)
ld = ImageDraw.Draw(letters)
box = ld.textbbox((0, 0), 'KO', font=font)
ld.text(((N - (box[2] - box[0])) // 2 - box[0], (N - (box[3] - box[1])) // 2 - box[1]), 'KO', font=font, fill=255)
letters = letters.transform((N, N), Image.AFFINE, (1, 0.20, -0.20 * N / 2, 0, 1, 0), resample=Image.BICUBIC)

# From the back: the cyan drop, the dark rim, violet, pink, white, and the chrome face.
drop = ImageChops.offset(grow(letters, 34), 0, 22)
tile.alpha_composite(flat(CYAN, ImageChops.multiply(drop, shape)))
tile.alpha_composite(flat(DARK, grow(letters, 34)))
tile.alpha_composite(flat(VIOLET, grow(letters, 26)))
tile.alpha_composite(flat(PINK, grow(letters, 16)))
tile.alpha_composite(flat((255, 255, 255), grow(letters, 7)))
top, bottom = (N - (box[3] - box[1])) / 2 / N, (N + (box[3] - box[1])) / 2 / N
span = bottom - top
chrome = gradient([(0, (60, 140, 255)), (top, (60, 140, 255)), (top + 0.22 * span, (90, 225, 255)), (top + 0.40 * span, (255, 255, 255)),
                   (top + 0.47 * span, (255, 215, 120)), (top + 0.53 * span, (255, 90, 190)), (top + 0.74 * span, (140, 80, 255)),
                   (bottom, (60, 190, 255)), (1, (60, 190, 255))])
chrome.putalpha(letters)
tile.alpha_composite(chrome)

# Nothing outside the tile.
tile.putalpha(ImageChops.multiply(tile.split()[-1], shape))

os.makedirs(os.path.dirname(ICO), exist_ok=True)
tile.save(PNG)
tile.save(ICO, sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)])
print('wrote', ICO, os.path.getsize(ICO), 'bytes and', PNG)
