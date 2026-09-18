"""
Builds M_AimMark: the lines the aiming marks are drawn with.

WHY NOT JUST REUSE M_ShotTrail, which is the same kind of surface: because the
trail takes its COLOUR from a material parameter and its ALPHA from the vertex,
and the aim marks need three different colours in one mesh - the faint blue of
the carriage stops, the warm white of the lay, and the amber the lay turns when
the guns are hard against the stop. One parameter cannot be three colours.

So here the vertex carries both: RGB is the tint, A is the strength. Setting the
trail's parameter to white and hoping the vertices would tint it was tried first
and drew BLACK LINES, which is this project's oldest trap wearing a new hat - see
the unit note below. White is 1.0, and 1.0 against this scene's white point is
nothing.

THE UNIT RULE, which has now cost two features: Config/DefaultEngine.ini enables
ExtendDefaultLuminanceRange and fences auto-exposure to EV100 12.5-16, so the
scene's white point is at least 2^12.5 = 5793 cd/m2. An emissive authored around
1 arrives at two ten-thousandths of white and the filmic toe finishes it. Vertex
colour is stored in eight bits and clamps at 1, so it CANNOT carry candelas - the
brightness has to be a scalar parameter applied after it, which is exactly what
AimBright is.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT = "/Game/Materials/M_AimMark"
A_IN, B_IN = ["A"], ["B"]
IN1 = ["", "Input", "VectorInput"]

FAILURES = []


def L(m):
    unreal.log("AIMMAT " + m)


def fail(m):
    FAILURES.append(m)
    unreal.log_error("AIMMAT FAIL " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s: %s" % (n, str(e)[:90]))
        return False


def build():
    if EAL.does_asset_exist(MAT):
        EAL.delete_asset(MAT)
    mat = AT.create_asset("M_AimMark", "/Game/Materials", unreal.Material,
                          unreal.MaterialFactoryNew())

    sp(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    if mat.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        fail("blend_mode did not stick")
    sp(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    if mat.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_UNLIT:
        fail("shading_model did not stick")
    sp(mat, "two_sided", True)
    sp(mat, "translucency_pass", unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    # Fog off: measured on the trail, vertex fog replaces a translucent surface's
    # colour with the fog's and every tint came out the same sooty grey.
    sp(mat, "use_translucency_vertex_fog", False)
    # DEPTH TESTED, and the first version was not. Switched off so the swell
    # could not occlude the marks, it also let them draw straight through the
    # ship's own hull and rigging, which reads as a bug however right the
    # geometry is. The ride height above the surface handles the chop; hulls
    # are meant to hide what is behind them.
    sp(mat, "disable_depth_test", False)

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def link(a, ao, b, pins):
        for p in pins:
            if MEL.connect_material_expressions(a, ao, b, p):
                return True
        fail("link %s -> %s %s" % (a.get_name(), b.get_name(), pins))
        return False

    def const(v, x, y):
        n = expr(unreal.MaterialExpressionConstant, x, y)
        sp(n, "r", v)
        back = n.get_editor_property("r")
        if abs(back - v) > 1e-6:
            fail("Constant %.3f did not stick, reads %.3f" % (v, back))
        return n

    def mul(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionMultiply, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def sub(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionSubtract, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def sat(a, x, y, ao=""):
        n = expr(unreal.MaterialExpressionClamp, x, y)
        sp(n, "min_default", 0.0)
        sp(n, "max_default", 1.0)
        link(a, ao, n, IN1 + ["Input"])
        return n

    vcol = expr(unreal.MaterialExpressionVertexColor, -1600, 200)

    # A soft edge across the line's width, so it reads as a drawn mark and not
    # as a strip of tape laid on the sea. V runs 0..1 across, so |2v-1| is the
    # distance from the spine.
    uv = expr(unreal.MaterialExpressionTextureCoordinate, -1600, -100)
    sp(uv, "coordinate_index", 0)
    sp(uv, "u_tiling", 1.0)
    sp(uv, "v_tiling", 1.0)
    vonly = expr(unreal.MaterialExpressionComponentMask, -1450, -100)
    sp(vonly, "r", False)
    sp(vonly, "g", True)
    sp(vonly, "b", False)
    sp(vonly, "a", False)
    link(uv, "", vonly, IN1)
    shifted = sub(mul(vonly, const(2.0, -1300, -30), -1150, -100),
                  const(1.0, -1150, -30), -1000, -100)
    absv = expr(unreal.MaterialExpressionAbs, -850, -100)
    link(shifted, "", absv, IN1)
    across = sat(sub(const(1.0, -700, -170), absv, -550, -100), -400, -100)

    # OPACITY: the vertex alpha, softened across the width, scaled by one knob.
    strength = expr(unreal.MaterialExpressionScalarParameter, -700, 320)
    sp(strength, "parameter_name", "AimOpacity")
    sp(strength, "default_value", 1.0)
    sp(strength, "group", "Aim")
    opacity = sat(mul(mul(across, vcol, -250, 60, bo="A"), strength, -100, 120),
                  50, 120)
    if not MEL.connect_material_property(opacity, "",
                                         unreal.MaterialProperty.MP_OPACITY):
        fail("could not connect MP_OPACITY")

    # EMISSIVE: the vertex RGB is the tint, 0..1, and the scalar carries the
    # candelas. Vertex colour is eight bits and clamps at one, so it cannot hold
    # the brightness itself - that is the whole reason this split exists.
    bright = expr(unreal.MaterialExpressionScalarParameter, -700, 460)
    sp(bright, "parameter_name", "AimBright")
    sp(bright, "default_value", 5200.0)
    sp(bright, "group", "Aim")
    back = bright.get_editor_property("default_value")
    L("AimBright readback = %.0f cd/m2" % back)
    if back < 100.0:
        fail("AimBright is far below the scene white point - the marks render BLACK")

    if not MEL.connect_material_property(mul(vcol, bright, -250, 460), "",
                                         unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("could not connect MP_EMISSIVE_COLOR")

    MEL.recompile_material(mat)
    EAL.save_asset(MAT)

    for name in ("MP_EMISSIVE_COLOR", "MP_OPACITY"):
        node = MEL.get_material_property_input_node(
            mat, getattr(unreal.MaterialProperty, name))
        L("%s -> %s" % (name, node.get_name() if node else "NOTHING"))
        if node is None:
            fail(name + " came back unconnected from the saved asset")

    L("built, expressions=%d" % MEL.get_num_material_expressions(mat))
    L("blend=%s shading=%s twoSided=%s pass=%s fog=%s depthTest=%s"
      % (mat.get_editor_property("blend_mode"),
         mat.get_editor_property("shading_model"),
         mat.get_editor_property("two_sided"),
         mat.get_editor_property("translucency_pass"),
         mat.get_editor_property("use_translucency_vertex_fog"),
         not mat.get_editor_property("disable_depth_test")))


build()
if FAILURES:
    raise RuntimeError("AIMMAT %d failures: %s" % (len(FAILURES), FAILURES[:4]))
L("DONE with no failures")
