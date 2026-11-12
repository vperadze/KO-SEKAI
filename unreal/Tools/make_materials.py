"""Creates the two materials every dummy model and level block uses.

Run once (already done; the .uasset files are committed):
  UnrealEditor-Cmd KoSekai.uproject -run=pythonscript -script=Tools/make_materials.py
"""
import unreal

PATH = "/Game/KS/Materials"
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def make(name, ghost):
    full = f"{PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    mat = tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    glow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 250)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 1.0 if ghost else 0.0)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 150)
    mel.connect_material_expressions(color, "", mul, "A")
    mel.connect_material_expressions(glow, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if ghost:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        opacity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 450)
        opacity.set_editor_property("parameter_name", "Opacity")
        opacity.set_editor_property("default_value", 0.45)
        mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    else:
        mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 400)
        rough.set_editor_property("r", 0.6)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log(f"KS material written: {full}")


make("M_KS_Solid", False)
make("M_KS_Ghost", True)
