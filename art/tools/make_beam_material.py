"""Makes /Game/KS/Arena/M_KS_Beam, the material of the arena's light beams and lasers. Runs inside the editor, without a window:

    UnrealEditor-Cmd unreal/KoSekai.uproject -run=pythonscript -script=$PWD/art/tools/make_beam_material.py

A beam is light seen in hazy air: additive, unlit, drawn from both sides. The mesh (a cone for a lamp, a thin box for
a laser) only says where the beam is; how bright each point of it is comes from where the point lies:

  - across the beam: brightest where you look through the middle of it, nothing at its edge. Worked out from the
    beam's own axis (parameters Tip and Axis, set by KSWorld::AimBeam), not from the mesh's normals: the first
    version used those, and the engine cone's normals made the narrow end, where a beam is brightest, vanish. Without
    any such term the beams were flat wedges of one colour (player, 2026-11-13: "lights done with meshes look bad").
  - along the beam: hot at the lamp, gone at Length.
  - haze: a slow drifting unevenness, so the air in the beam is not one flat tone.
  - where the beam meets the floor, the crowd or a fighter it fades into them instead of ending in a hard line.

Parameters: Color, Glow, Tip (world position of the lamp), Axis (unit vector along the beam), Length, Soft (how
quickly the edge falls off; higher is a narrower-looking beam), Haze (0 = even, 1 = strongly uneven).
"""
import unreal

PATH, NAME = '/Game/KS/Arena', 'M_KS_Beam'
EAL, MEL = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
if EAL.does_asset_exist('%s/%s' % (PATH, NAME)): EAL.delete_asset('%s/%s' % (PATH, NAME))
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('two_sided', True)

ROW = [0]


def node(kind, **props):
    ROW[0] += 1
    e = MEL.create_material_expression(mat, kind, -1800 + 170 * (ROW[0] % 9), 140 * (ROW[0] // 9))
    for k, v in props.items(): e.set_editor_property(k, v)
    return e


def op(kind, first, second=None, pa='A', pb='B', **props):
    e = node(kind, **props)
    MEL.connect_material_expressions(first, '', e, pa)
    if second is not None: MEL.connect_material_expressions(second, '', e, pb)
    return e


def one(kind, source, **props):
    e = node(kind, **props)
    MEL.connect_material_expressions(source, '', e, '')
    return e


E = unreal
mul = lambda a, b: op(E.MaterialExpressionMultiply, a, b)
sub = lambda a, b: op(E.MaterialExpressionSubtract, a, b)
dot = lambda a, b: op(E.MaterialExpressionDotProduct, a, b)
const = lambda v: node(E.MaterialExpressionConstant, r=v)
rgb = lambda e: one(E.MaterialExpressionComponentMask, e, r=True, g=True, b=True, a=False)

color = node(E.MaterialExpressionVectorParameter, parameter_name='Color', default_value=unreal.LinearColor(1, 1, 1, 1))
glow = node(E.MaterialExpressionScalarParameter, parameter_name='Glow', default_value=1.0)
tip = rgb(node(E.MaterialExpressionVectorParameter, parameter_name='Tip', default_value=unreal.LinearColor(0, 0, 0, 0)))
axis = rgb(node(E.MaterialExpressionVectorParameter, parameter_name='Axis', default_value=unreal.LinearColor(0, 0, -1, 0)))
length = node(E.MaterialExpressionScalarParameter, parameter_name='Length', default_value=1000.0)
soft = node(E.MaterialExpressionScalarParameter, parameter_name='Soft', default_value=1.6)
haze = node(E.MaterialExpressionScalarParameter, parameter_name='Haze', default_value=0.45)

where = node(E.MaterialExpressionWorldPosition)
from_tip = sub(where, tip)
along = dot(from_tip, axis)                                        # cm from the lamp, measured along the beam
out_from_axis = one(E.MaterialExpressionNormalize, sub(from_tip, mul(axis, along)))
# The line of sight with its part along the beam taken out: looking down the middle of the beam it is in line with
# "out from the axis" (1), at the beam's edge square to it (0).
view = node(E.MaterialExpressionCameraVectorWS)
view_across = one(E.MaterialExpressionNormalize, sub(view, mul(axis, dot(view, axis))))
through = one(E.MaterialExpressionAbs, dot(out_from_axis, view_across))
across = op(E.MaterialExpressionPower, through, soft, 'Base', 'Exponent')

# Along: (1 - t) squared.
t = one(E.MaterialExpressionSaturate, op(E.MaterialExpressionDivide, along, length))
left = one(E.MaterialExpressionOneMinus, t)
fade = mul(left, left)

# Haze: the engine's small tiling noise, stretched until a grain of it is the size of a head, drifting slowly.
noise = node(E.MaterialExpressionTextureSample, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
noise.set_editor_property('texture', unreal.load_asset('/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq'))
flat = one(E.MaterialExpressionComponentMask, where, r=True, g=True, b=False, a=False)
height = one(E.MaterialExpressionComponentMask, where, r=False, g=False, b=True, a=False)
spot = op(E.MaterialExpressionAdd, mul(flat, const(0.0011)), mul(height, const(0.0007)))
drift = mul(node(E.MaterialExpressionTime), const(0.012))
MEL.connect_material_expressions(op(E.MaterialExpressionAdd, spot, drift), '', noise, 'UVs')
uneven = op(E.MaterialExpressionLinearInterpolate, const(1.0), mul(one(E.MaterialExpressionComponentMask, noise, r=True, g=False, b=False, a=False), const(1.6)), 'A', 'B')
MEL.connect_material_expressions(haze, '', uneven, 'Alpha')

# Into whatever it meets.
meet = node(E.MaterialExpressionDepthFade, fade_distance_default=70.0)

out = mul(mul(mul(mul(mul(color, glow), across), fade), uneven), meet)
MEL.connect_material_property(out, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.recompile_material(mat)
EAL.save_loaded_asset(mat)
with open(unreal.Paths.project_saved_dir() + 'beam_material.txt', 'w') as f: f.write('KSIMPORT beam material made: %d nodes\n' % ROW[0])
unreal.log('KSIMPORT beam material made')
