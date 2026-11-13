"""Brings new face textures and voice lines into the game project. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/import_voice_faces.py

Faces:  art/models/<fighter>/faces/T_<Name>_Face_<expr>.png
        -> /Game/KoSekai/Characters/<Folder>/Faces/T_<Name>_Face_<expr> and a material MI_<Name>_Face_<expr>,
           copied from one of the fighter's existing face materials with the picture swapped.
Voices: art/audio/voice/V_<Folder>_<line>.wav -> /Game/KS/Voice/V_<Folder>_<line>
Music:  art/audio/M_<Name>.wav (from art/tools/music.py) -> /Game/KS/Audio/M_<Name>, set to loop.
        Brought in again whenever the WAV is newer than the asset.

Only what is missing or wrong is imported. -KSNewPictures brings every face PNG in again and remakes its material;
-KSReimport brings every voice line in again.
"""
import glob, os
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FOLDERS = {'sergeant-mullet-v2': 'SergeantMulletV2', 'sir-lasercorn': 'SirLasercorn', 'aerobics-annihilator': 'AerobicsAnnihilator',
           'grandma-thunder': 'GrandmaThunder', 'count-hairspray': 'CountHairspray', 'turbotron': 'Turbotron', 'channel-zero': 'ChannelZero'}
REIMPORT = 'ksreimport' in unreal.SystemLibrary.get_command_line().lower()
NEWPICS = 'ksnewpictures' in unreal.SystemLibrary.get_command_line().lower()
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def bring(path, folder):
    task = unreal.AssetImportTask()
    task.filename = path; task.destination_path = folder
    task.automated = True; task.replace_existing = True; task.save = True
    tools.import_asset_tasks([task])
    return EAL.load_asset('%s/%s' % (folder, os.path.splitext(os.path.basename(path))[0]))


faces = voices = 0
for src, folder in FOLDERS.items():
    base = '/Game/KoSekai/Characters/%s/Faces' % folder
    for png in sorted(glob.glob(os.path.join(ROOT, 'art', 'models', src, 'faces', 'T_*_Face_*.png'))):
        name = os.path.splitext(os.path.basename(png))[0]            # T_<Name>_Face_<expr>
        mi_name = 'MI' + name[1:]
        prefix = name.rsplit('_', 1)[0].replace('T_', 'MI_', 1)       # MI_<Name>_Face
        expr = name.rsplit('_', 1)[1]
        model = next((m for m in ('hurt', 'ko', 'neutral') if m != expr and EAL.does_asset_exist('%s/%s_%s' % (base, prefix, m))), None)
        if not model: continue                                        # this is one of the faces the others are copied from
        # Leave alone a face that is already right: its material shows this picture in exactly one slot.
        if EAL.does_asset_exist('%s/%s' % (base, name)) and EAL.does_asset_exist('%s/%s' % (base, mi_name)) and not NEWPICS:
            shown = [v.parameter_value.get_name() for v in EAL.load_asset('%s/%s' % (base, mi_name)).get_editor_property('texture_parameter_values') if v.parameter_value]
            if shown.count(name) == 1: continue
        tex = EAL.load_asset('%s/%s' % (base, name)) if EAL.does_asset_exist('%s/%s' % (base, name)) and not NEWPICS else bring(png, base)
        if not tex: unreal.log_error('KSIMPORT failed texture %s' % png); continue
        # The fighters' skeletons are scaled 0.01, so streamed textures arrive at tiny mips: never stream them.
        tex.set_editor_property('never_stream', True)
        tex.set_editor_property('max_texture_size', 2048)
        EAL.save_loaded_asset(tex)
        # The material is a copy of an existing face material with only its face picture swapped. The same material
        # also carries the normal and roughness pictures: those must stay (swapping them too turned hair to chrome).
        if EAL.does_asset_exist('%s/%s' % (base, mi_name)): EAL.delete_asset('%s/%s' % (base, mi_name))
        EAL.duplicate_asset('%s/%s_%s' % (base, prefix, model), '%s/%s' % (base, mi_name))
        mi = EAL.load_asset('%s/%s' % (base, mi_name))
        old = 'T%s_%s' % (prefix[2:], model)                          # the copied material's own face picture
        swapped = 0
        for value in mi.get_editor_property('texture_parameter_values'):
            if value.parameter_value and value.parameter_value.get_name() == old:
                unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, value.parameter_info.name, tex); swapped += 1
        if swapped != 1: unreal.log_error('KSIMPORT %s: %d pictures swapped, expected 1' % (mi_name, swapped))
        EAL.save_loaded_asset(mi)
        faces += 1
        unreal.log('KSIMPORT face %s' % mi_name)

for wav in sorted(glob.glob(os.path.join(ROOT, 'art', 'audio', 'voice', 'V_*.wav'))):
    name = os.path.splitext(os.path.basename(wav))[0]
    if EAL.does_asset_exist('/Game/KS/Voice/%s' % name) and not REIMPORT: continue
    if bring(wav, '/Game/KS/Voice'): voices += 1
    else: unreal.log_error('KSIMPORT failed sound %s' % wav)

music = 0
for wav in sorted(glob.glob(os.path.join(ROOT, 'art', 'audio', 'M_*.wav'))):
    name = os.path.splitext(os.path.basename(wav))[0]
    asset = os.path.join(ROOT, 'unreal', 'Content', 'KS', 'Audio', name + '.uasset')
    if os.path.exists(asset) and os.path.getmtime(asset) >= os.path.getmtime(wav) and not REIMPORT: continue
    sound = bring(wav, '/Game/KS/Audio')
    if not sound: unreal.log_error('KSIMPORT failed music %s' % wav); continue
    sound.set_editor_property('looping', True)
    EAL.save_loaded_asset(sound)
    music += 1

unreal.log('KSIMPORT done faces=%d voices=%d music=%d' % (faces, voices, music))
