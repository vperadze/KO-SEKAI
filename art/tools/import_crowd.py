"""Brings the spectators and the crowd's sounds into the game project. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/import_crowd.py

Pictures: art/arena/T_KS_Crowd_<name>.png (from art/arena/slice_crowd.py) -> /Game/KS/Arena/T_KS_Crowd_<name>
Sounds:   art/audio/S_<Cheer|Roar|Gasp|Boo|Shout|Beast>_<n>.wav and art/audio/M_Crowd.wav (from art/tools/prep_crowd.py)
          -> /Game/KS/Audio; M_Crowd is set to loop.

Anything whose source file is newer than its asset is brought in again.
"""
import glob, os
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

names = [w for kind in ('Cheer', 'Roar', 'Gasp', 'Boo', 'Shout', 'Beast') for w in sorted(glob.glob(os.path.join(ROOT, 'art', 'audio', 'S_%s_*.wav' % kind)))]
for wav in names + glob.glob(os.path.join(ROOT, 'art', 'audio', 'M_Crowd.wav')):
    if not stale(wav, 'KS/Audio'): continue
    sound = bring(wav, '/Game/KS/Audio')
    if not sound: unreal.log_error('KSIMPORT failed sound %s' % wav); continue
    if os.path.basename(wav).startswith('M_'):
        sound.set_editor_property('looping', True)
        EAL.save_loaded_asset(sound)
    sounds += 1

unreal.log('KSIMPORT crowd done pictures=%d sounds=%d' % (pictures, sounds))
