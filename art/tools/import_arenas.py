"""Brings the arena pictures into the game project. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/import_arenas.py

art/arena/raw/bg_<arena>.jpg  -> /Game/KS/Arena/T_KS_Bg_<Arena>    (the wall behind the crowd, 21:9)
art/arena/raw/mat_<arena>.jpg -> /Game/KS/Arena/T_KS_Mat_<Arena>   (the ring canvas, square)

The pictures come from art/arena/gen_arenas.py; the names must match the Key column of KSArenaData.h.
A picture whose file is newer than its asset is brought in again.
"""
import glob, os, shutil
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

done = 0
for jpg in sorted(glob.glob(os.path.join(ROOT, 'art', 'arena', 'raw', '*.jpg'))):
    kind, _, arena = os.path.splitext(os.path.basename(jpg))[0].partition('_')
    if kind not in ('bg', 'mat') or not arena: continue
    name = 'T_KS_%s_%s' % ('Bg' if kind == 'bg' else 'Mat', arena.capitalize())
    asset = os.path.join(ROOT, 'unreal', 'Content', 'KS', 'Arena', name + '.uasset')
    if os.path.exists(asset) and os.path.getmtime(asset) >= os.path.getmtime(jpg): continue
    # Dreamina's downloads are PNG data whatever the file is called, and Unreal picks its reader by the name, so a
    # ".jpg" that is a PNG fails to import without a useful word. Hand it over under its true type.
    with open(jpg, 'rb') as f: is_png = f.read(8) == b'\x89PNG\r\n\x1a\n'
    src = os.path.join(ROOT, 'art', 'arena', name + ('.png' if is_png else '.jpg'))
    shutil.copyfile(jpg, src)
    task = unreal.AssetImportTask()
    task.filename = src; task.destination_path = '/Game/KS/Arena'
    task.automated = True; task.replace_existing = True; task.save = True
    tools.import_asset_tasks([task])
    tex = EAL.load_asset('/Game/KS/Arena/' + name)
    if not tex: unreal.log_error('KSIMPORT failed picture %s' % jpg); continue
    # The backdrops are not a power of two in size, so they cannot stream or carry smaller copies. The mats are
    # shown at half their painted size: twenty pictures at full size would be a fifth of the 1 GB the package may weigh.
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('srgb', True)
    tex.set_editor_property('max_texture_size', 1024 if kind == 'mat' else 4096)
    EAL.save_loaded_asset(tex)
    done += 1
    unreal.log('KSIMPORT arena picture %s' % name)

# The opening film's frame sheets (art/cinematic/prep_cinematic.py) -> /Game/KS/Cine.
film = 0
for png in sorted(glob.glob(os.path.join(ROOT, 'art', 'cinematic', 'T_KS_Cine_*.png'))):
    name = os.path.splitext(os.path.basename(png))[0]
    asset = os.path.join(ROOT, 'unreal', 'Content', 'KS', 'Cine', name + '.uasset')
    if os.path.exists(asset) and os.path.getmtime(asset) >= os.path.getmtime(png): continue
    task = unreal.AssetImportTask()
    task.filename = png; task.destination_path = '/Game/KS/Cine'
    task.automated = True; task.replace_existing = True; task.save = True
    tools.import_asset_tasks([task])
    tex = EAL.load_asset('/Game/KS/Cine/' + name)
    if not tex: unreal.log_error('KSIMPORT failed film sheet %s' % png); continue
    # A sheet is read one small frame at a time and always whole: no smaller copies, no streaming.
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('srgb', True)
    EAL.save_loaded_asset(tex)
    film += 1

unreal.log('KSIMPORT done arena_pictures=%d film_sheets=%d' % (done, film))
