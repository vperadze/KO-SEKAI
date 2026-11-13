#!/usr/bin/env python3
"""Cuts the fighters' portraits for the select screen out of photographs the game takes of its own models.

    UnrealEditor unreal/KoSekai.uproject -game -windowed -ResX=1280 -ResY=720 -KSPortraits -KSNoCamera -nosound
        writes unreal/Saved/Screenshots/avatar_<Folder>.png (one head each) and quits
    art/tracker-model/venv/bin/python art/ui/make_avatars.py
        writes art/ui/out/T_UI_av_<Folder>.png (256 x 256) and a contact sheet art/ui/out/avatars_contact.jpg
    then art/ui/import_ui.py headless.
"""
import glob, os
from PIL import Image, ImageEnhance

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SHOTS = os.path.join(ROOT, 'unreal', 'Saved', 'Screenshots')
OUT = os.path.join(ROOT, 'art', 'ui', 'out')
SIZE = 256

tiles = []
for shot in sorted(glob.glob(os.path.join(SHOTS, 'avatar_*.png'))):
    name = os.path.splitext(os.path.basename(shot))[0][len('avatar_'):]
    im = Image.open(shot).convert('RGB')
    w, h = im.size
    side = h                                                    # the head is in the middle of the picture
    im = im.crop(((w - side) // 2, 0, (w + side) // 2, side)).resize((SIZE, SIZE), Image.LANCZOS)
    im = ImageEnhance.Contrast(ImageEnhance.Color(im).enhance(1.15)).enhance(1.08)
    im.save(os.path.join(OUT, 'T_UI_av_%s.png' % name))
    tiles.append(im)
    print('T_UI_av_%s.png' % name)
if tiles:
    sheet = Image.new('RGB', (SIZE * len(tiles), SIZE))
    for i, im in enumerate(tiles): sheet.paste(im, (SIZE * i, 0))
    sheet.save(os.path.join(OUT, 'avatars_contact.jpg'), quality=88)
print('%d portraits' % len(tiles))
