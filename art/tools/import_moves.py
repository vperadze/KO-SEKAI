"""Brings the fighters' extra animation clips into the game project. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/import_moves.py

art/models/<fighter>/extra/extra.glb (from art/tools/add_moves.py; clips named Fun_A, Picked, Special...)
    -> /Game/KoSekai/Characters/<Folder>/animation_glb/SkeletalMeshes/animation_glb<Clip>, on the fighter's own skeleton.
Only the animations are taken: the fighter's mesh, materials and faces are not touched. A clip already in the game is
left alone unless -KSReimport is given.
"""
import json, os
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FOLDERS = {'sergeant-mullet-v2': 'SergeantMulletV2', 'sir-lasercorn': 'SirLasercorn', 'aerobics-annihilator': 'AerobicsAnnihilator',
           'grandma-thunder': 'GrandmaThunder', 'count-hairspray': 'CountHairspray', 'turbotron': 'Turbotron', 'channel-zero': 'ChannelZero'}
KEYS = ('Fun_A', 'Fun_B', 'Picked', 'Special', 'Win', 'Stagger', 'Shocked', 'Blown')
REIMPORT = 'ksreimport' in unreal.SystemLibrary.get_command_line().lower()
EAL = unreal.EditorAssetLibrary

done = 0
NOTES = []            # unreal.log does not reach the terminal in a headless run: the same lines go to a file


def note(text):
    unreal.log(text); NOTES.append(text)


for src, folder in FOLDERS.items():
    glb = os.path.join(ROOT, 'art', 'models', src, 'extra', 'extra.glb')
    if not os.path.exists(glb): continue
    base = '/Game/KoSekai/Characters/%s/animation_glb/SkeletalMeshes' % folder
    if all(EAL.does_asset_exist('%s/animation_glb%s' % (base, k)) for k in KEYS) and not REIMPORT: continue
    skeleton = EAL.load_asset(base + '/animation_glb_Skeleton')
    if not skeleton: unreal.log_error('KSIMPORT no skeleton for %s' % folder); continue
    temp = '/Game/KoSekai/Characters/%s/_extra' % folder
    if EAL.does_directory_exist(temp): EAL.delete_directory(temp)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    common = pipeline.get_editor_property('common_skeletal_meshes_and_animations_properties')
    common.set_editor_property('import_only_animations', True)
    common.set_editor_property('skeleton', skeleton)
    pipeline.get_editor_property('mesh_pipeline').set_editor_property('import_static_meshes', False)
    pipeline.get_editor_property('mesh_pipeline').set_editor_property('import_skeletal_meshes', False)
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    pipeline.get_editor_property('material_pipeline').get_editor_property('texture_pipeline').set_editor_property('import_textures', False)
    anim = pipeline.get_editor_property('animation_pipeline')
    anim.set_editor_property('import_animations', True)
    anim.set_editor_property('import_bone_tracks', True)

    params = unreal.ImportAssetParameters()
    params.set_editor_property('is_automated', True)
    params.set_editor_property('replace_existing', True)
    params.set_editor_property('override_pipelines', [unreal.SoftObjectPath(pipeline.get_path_name())])
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    manager.import_asset(temp, unreal.InterchangeManager.create_source_data(glb), params)

    found = [a for a in EAL.list_assets(temp, recursive=True) if isinstance(EAL.load_asset(a), unreal.AnimSequence)]
    note('KSIMPORT %s imported: %s' % (folder, ' '.join(a.split('/')[-1].split('.')[0] for a in found)))
    listed = json.load(open(glb[:-4] + '.json')) if os.path.exists(glb[:-4] + '.json') else []
    for path in found:
        name = path.split('/')[-1].split('.')[0]
        # A file with one clip names it after the file; with several, after the clip.
        key = next((k for k in sorted(KEYS, key=len, reverse=True) if name.endswith(k)), None) or (listed[0] if len(found) == 1 and len(listed) == 1 else None)
        if not key: continue
        dest = '%s/animation_glb%s' % (base, key)
        if EAL.does_asset_exist(dest): EAL.delete_asset(dest)
        if EAL.rename_asset(path.split('.')[0], dest):
            EAL.save_asset(dest); done += 1
        else: note('KSIMPORT could not move %s' % path)
    EAL.delete_directory(temp)

note('KSIMPORT moves done: %d clips' % done)
open(os.path.join(ROOT, 'unreal', 'Saved', 'import_moves.txt'), 'w').write('\n'.join(NOTES) + '\n')
