"""
Builds M_MuzzleFlash: the short, very bright fire at the gun's mouth.

WHAT MAKES IT A FLASH AND NOT A SMALL SUN:

1. IT IS LIGHT, SO IT MAY SIMPLY FADE. The smoke next door is eroded against
   noise, because smoke is MATTER and matter dies ragged. A flash is burning gas
   giving off light; when the light stops, it stops. A square fade is right here
   and would be wrong there, and copying the smoke's erosion would have bought
   four noise samples for an effect that is on screen for six frames.

2. PHYSICAL UNITS, OR IT IS BLACK. This project fences EV100 between 12.5 and 16
   with ExtendDefaultLuminanceRange on, so the scene's white point is
   2^12.5 = 5793 cd/m2. An emissive authored anywhere near 1 - the value every
   tutorial uses - is crushed to black, and it looks exactly like a dead input
   rather than a dim one. A day went into that lesson on the gun smoke. The core
   here is 60000 cd/m2, roughly ten times the white point, which is what makes it
   bloom instead of merely being pale.

3. THE COLOUR IS THE POWDER'S, not white. Black powder burns yellow-orange; the
   core runs to near-white only because it is ten times over the white point and
   the tone mapper takes it there. Authoring white would give a photographic
   flashbulb.

4. TRANSLUCENT, NOT ADDITIVE - and that is not a preference. The gun smoke's
   script records the measurement: after the ISM usage flag was fixed, every
   additive build in this project rendered NOTHING at all while every translucent
   build rendered its cards. Addition is the better argument for a flash (it is
   light, and order would not matter) and the engine disagreed with it, so the
   engine wins. Re-deriving that would have cost the same hours twice.

5. THE USAGE FLAG. Without bUsedWithInstancedStaticMeshes the engine silently
   swaps in the DEFAULT material and says so in a line that does not contain the
   word "failed". That is what hid the smoke for hours.

Every property the effect depends on is read BACK and asserted rather than set
and hoped for: sp() swallows a failure into a WARN line, and run_py.ps1 only
fails on a Python exception, so a material that quietly stayed opaque would
print "ok" and then render as a solid grey plane.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT = "/Game/Materials/M_MuzzleFlash"

A_IN, B_IN = ["A"], ["B"]
IN1 = ["", "Input", "VectorInput"]

FAILURES = []


def L(m):
    unreal.log("FLASHMAT " + m)


def fail(m):
    FAILURES.append(m)
    unreal.log_error("FLASHMAT FAIL " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s: %s" % (n, str(e)[:80]))
        return False


def build():
    if EAL.does_asset_exist(MAT):
        EAL.delete_asset(MAT)
    mat = AT.create_asset("M_MuzzleFlash", "/Game/Materials", unreal.Material,
                          unreal.MaterialFactoryNew())

    sp(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    sp(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    sp(mat, "two_sided", True)
    sp(mat, "used_with_instanced_static_meshes", True)
    if mat.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        fail("blend_mode did not stick - the flash would render as a solid card")
    if mat.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_UNLIT:
        fail("shading_model did not stick - a lit flash is a contradiction")
    if not mat.get_editor_property("two_sided"):
        fail("two_sided did not stick - cards vanish seen from behind")
    if not mat.get_editor_property("used_with_instanced_static_meshes"):
        fail("used_with_instanced_static_meshes did not stick - the engine will "
             "quietly swap in the DEFAULT material and none of this is drawn")

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
        return n

    def const2(u, v, x, y):
        n = expr(unreal.MaterialExpressionConstant2Vector, x, y)
        sp(n, "r", u)
        sp(n, "g", v)
        return n

    def scalar(name, v, x, y):
        n = expr(unreal.MaterialExpressionScalarParameter, x, y)
        sp(n, "parameter_name", name)
        sp(n, "default_value", v)
        sp(n, "group", "Flash")
        return n

    def vector(name, c, x, y):
        n = expr(unreal.MaterialExpressionVectorParameter, x, y)
        sp(n, "parameter_name", name)
        sp(n, "default_value", c)
        sp(n, "group", "Flash")
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

    def lerp(a, b, al, x, y, ao="", bo="", alo=""):
        n = expr(unreal.MaterialExpressionLinearInterpolate, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        link(al, alo, n, ["Alpha"])
        return n

    # ---- the disc -----------------------------------------------------------
    # Distance from the card's centre, 0 in the middle and 1 at the edge of the
    # inscribed circle. Everything else is a function of this one number, which
    # is why the flash needs no texture at all: a radial falloff is exactly what
    # a ball of burning gas looks like at six frames of exposure.
    uv = expr(unreal.MaterialExpressionTextureCoordinate, -1500, 0)
    centred = sub(uv, const2(0.5, 0.5, -1500, 140), -1300, 0)
    radius = expr(unreal.MaterialExpressionLength, -1150, 0)
    link(centred, "", radius, IN1)
    # * 2 so the card's inscribed circle spans 0..1 rather than 0..0.5.
    r01 = sat(mul(radius, const(2.0, -1000, 120), -1000, 0), -870, 0)

    # ---- the age ------------------------------------------------------------
    # Age01 is written by the actor every tick, 0 at the muzzle and 1 at death.
    # The flash fades on the SQUARE of what is left: a linear fade reads as a
    # lamp being turned down, and powder does not do that.
    age = scalar("Age01", 0.0, -1500, 320)
    left = sat(sub(const(1.0, -1300, 300), age, -1150, 320), -1020, 320)
    left2 = mul(left, left, -880, 320)

    # ---- the opacity --------------------------------------------------------
    # Falls off from the centre and dies with the square of what is left. The
    # edge reaches zero on its own, so no separate soft-edge term is needed.
    falloff = sat(sub(const(1.0, -700, -120), r01, -560, -60), -430, -60)
    edge = mul(falloff, falloff, -300, -60)
    opacity = mul(edge, left2, -150, 60)

    # ---- the colour ---------------------------------------------------------
    # Core to rim, in candelas. The core is ten times the scene's white point
    # (5793 cd/m2 with EV100 fenced at 12.5), which is what makes it bloom
    # rather than merely read as pale yellow; the rim is under the white point,
    # so the flash has an edge instead of being a uniform blob.
    #
    # The two colours are 0..1 hues and the candelas live in a SEPARATE scalar,
    # deliberately: vertex colour and most colour pickers clamp at 1, and this
    # project has already lost a day to a brightness that could not be expressed
    # where it was written.
    core = vector("CoreTint", unreal.LinearColor(1.0, 0.92, 0.62, 1.0), -700, 420)
    rim = vector("RimTint", unreal.LinearColor(1.0, 0.45, 0.10, 1.0), -700, 600)
    tint = lerp(core, rim, r01, -450, 480)
    bright = scalar("Brightness", 60000.0, -450, 700)
    emissive = mul(tint, bright, -200, 520)

    if not MEL.connect_material_property(opacity, "",
                                         unreal.MaterialProperty.MP_OPACITY):
        fail("could not connect MP_OPACITY")
    if not MEL.connect_material_property(emissive, "",
                                         unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("could not connect MP_EMISSIVE_COLOR")

    MEL.recompile_material(mat)
    EAL.save_asset(MAT)
    L("built, expressions=%d" % MEL.get_num_material_expressions(mat))
    L("blend=%s shading=%s twoSided=%s usedWithISM=%s"
      % (mat.get_editor_property("blend_mode"),
         mat.get_editor_property("shading_model"),
         mat.get_editor_property("two_sided"),
         mat.get_editor_property("used_with_instanced_static_meshes")))


build()
if FAILURES:
    raise RuntimeError("FLASHMAT %d failures: %s" % (len(FAILURES), FAILURES[:4]))
L("DONE with no failures")
