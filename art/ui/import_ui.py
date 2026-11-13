"""Brings the interface art, fonts and sounds into the game project. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/ui/import_ui.py

Pictures: art/ui/out/T_UI_<name>.png (from art/ui/cut_ui.py)  -> /Game/KS/UI/T_UI_<name>
Fonts:    art/ui/fonts/F_<Name>.ttf (SIL Open Font License)   -> copied as files to unreal/Content/KS/UI/Fonts; KSHUD reads them
          itself (Unreal's font importer needs a window and crashes here). DefaultGame.ini stages that folder.
Sounds:   art/audio/S_<Pop|Menu|Select|Score|Slam>_<n>.wav (from art/tools/prep_audio.py) -> /Game/KS/Audio

Only what is missing or older than its source is imported.
"""
import glob, os, shutil
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


pictures = fonts = sounds = 0
for png in sorted(glob.glob(os.path.join(ROOT, 'art', 'ui', 'out', 'T_UI_*.png'))):
    if not stale(png, 'KS/UI'): continue
    tex = bring(png, '/Game/KS/UI')
    if not tex: unreal.log_error('KSIMPORT failed picture %s' % png); continue
    # Interface pictures are drawn at their own size: no smaller copies, never loaded late, edges not repeated.
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_BC7)
    EAL.save_loaded_asset(tex)
    pictures += 1

for ttf in sorted(glob.glob(os.path.join(ROOT, 'art', 'ui', 'fonts', 'F_*.ttf'))):
    folder = os.path.join(ROOT, 'unreal', 'Content', 'KS', 'UI', 'Fonts')
    os.makedirs(folder, exist_ok=True)
    for f in (ttf, ttf[:-4] + '.OFL.txt'): shutil.copy2(f, folder)     # the licence travels with the font
    fonts += 1

for kind in ('Pop', 'Menu', 'Select', 'Score', 'Slam'):
    for wav in sorted(glob.glob(os.path.join(ROOT, 'art', 'audio', 'S_%s_*.wav' % kind))):
        if not stale(wav, 'KS/Audio'): continue
        if bring(wav, '/Game/KS/Audio'): sounds += 1
        else: unreal.log_error('KSIMPORT failed sound %s' % wav)

unreal.log('KSIMPORT ui done: %d pictures, %d fonts, %d sounds' % (pictures, fonts, sounds))
