"""Brings the arena's Meshy set pieces into the game project. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/import_props.py [-KSReimport]

art/models/_props/<Name>/model_slim.glb (from art/arena/gen_props.py, then art/tools/slim_props.py) -> /Game/KS/Props/<Name>/SM_<Name> with its
material and textures. KSWorld.cpp (FKSBuilder::Prop) loads them by that name and fits them by their bounds, so a
model's size and where its middle is do not matter; which way it faces does (see the bounds in the log).

Meshy's models have up to 2.7 million triangles, so it is the slimmed copy model_slim.glb that is imported: run
art/tools/slim_props.py first. Textures are capped at 1024.
The log is unreal/Saved/import_props.txt (unreal.log does not reach the terminal in a headless run).
"""
import glob, os
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
REIMPORT = 'ksreimport' in unreal.SystemLibrary.get_command_line().lower()
EAL = unreal.EditorAssetLibrary
NOTES = []


def note(text):
    unreal.log(text); NOTES.append(text)


def tris(mesh):
    try: return mesh.get_num_triangles(0)
    except Exception: return -1


done = 0
for glb in sorted(glob.glob(os.path.join(ROOT, 'art', 'models', '_props', '*', 'model_slim.glb'))):
    name = os.path.basename(os.path.dirname(glb))
    base = '/Game/KS/Props/' + name
    dest = '%s/SM_%s' % (base, name)
    uasset = os.path.join(ROOT, 'unreal', 'Content', 'KS', 'Props', name, 'SM_%s.uasset' % name)
    if os.path.exists(uasset) and os.path.getmtime(uasset) >= os.path.getmtime(glb) and not REIMPORT: continue
    if EAL.does_directory_exist(base): EAL.delete_directory(base)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    mesh_pipe = pipeline.get_editor_property('mesh_pipeline')
    for prop, value in (('import_static_meshes', True), ('import_skeletal_meshes', False), ('combine_static_meshes', True),
                        ('import_collision', False), ('collision', False), ('build_nanite', False), ('generate_lightmap_u_vs', False)):
        try: mesh_pipe.set_editor_property(prop, value)
        except Exception: pass
    try: pipeline.get_editor_property('animation_pipeline').set_editor_property('import_animations', False)
    except Exception: pass
    params = unreal.ImportAssetParameters()
    params.set_editor_property('is_automated', True)
    params.set_editor_property('replace_existing', True)
    params.set_editor_property('override_pipelines', [unreal.SoftObjectPath(pipeline.get_path_name())])
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(base, unreal.InterchangeManager.create_source_data(glb), params)

    assets = [a.split('.')[0] for a in EAL.list_assets(base, recursive=True)]
    meshes = [a for a in assets if isinstance(EAL.load_asset(a), unreal.StaticMesh)]
    if len(meshes) != 1:
        note('KSIMPORT FAILED %s: %d static meshes (%s)' % (name, len(meshes), ' '.join(assets))); continue
    if meshes[0] != dest and not EAL.rename_asset(meshes[0], dest):
        note('KSIMPORT FAILED %s: could not rename %s' % (name, meshes[0])); continue
    mesh = EAL.load_asset(dest)
    textures = 0
    for a in [x.split('.')[0] for x in EAL.list_assets(base, recursive=True)]:
        tex = EAL.load_asset(a)
        if isinstance(tex, unreal.Texture2D):
            tex.set_editor_property('max_texture_size', 1024)
            textures += 1
        EAL.save_asset(a)
    box = mesh.get_bounding_box()
    note('KSIMPORT prop %s triangles %d textures %d  min (%.0f %.0f %.0f) max (%.0f %.0f %.0f)' % (
        name, tris(mesh), textures, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z))
    done += 1


# Our own material in place of the imported one. Meshy's metal maps are close to mirror chrome: under the arena's lamps
# and without temporal anti-aliasing the risers glittered like tinsel and the floor lamps looked carved from ice.
# M_KS_Prop: the painted colour (Tex) and the normal map, a fixed dull metal (Rough, Metal), and Glow, a little of the
# paint as light of its own so that black steel in a dark hall and a landmark behind the stands can be seen at all.
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
KIT = ('Podium', 'Tower', 'Truss', 'Lamp', 'MovingHead', 'Post', 'Barrier', 'Speakers', 'Scoreboard')
MAT = '/Game/KS/Props/M_KS_Prop'
if REIMPORT and EAL.does_asset_exist(MAT): EAL.delete_asset(MAT)
if not EAL.does_asset_exist(MAT):
    mat = tools.create_asset('M_KS_Prop', '/Game/KS/Props', unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('two_sided', True)

    def node(kind, x, y, **props):
        e = MEL.create_material_expression(mat, kind, x, y)
        for k, v in props.items(): e.set_editor_property(k, v)
        return e

    tex = node(unreal.MaterialExpressionTextureSampleParameter2D, -700, 0, parameter_name='Tex')
    nrm = node(unreal.MaterialExpressionTextureSampleParameter2D, -700, 300, parameter_name='Normal',
               texture=unreal.load_asset('/Engine/EngineMaterials/DefaultNormal'), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    rough = node(unreal.MaterialExpressionScalarParameter, -700, 600, parameter_name='Rough', default_value=0.62)
    metal = node(unreal.MaterialExpressionScalarParameter, -700, 700, parameter_name='Metal', default_value=0.25)
    glow = node(unreal.MaterialExpressionScalarParameter, -700, 800, parameter_name='Glow', default_value=0.1)
    lit = node(unreal.MaterialExpressionMultiply, -350, 800)
    tint = node(unreal.MaterialExpressionVectorParameter, -700, 150, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    paint = node(unreal.MaterialExpressionMultiply, -500, 50)
    MEL.connect_material_expressions(tex, 'RGB', paint, 'A'); MEL.connect_material_expressions(tint, '', paint, 'B')
    MEL.connect_material_expressions(paint, '', lit, 'A'); MEL.connect_material_expressions(glow, '', lit, 'B')
    MEL.connect_material_property(paint, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(nrm, 'RGB', unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(metal, '', unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(lit, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    note('KSIMPORT prop material made')
mat = EAL.load_asset(MAT)
for folder in sorted(glob.glob(os.path.join(ROOT, 'unreal', 'Content', 'KS', 'Props', '*', ''))):
    name = os.path.basename(os.path.dirname(folder)); base = '/Game/KS/Props/' + name
    mesh = EAL.load_asset('%s/SM_%s' % (base, name))
    if not mesh: continue
    mi_path = '%s/MI_%s' % (base, name)
    if EAL.does_asset_exist(mi_path) and not REIMPORT and mesh.get_material(0) and mesh.get_material(0).get_name() == 'MI_' + name: continue
    if EAL.does_asset_exist(mi_path): EAL.delete_asset(mi_path)
    inside = [x.split('.')[0] for x in EAL.list_assets(base, recursive=True)]

    def find(ending): return next((EAL.load_asset(x) for x in inside if x.endswith(ending)), None)

    paint, normal = find('_texture_0'), find('_texture_2')
    if name in KIT and paint and not normal: normal = paint            # a kit model whose normal map is already gone: it is not used
    # Meshy's files list their pictures as paint, metal, normal. The normal map has to be stored as one for the
    # material's sampler to take it.
    if not paint or not normal: note('KSIMPORT FAILED %s: textures missing' % name); continue
    if name not in KIT and normal.get_editor_property('compression_settings') != unreal.TextureCompressionSettings.TC_NORMALMAP:
        normal.set_editor_property('srgb', False)
        normal.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        EAL.save_loaded_asset(normal)
    mi = tools.create_asset('MI_' + name, base, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, mat)
    MEL.set_material_instance_texture_parameter_value(mi, 'Tex', paint)
    # The kit goes without its normal map and nearly matt: the risers' perforated panels and non-slip decks are fine
    # patterns that sparkled white under the lamps (the game has no temporal anti-aliasing). The riser, of which
    # there are a hundred behind the crowd, is darkened as well, so that the people stand out and not the steel.
    kit = name in KIT
    if not kit: MEL.set_material_instance_texture_parameter_value(mi, 'Normal', normal)
    MEL.set_material_instance_scalar_parameter_value(mi, 'Glow', 0.06 if kit else 0.16)
    MEL.set_material_instance_scalar_parameter_value(mi, 'Metal', 0.1)
    MEL.set_material_instance_scalar_parameter_value(mi, 'Rough', 0.88 if kit else 0.7)
    shade = {'Podium': 0.14, 'Truss': 0.5, 'MovingHead': 0.6}.get(name, 1.0)
    MEL.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(shade, shade, shade, 1))
    mesh.set_material(0, mi)
    EAL.save_loaded_asset(mi); EAL.save_loaded_asset(mesh)
    # The imported material and its metal map are not used any more.
    for gone in [x for x in inside if x.endswith('_material_0') or x.endswith('_texture_1') or (kit and x.endswith('_texture_2'))]: EAL.delete_asset(gone)
    note('KSIMPORT prop %s wears MI_%s' % (name, name))

note('KSIMPORT props done: %d' % done)
with open(os.path.join(ROOT, 'unreal', 'Saved', 'import_props.txt'), 'w') as f: f.write('\n'.join(NOTES) + '\n')
