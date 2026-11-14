#!/usr/bin/env python3
"""Cuts the jam trailer (40 s, 1920x1080) from the opening film's shots and the takes of art/trailer/takes.sh.

    python3 art/trailer/make_trailer.py            -> art/trailer/KO-SEKAI_trailer.mp4
    python3 art/trailer/make_trailer.py sheet      -> art/trailer/build/sheet.jpg, one small picture a second, to look at

Needs an ffmpeg with libx264 (KS_FFMPEG, or one on the path; `pip install imageio-ffmpeg` brings one) and Pillow.
The cut is the three tables below: CUT (the pictures, in order), TITLES (words slammed on top) and SOUND (voices
and effects laid over the music). Times in CUT are seconds into the source; times in the other two are seconds
into the trailer. The script it follows is in art/trailer/SCRIPT.md.
"""
import os, random, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
BUILD = os.path.join(HERE, 'build')
OUT = os.path.join(HERE, 'KO-SEKAI_trailer.mp4')
FF = os.environ.get('KS_FFMPEG') or shutil.which('ffmpeg') or sys.exit('no ffmpeg: set KS_FFMPEG')
W, H, FPS = 1920, 1080, 30
FILM, TAKE, VOICE, AUDIO = (os.path.join(ROOT, *p) for p in (('art', 'cinematic', 'raw'), ('art', 'trailer', 'raw'),
                                                             ('art', 'audio', 'voice'), ('art', 'audio')))
BANGERS = os.path.join(ROOT, 'art', 'ui', 'fonts', 'F_Bangers.ttf')
RUSSO = os.path.join(ROOT, 'art', 'ui', 'fonts', 'F_RussoOne.ttf')
# The game writes "WEBCAM OFF - KEYBOARD" in the bottom right corner when it plays without a camera, as it does in
# every take. It is smeared out of the gameplay shots (x, y, width, height in the take's 1600x900).
HINT = 'delogo=x=1066:y=860:w=526:h=34'

# ---------------------------------------------------------------------------------------------------------------
# (source, start, seconds in the trailer, speed, how loud its own sound is, extra)
#   source "film/x" is art/cinematic/raw/x.mp4, "take/x" is art/trailer/raw/x.mkv, "still/x" the picture art/ui/out/T_UI_x.png
#   A take's first three seconds show the desktop under the window that is still opening: never start before 4.
#   extra: 'flash' opens the shot on a white flash, 'nohint' leaves the corner alone (menus, the film)
CUT = [
    # 1987: the film (13 s)
    ('film/city',          0.3, 2.6, 1, 0.45, ''),
    ('extra/fighters7',    0.3, 3.2, 1, 0.45, 'flash'),     # the film's own shot has six: this one was made again with the vampire
    ('film/cancelled',     0.5, 2.4, 1, 0.45, ''),
    ('film/alley',         1.5, 2.2, 1, 0.45, ''),
    ('film/fists',         2.7, 1.3, 1, 0.6, ''),
    ('film/pull',          2.6, 1.3, 1, 0.5, ''),
    # choose your fighter (4 s)
    ('take/select',        5.0, 0.6, 1, 0, 'flash'),
    ('take/select',       14.0, 0.6, 1, 0, ''),
    ('take/select',       23.0, 0.6, 1, 0, ''),
    ('take/select',       32.0, 0.55, 1, 0, ''),
    ('take/select',       38.0, 0.55, 1, 0, ''),
    ('take/select',       47.0, 0.55, 1, 0, ''),
    ('take/select',       56.0, 0.55, 1, 0, ''),
    # the arenas (3 s)
    ('take/bout_volcano',  4.2, 1.05, 1, 0, 'nohint'),
    ('take/bout_space',    4.0, 0.65, 1, 0, 'nohint'),
    ('take/spec_dojo',     4.3, 0.65, 1, 0, 'nohint'),
    ('take/bout_castle',   4.0, 0.65, 1, 0, 'nohint'),
    # the fight (8 s)
    ('take/bout_volcano', 15.2, 1.6, 1, 0.8, 'flash'),
    ('take/bout_space',   14.2, 1.3, 1, 0.8, ''),
    ('take/bout_castle',  23.4, 1.3, 1, 0.8, ''),
    ('take/bout_beach',   47.2, 1.5, 1, 0.8, ''),
    ('take/bout_volcano', 63.3, 1.2, 1, 0.8, ''),
    ('take/bout_space',   32.3, 1.1, 1, 0.8, ''),
    # the specials (5 s)
    ('take/spec_rooftop', 12.3, 1.6, 1, 0.9, 'flash'),
    ('take/spec_dojo',    10.2, 1.7, 1, 0.9, ''),
    ('take/spec_studio',  17.9, 1.7, 1, 0.9, ''),
    # knockout (3 s)
    ('take/bout_beach',   57.2, 1.8, 1, 0.9, 'flash'),
    ('take/bout_space',   59.4, 1.2, 1, 0.5, ''),
    # the name (4 s)
    ('still/title_bg',     0.0, 4.0, 1, 0, 'flash'),
]
# (text, from, seconds, style, x, y) with x, y the middle of the words as parts of the picture
#   styles: 'yell' big comic letters, 'tape' the film's caption line, 'small' a line under a yell, 'logo' the logo
TITLES = [
    ('1987.',                                0.5, 1.9, 'tape', 0.5, 0.84),
    ('THE GREATEST GAME NEVER RELEASED',     3.0, 2.6, 'tape', 0.5, 0.84),
    ('CANCELLED.',                           6.0, 2.0, 'yell', 0.5, 0.5),
    ('ONE CABINET SURVIVED.',                8.4, 2.0, 'tape', 0.5, 0.84),
    ('INSERT YOURSELF.',                    11.8, 1.1, 'cyan', 0.5, 0.5),
    ('7 FIGHTERS',                          15.1, 1.8, 'huge', 0.3, 0.62),
    ('11 ARENAS',                           17.9, 2.0, 'huge', 0.5, 0.3),
    ('WEBCAM BOXING',                       20.5, 2.0, 'huge', 0.5, 0.78),
    ('NO GAMEPAD.  JUST YOUR FISTS.',       22.7, 2.2, 'small', 0.5, 0.9),
    ('DODGE.  BLOCK.  COUNTER.',            25.3, 2.4, 'small', 0.5, 0.9),
    ('SHOUT TO UNLEASH\nYOUR SPECIAL',      28.2, 2.2, 'yell', 0.5, 0.74),
    ('',                                    36.15, 3.85, 'logo', 0.5, 0.33),
    ('WEBCAM BOXING',                       36.9, 3.1, 'yell', 0.5, 0.66),
    ('UNRATED.  UNRELEASED.  UNTIL NOW.',   37.6, 2.4, 'small', 0.5, 0.82),
    ('EPIC MEGAJAM 2026',                   38.2, 1.8, 'small', 0.5, 0.92),
]
# (file, at, loudness)   "vo/x" art/trailer/vo/VO_x.wav, "ann/x" the ring announcer, "sfx/x" art/audio/S_x.wav,
#                        "voice/x" a fighter's line art/audio/voice/V_x.wav
SOUND = [
    ('vo/year',              0.3, 1.0),
    ('vo/cancelled',         5.6, 1.0),
    ('vo/cabinet',           9.3, 1.0),
    ('sfx/Slam_1',          11.8, 0.7),
    ('ann/choose',          13.2, 0.9),
    ('sfx/Select_1',        13.6, 0.5), ('sfx/Select_1', 14.2, 0.5), ('sfx/Select_1', 14.8, 0.5), ('sfx/Select_1', 15.35, 0.5),
    ('sfx/Select_1',        15.9, 0.5), ('sfx/Select_1', 16.45, 0.5),
    ('ann/name_GrandmaThunder', 17.05, 0.9),
    ('sfx/Slam_1',          17.9, 0.7),
    ('vo/fists',            20.3, 1.0),
    ('sfx/Slam_1',          20.5, 0.6),
    ('sfx/Slam_1',          28.2, 0.6),
    ('ann/ko',              33.4, 1.0),
    ('sfx/Cheer_1',         34.2, 0.6),
    ('sfx/Slam_1',          36.15, 0.9),
    ('vo/rated',            37.0, 1.0),
]
MUSIC = ('M_Fight.wav', 0.0, 0.55)        # file in art/audio, where in it the trailer starts, loudness
LENGTH = 40.0
# ---------------------------------------------------------------------------------------------------------------


def run(*a):
    r = subprocess.run([FF, '-hide_banner', '-loglevel', 'error', '-y', *a])
    if r.returncode: sys.exit('ffmpeg failed: ' + ' '.join(a[:12]))


def path(name):
    kind, n = name.split('/', 1)
    return {'film': lambda: os.path.join(FILM, n + '.mp4'), 'take': lambda: os.path.join(TAKE, n + '.mkv'),
            'vo': lambda: os.path.join(HERE, 'vo', 'VO_%s.wav' % n), 'ann': lambda: os.path.join(VOICE, 'V_Announcer_%s.wav' % n),
            'voice': lambda: os.path.join(VOICE, 'V_%s.wav' % n), 'sfx': lambda: os.path.join(AUDIO, 'S_%s.wav' % n),
            'still': lambda: os.path.join(ROOT, 'art', 'ui', 'out', 'T_UI_%s.png' % n),
            'extra': lambda: os.path.join(HERE, 'dreamina', n + '.mp4')}[kind]()


def segments():
    """Every shot of CUT as its own file, all alike (size, rate, coding), so they join without coding again."""
    os.makedirs(BUILD, exist_ok=True)
    files, at = [], 0.0
    for i, (src, start, sec, speed, _, extra) in enumerate(CUT):
        frames = round(sec * FPS)
        vf = []
        if src.startswith('take/') and 'nohint' not in extra: vf.append(HINT)
        vf += ['setpts=(PTS-STARTPTS)/%g' % speed, 'fps=%d' % FPS, 'scale=%d:%d:flags=lanczos' % (W, H), 'setsar=1']
        if src.startswith('take/'): vf.append('unsharp=5:5:0.6')            # 1600 wide stretched to 1920
        if 'flash' in extra: vf.append('fade=t=in:st=0:d=0.2:color=white')
        out = os.path.join(BUILD, 'seg_%02d.mp4' % i)
        key = 'v2|%s|%s|%s|%s|%s' % (src, start, sec, speed, extra)
        if not (os.path.exists(out) and os.path.exists(out + '.key') and open(out + '.key').read() == key):
            still = ['-loop', '1', '-framerate', str(FPS)] if src.startswith('still/') else []
            run(*still, '-ss', '%.3f' % start, '-i', path(src), '-t', '%.3f' % (sec * speed + 0.5), '-an', '-vf', ','.join(vf), '-frames:v', str(frames),
                '-c:v', 'libx264', '-preset', 'fast', '-crf', '12', '-pix_fmt', 'yuv420p',
                # the same colour labels on every shot: joined without them, the film's shots (unlabelled) were dropped by the decoder
                '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv', '-video_track_timescale', '15360', out)
            open(out + '.key', 'w').write(key)
        files.append((out, at, frames / FPS)); at += frames / FPS
    with open(os.path.join(BUILD, 'list.txt'), 'w') as f:
        for out, _, _ in files: f.write("file '%s'\n" % out)
    run('-f', 'concat', '-safe', '0', '-i', os.path.join(BUILD, 'list.txt'), '-c', 'copy', os.path.join(BUILD, 'body.mp4'))
    return files, at


# ---- the words ---------------------------------------------------------------------------------------------------
def words(text, style):
    """One title as a picture with a clear background, at the size it rests at."""
    if style == 'logo':
        im = Image.open(os.path.join(ROOT, 'art', 'ui', 'out', 'T_UI_logo.png')).convert('RGBA')
        k = 1250 / im.width
        return im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
    font, size, fill, edge, shadow = {'yell': (BANGERS, 150, (255, 226, 52), 10, (255, 30, 150)),
                                      'huge': (BANGERS, 230, (255, 226, 52), 13, (255, 30, 150)),
                                      'cyan': (BANGERS, 150, (80, 240, 255), 10, (255, 30, 150)),
                                      'small': (RUSSO, 58, (255, 255, 255), 5, (20, 0, 50)),
                                      'tape': (RUSSO, 64, (255, 230, 80), 5, (20, 0, 50))}[style]
    f = ImageFont.truetype(font, size)
    lines = text.split('\n')
    boxes = [f.getbbox(l) for l in lines]
    lw = [b[2] - b[0] for b in boxes]; lh = int(size * 1.02)
    pad = edge * 3 + 16
    im = Image.new('RGBA', (max(lw) + 2 * pad, lh * len(lines) + 2 * pad), (0, 0, 0, 0))
    for layer in ('shadow', 'face'):
        d = ImageDraw.Draw(im)
        for n, l in enumerate(lines):
            x = pad + (max(lw) - lw[n]) // 2 - boxes[n][0]; y = pad + n * lh - int(size * 0.1)
            if layer == 'shadow': d.text((x + edge, y + edge), l, font=f, fill=shadow + (255,), stroke_width=edge, stroke_fill=shadow + (255,))
            else: d.text((x, y), l, font=f, fill=fill + (255,), stroke_width=edge, stroke_fill=(16, 0, 40, 255))
    if style in ('yell', 'huge', 'cyan'): im = im.rotate(-4, expand=True, resample=Image.BICUBIC)
    return im


def overlay_frames(pipe, total):
    """The layer over the pictures, frame by frame: scan lines, the recorder's PLAY mark, and the titles slamming in."""
    rng = random.Random(7)
    base = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(base)
    for y in range(0, H, 4): d.line([(0, y), (W, y)], fill=(0, 0, 0, 34), width=1)
    vig = Image.new('L', (W // 8, H // 8), 0)
    ImageDraw.Draw(vig).ellipse([-30, -22, W // 8 + 30, H // 8 + 22], fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(26)).resize((W, H))
    base = Image.composite(base, Image.new('RGBA', (W, H), (0, 0, 0, 150)), vig)      # darker towards the corners
    osd = ImageFont.truetype(RUSSO, 54)
    made = [(words(t, s), a, sec, s, x, y) for t, a, sec, s, x, y in TITLES]
    plain = base.tobytes()
    for n in range(total):
        t = n / FPS
        live = [m for m in made if m[1] <= t < m[1] + m[2]]
        tape = t < 2.6 or t > LENGTH - 0.9
        if not live and not tape: pipe.write(plain); continue
        im = base.copy()
        if tape:                                                                     # the video recorder's own letters
            d = ImageDraw.Draw(im)
            if int(t * 2) % 2 == 0 or t > 1: d.text((110, 84), 'PLAY ▶' if t < 3 else 'STOP ■', font=osd, fill=(255, 255, 255, 235), stroke_width=3, stroke_fill=(0, 0, 0, 160))
            if t < 3: d.text((W - 470, H - 150), 'SP  0:00:%02d' % int(t), font=osd, fill=(255, 255, 255, 235), stroke_width=3, stroke_fill=(0, 0, 0, 160))
            for _ in range(5):                                                       # a band of tracking noise near the bottom
                y = H - 70 + rng.randint(-26, 26); d.line([(0, y), (W, y)], fill=(255, 255, 255, rng.randint(40, 150)), width=rng.randint(1, 4))
        for pic, a, sec, style, x, y in live:
            p = (t - a) * FPS                                                        # frames since it appeared
            left = (a + sec - t) * FPS
            grow = 1 + 1.6 * max(0.0, 1 - p / 4) ** 2 if style != 'tape' else 1.0   # slams in from large over four frames
            alpha = min(1.0, (p + 1) / 3, left / 3)
            shake = max(0.0, 1 - p / 9) * 14 if style in ('yell', 'huge', 'cyan', 'logo') else 0
            q = pic if grow == 1 else pic.resize((round(pic.width * grow), round(pic.height * grow)), Image.BILINEAR)
            if alpha < 1: q = q.copy(); q.putalpha(q.getchannel('A').point(lambda v: int(v * alpha)))
            im.alpha_composite(q, (max(0, min(W - q.width, round(x * W - q.width / 2 + rng.uniform(-shake, shake)))),
                                   max(0, min(H - q.height, round(y * H - q.height / 2 + rng.uniform(-shake, shake))))))
        pipe.write(im.tobytes())


# ---- the sound ---------------------------------------------------------------------------------------------------
def sound(files):
    """ffmpeg's inputs and the filter that mixes them: music that ducks under the voices, each shot's own sound, the lines."""
    ins, parts, mix, speech = [], [], [], []
    def add(args, chain, bus):
        k = len(ins) + 2                                                             # 0 is the picture, 1 the overlay
        ins.append(args); parts.append('[%d:a]%s[a%d]' % (k, chain, k)); bus.append('[a%d]' % k)
    stereo = 'aresample=48000,aformat=channel_layouts=stereo'
    for (src, start, sec, speed, gain, _), (_, at, real) in zip(CUT, files):
        if gain <= 0 or speed != 1 or src.startswith('still/'): continue
        hp = ',highpass=f=220' if src.startswith('take/') else ''                    # thins the take's own music under ours
        add(['-ss', '%.3f' % start, '-t', '%.3f' % real, '-i', path(src)],
            '%s%s,afade=t=in:d=0.04,afade=t=out:st=%.3f:d=0.06,volume=%g,adelay=%d:all=1' % (stereo, hp, max(0, real - 0.06), gain, round(at * 1000)), mix)
    for name, at, gain in SOUND:
        add(['-i', path(name)], '%s,volume=%g,adelay=%d:all=1' % (stereo, gain, round(at * 1000)), speech if name.split('/')[0] in ('vo', 'ann') else mix)
    k = len(ins) + 2
    ins.append(['-ss', '%.3f' % MUSIC[1], '-t', '%.3f' % LENGTH, '-i', os.path.join(AUDIO, MUSIC[0])])
    parts.append('[%d:a]%s,volume=%g,afade=t=in:d=0.4,afade=t=out:st=%.2f:d=1.2[mus]' % (k, stereo, MUSIC[2], LENGTH - 1.3))
    parts.append('%samix=inputs=%d:normalize=0,asplit[sp][key]' % (''.join(speech), len(speech)) if len(speech) > 1 else '%sasplit[sp][key]' % speech[0])
    parts.append('[mus][key]sidechaincompress=threshold=0.03:ratio=6:attack=15:release=350[duck]')
    parts.append('[duck][sp]%samix=inputs=%d:normalize=0,alimiter=limit=0.89,atrim=0:%.2f[aout]' % (''.join(mix), len(mix) + 2, LENGTH))
    return ins, parts


def main():
    files, total = segments()
    print('pictures: %.2f s in %d shots' % (total, len(files)))
    if len(sys.argv) > 1 and sys.argv[1] == 'sheet':
        run('-i', os.path.join(BUILD, 'body.mp4'), '-vf', 'fps=1,scale=384:-1,tile=8x5', '-frames:v', '1', os.path.join(BUILD, 'sheet.jpg')); return
    ins, parts = sound(files)
    # A worn tape: the colours a little apart, grain, a touch more colour. The words get it too, so they sit in the picture.
    look = '[0:v][1:v]overlay=format=auto,rgbashift=rh=3:bh=-3,noise=alls=9:allf=t,eq=saturation=1.12:contrast=1.04,format=yuv420p[vout]'
    cmd = [FF, '-hide_banner', '-loglevel', 'error', '-y', '-i', os.path.join(BUILD, 'body.mp4'),
           '-f', 'rawvideo', '-pix_fmt', 'rgba', '-s', '%dx%d' % (W, H), '-r', str(FPS), '-i', '-']
    for a in ins: cmd += a
    cmd += ['-filter_complex', ';'.join([look] + parts), '-map', '[vout]', '-map', '[aout]', '-t', '%.2f' % LENGTH,
            '-c:v', 'libx264', '-preset', 'slow', '-crf', '20', '-maxrate', '14M', '-bufsize', '28M', '-c:a', 'aac', '-b:a', '224k', '-movflags', '+faststart', OUT]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    overlay_frames(p.stdin, round(LENGTH * FPS))
    p.stdin.close()
    if p.wait(): sys.exit('ffmpeg failed on the final pass')
    print(OUT)


if __name__ == '__main__':
    main()
