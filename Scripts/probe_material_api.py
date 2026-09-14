"""
Asks the engine what it actually supports, before a material is written against
it.

Every material in this project so far is opaque, lit, world-XY-projected, and
connects only base colour / normal / roughness / specular / metallic / WPO. Gun
smoke needs none of those things: it needs translucency, an unlit shading model,
MP_OPACITY, MP_EMISSIVE_COLOR, per-instance randomness and a depth fade - six
capabilities this project has never once used.

Writing three hundred lines against six assumptions and finding out at render
time is how the last two material defects happened. This probe costs one run.

It also checks the thing the reviewer flagged: `set_editor_property` for
blend_mode can FAIL and the project's own sp() helper swallows that into a WARN
line, so a material that silently stayed opaque would still print "ok" and then
render as a solid grey plane. Here the value is read BACK.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

PROBE = "/Game/Materials/M_ApiProbe"


def L(m):
    unreal.log("PROBE " + m)


def has(name):
    ok = hasattr(unreal, name)
    L("%-42s %s" % ("unreal." + name, "yes" if ok else "MISSING"))
    return ok


def main():
    L("--- expression classes ---")
    for n in ("MaterialExpressionTextureCoordinate",
              "MaterialExpressionDepthFade",
              "MaterialExpressionPerInstanceRandom",
              "MaterialExpressionPerInstanceCustomData",
              "MaterialExpressionSceneDepth",
              "MaterialExpressionCameraPositionWS",
              "MaterialExpressionObjectPositionWS",
              "MaterialExpressionDynamicParameter",
              "MaterialExpressionFresnel",
              "MaterialExpressionPower",
              "MaterialExpressionMax"):
        has(n)

    L("--- material properties ---")
    for n in ("MP_OPACITY", "MP_EMISSIVE_COLOR", "MP_OPACITY_MASK"):
        L("%-42s %s" % ("MaterialProperty." + n,
                        "yes" if hasattr(unreal.MaterialProperty, n) else "MISSING"))

    L("--- enums ---")
    for cls, val in (("BlendMode", "BLEND_TRANSLUCENT"),
                     ("MaterialShadingModel", "MSM_UNLIT")):
        e = getattr(unreal, cls, None)
        L("%-42s %s" % (cls + "." + val,
                        "yes" if e is not None and hasattr(e, val) else "MISSING"))

    # --- does a translucent unlit material actually STICK? -----------------
    if EAL.does_asset_exist(PROBE):
        EAL.delete_asset(PROBE)
    mat = AT.create_asset("M_ApiProbe", "/Game/Materials", unreal.Material,
                          unreal.MaterialFactoryNew())
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    except Exception as e:
        L("set blend_mode RAISED: %s" % str(e)[:90])
    try:
        mat.set_editor_property("shading_model",
                                unreal.MaterialShadingModel.MSM_UNLIT)
    except Exception as e:
        L("set shading_model RAISED: %s" % str(e)[:90])

    # Read BACK. This is the whole point of the probe.
    bm = mat.get_editor_property("blend_mode")
    sm = mat.get_editor_property("shading_model")
    L("blend_mode readback   = %s  (wanted BLEND_TRANSLUCENT)" % bm)
    L("shading_model readback= %s  (wanted MSM_UNLIT)" % sm)

    # And does connecting to OPACITY / EMISSIVE actually return true?
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 0)
    c.set_editor_property("r", 0.5)
    for prop in ("MP_OPACITY", "MP_EMISSIVE_COLOR"):
        p = getattr(unreal.MaterialProperty, prop, None)
        if p is None:
            continue
        ok = MEL.connect_material_property(c, "", p)
        L("connect_material_property %-18s -> %s" % (prop, ok))

    MEL.recompile_material(mat)
    EAL.save_asset(PROBE)
    L("DONE expressions=%d" % MEL.get_num_material_expressions(mat))


main()
