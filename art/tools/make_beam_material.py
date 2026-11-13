"""Makes /Game/KS/Arena/M_KS_Beam, the material of the arena's light beams. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/make_beam_material.py

A beam is a cone of light drawn as glowing air: additive, unlit, seen from both sides, brightest at the lamp
(parameter Tip, a world position) and fading out over Length. Parameters Color and Glow set its look.
"""
import unreal

PATH, NAME = '/Game/KS/Arena', 'M_KS_Beam'
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
if EAL.does_asset_exist('%s/%s' % (PATH, NAME)): EAL.delete_asset('%s/%s' % (PATH, NAME))
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('two_sided', True)


def node(kind, x, y, **props):
    e = MEL.create_material_expression(mat, kind, x, y)
    for k, v in props.items(): e.set_editor_property(k, v)
    return e


def mul(a, b, x, y):
    m = node(unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(a, '', m, 'A'); MEL.connect_material_expressions(b, '', m, 'B')
    return m


color = node(unreal.MaterialExpressionVectorParameter, -900, 0, parameter_name='Color', default_value=unreal.LinearColor(1, 1, 1, 1))
glow = node(unreal.MaterialExpressionScalarParameter, -900, 200, parameter_name='Glow', default_value=1.0)
tip = node(unreal.MaterialExpressionVectorParameter, -900, 400, parameter_name='Tip', default_value=unreal.LinearColor(0, 0, 0, 0))
length = node(unreal.MaterialExpressionScalarParameter, -900, 600, parameter_name='Length', default_value=1000.0)
# Soft sides: the cone is brightest where it is seen face on and vanishes where its surface turns edge on.
fresnel = node(unreal.MaterialExpressionFresnel, -900, 800, exponent=1.5, base_reflect_fraction=0.0)
middle = node(unreal.MaterialExpressionOneMinus, -700, 800)
MEL.connect_material_expressions(fresnel, '', middle, '')
middle = mul(middle, middle, -550, 800)
# Fades with distance from the lamp.
where = node(unreal.MaterialExpressionWorldPosition, -900, 1000)
dist = node(unreal.MaterialExpressionDistance, -700, 1000)
MEL.connect_material_expressions(where, '', dist, 'A'); MEL.connect_material_expressions(tip, '', dist, 'B')
along = node(unreal.MaterialExpressionDivide, -550, 1000)
MEL.connect_material_expressions(dist, '', along, 'A'); MEL.connect_material_expressions(length, '', along, 'B')
sat = node(unreal.MaterialExpressionSaturate, -400, 1000)
MEL.connect_material_expressions(along, '', sat, '')
fade = node(unreal.MaterialExpressionOneMinus, -250, 1000)
MEL.connect_material_expressions(sat, '', fade, '')
fade = mul(fade, fade, -100, 1000)

# The soft-sides term is left out of the product: with it the narrow end of the cone, where the beam should be
# brightest, drew as nothing at all. The beam is the cone's flat glow, fading with distance from the lamp.
out = mul(mul(color, glow, -500, 100), fade, -150, 300)
MEL.connect_material_property(out, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
unreal.log('KSIMPORT beam material made')
