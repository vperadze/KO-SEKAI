"""Brings the spectators and the crowd's sounds into the game project. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/import_crowd.py

Pictures: art/arena/T_KS_Crowd_<name>.png (from art/arena/slice_crowd.py) -> /Game/KS/Arena/T_KS_Crowd_<name>
Sounds:   art/audio/S_<Cheer|Roar|Gasp|Boo|Shout|Beast>_<n>.wav and art/audio/M_Crowd.wav (from art/tools/prep_crowd.py)
          -> /Game/KS/Audio; M_Crowd is set to loop.

Anything whose source file is newer than its asset is brought in again.
"""
import glob, os, struct
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def stale(src, folder):
    asset = os.path.join(ROOT, 'unreal', 'Content', folder, os.path.splitext(os.path.basename(src))[0] + '.uasset')
    return not os.path.exists(asset) or os.path.getmtime(asset) < os.path.getmtime(src)


def bring(path, folder):
    task = unreal.AssetImportTask()
    task.filename = path; task.destination_path = folder
    task.automated = True; task.replace_existing = True; task.save = True
    tools.import_asset_tasks([task])
    return EAL.load_asset('%s/%s' % (folder, os.path.splitext(os.path.basename(path))[0]))


pictures = sounds = 0
for png in sorted(glob.glob(os.path.join(ROOT, 'art', 'arena', 'T_KS_Crowd_*.png'))):
    if not stale(png, 'KS/Arena'): continue
    tex = bring(png, '/Game/KS/Arena')
    if not tex: unreal.log_error('KSIMPORT failed picture %s' % png); continue
    # Rendered people, not pixels: smooth filtering (the pixel crowd used TF_NEAREST). The sheets are small, so they need not stream.
    tex.set_editor_property('filter', unreal.TextureFilter.TF_DEFAULT)
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('srgb', True)
    EAL.save_loaded_asset(tex)
    pictures += 1

# Every sheet is cooked at half its drawn size (SHRINK): 179 sheets at full size were 255 MB of a package that has to
# stay under 1 GB. The engine does the shrinking (max_texture_size), so the pictures in art/arena stay as drawn and
# SHRINK = 1 gives the full size back. This runs over all the sheets, not only the ones just brought in.
SHRINK = 2
capped, sizes = 0, {}
for png in sorted(glob.glob(os.path.join(ROOT, 'art', 'arena', 'T_KS_Crowd_*.png'))):
    with open(png, 'rb') as f:
        wide, high = struct.unpack('>II', f.read(24)[16:24])
    limit = 0 if SHRINK == 1 else max(wide, high) // SHRINK
    sizes[limit] = sizes.get(limit, 0) + 1
    tex = EAL.load_asset('/Game/KS/Arena/' + os.path.splitext(os.path.basename(png))[0])
    if not tex: unreal.log_error('KSIMPORT no asset for %s' % png); continue
    if tex.get_editor_property('max_texture_size') == limit: continue
    tex.set_editor_property('max_texture_size', limit)
    EAL.save_loaded_asset(tex)
    capped += 1
with open(os.path.join(ROOT, 'unreal', 'Saved', 'import_crowd.txt'), 'w') as f:
    f.write('pictures=%d changed=%d shrink=%d longest_side=%s\n' % (sum(sizes.values()), capped, SHRINK, sorted(sizes.items())))

names =[w for kind in ('Cheer', 'Roar', 'Gasp', 'Boo', 'Shout', 'Beast') for w in sorted(glob.glob(os.path.join(ROOT, 'art', 'audio', 'S_%s_*.wav' % kind)))]
for wav in names + glob.glob(os.path.join(ROOT, 'art', 'audio', 'M_Crowd.wav')):
    if not stale(wav, 'KS/Audio'): continue
    sound = bring(wav, '/Game/KS/Audio')
    if not sound: unreal.log_error('KSIMPORT failed sound %s' % wav); continue
    if os.path.basename(wav).startswith('M_'):
        sound.set_editor_property('looping', True)
        EAL.save_loaded_asset(sound)
    sounds += 1

unreal.log('KSIMPORT crowd done pictures=%d sounds=%d' % (pictures, sounds))
