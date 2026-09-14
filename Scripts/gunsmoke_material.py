"""
Builds M_GunSmoke: the first translucent, unlit, instanced material in this
project.

WHAT MAKES IT SMOKE AND NOT A GREY BALL, in the order it matters:

1. EROSION, NOT FADE. The card's alpha is (noise - threshold) rescaled, and the
   threshold CLIMBS with age. Multiplying alpha by (1 - age) gives a ball that
   goes see-through: a ghost. Eroding it against noise makes the rim break into
   holes and wisps and die ragged, which is what smoke does. This is the single
   line that decides whether the effect works.
2. LIT RIM, DARK CORE. Unlit, so the gradient is authored: the emissive runs
   from a near-black core to a bright rim by the dot of (pixel - puff centre)
   against the sun. Flat grey reads as a ball; twenty to one across the cloud
   reads as smoke.
3. SOFT PARTICLES. A depth fade kills the hard line where a card intersects the
   sea or the hull - that line is precisely what makes a card look like a card.
4. TWO NOISE SAMPLES at different scales, panned at different speeds. One alone
   slides visibly as a sheet.
5. PER-INSTANCE VARIATION. Each card offsets its noise and staggers its own
   death, so eighteen cards do not die on the same frame.

EVERY capability used here was PROBED first (Scripts/probe_material_api.py):
translucency, unlit, MP_OPACITY, MP_EMISSIVE_COLOR, DepthFade and
PerInstanceRandom are all present and all stick. No material in this project had
ever used any of them.

And the two properties that decide everything are read BACK and asserted, not
set through the project's sp() helper: that helper swallows a failure into a
WARN line, and run_py.ps1 only fails on a Python exception - so a material that
silently stayed BLEND_OPAQUE would print "ok" and then render as a solid grey
plane.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT = "/Game/Materials/M_GunSmoke"
TEX_DIR = "/Game/Textures"
A_IN, B_IN = ["A"], ["B"]
IN1 = ["", "Input", "VectorInput"]

FAILURES = []


def L(m):
    unreal.log("SMOKEMAT " + m)


def fail(m):
    FAILURES.append(m)
    unreal.log_error("SMOKEMAT FAIL " + m)


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
    mat = AT.create_asset("M_GunSmoke", "/Game/Materials", unreal.Material,
                          unreal.MaterialFactoryNew())

    # ---- the three properties the whole effect depends on, asserted --------
    # TRANSLUCENT. Additive was the reasoned choice - thirty cards in one
    # instanced component are not depth-sorted, and addition is commutative, so
    # an additive blend has no sorting problem to have. It was also, measured,
    # never drawn: after the usage flag was fixed, every additive build rendered
    # nothing at all while every translucent build rendered the cards. The
    # argument was good and the engine disagreed with it, so the engine wins.
    #
    # The unsorted-compositing worry is real but survivable here: the cards are
    # small relative to the cloud and each is faint, so mis-ordering costs a
    # little internal detail rather than the whole effect.
    #
    # (Kept for the record, because the reasoning is still right in general.)
    #
    # All thirty cards of a puff live in ONE instanced static mesh component.
    # Unreal sorts translucency per COMPONENT, not per instance, so thirty
    # alpha-blended cards inside one component are composited in whatever order
    # the instance buffer happens to hold. Unsorted alpha compositing does not
    # converge on anything: it flattens to a mush whose silhouette is the union
    # of the cards and whose interior has no depth. That is exactly what three
    # rounds of tuning opacity, card size and card count kept producing, and no
    # value of any of those was ever going to fix it.
    #
    # Addition is COMMUTATIVE. An additive blend gives the same result in any
    # order, so the sorting problem simply stops existing. The cost is that
    # black becomes invisible, which for sunlit powder smoke is very nearly the
    # truth anyway: what you see of a puff is the light it scatters.
    sp(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    sp(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    sp(mat, "two_sided", True)
    if mat.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        fail("blend_mode did not stick - the smoke would render as solid grey")
    if mat.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_UNLIT:
        fail("shading_model did not stick")
    if not mat.get_editor_property("two_sided"):
        fail("two_sided did not stick - cards vanish when seen from behind")

    # THE USAGE FLAG. A material must DECLARE that it may be used on instanced
    # static meshes; without it the engine silently substitutes the default
    # material and says so once, in a line that does not contain the words
    # "failed to compile":
    #
    #   Material ...MID_M_GunSmoke_0 missing bUsedWithInstancedStaticMeshes=True!
    #   Default Material will be used in game.
    #
    # Hours went into tuning opacity, card size, card count, noise source and
    # blend mode on a material that was never once on screen. What found it was
    # a one-variable probe - a flat RED emissive - which came back GREY. An
    # additive blend cannot draw darker than the sky behind it, and this one was
    # drawing darker; that was the contradiction that forced the question "is
    # this even my material?", which nothing else had asked.
    sp(mat, "used_with_instanced_static_meshes", True)
    if not mat.get_editor_property("used_with_instanced_static_meshes"):
        fail("used_with_instanced_static_meshes did not stick - the engine will "
             "quietly swap in the DEFAULT material and none of this graph will "
             "ever be drawn")

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
        sp(n, "group", "Smoke")
        return n

    def vector(name, c, x, y):
        n = expr(unreal.MaterialExpressionVectorParameter, x, y)
        sp(n, "parameter_name", name)
        sp(n, "default_value", c)
        sp(n, "group", "Smoke")
        return n

    def mul(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionMultiply, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def add(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionAdd, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def sub(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionSubtract, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def div(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionDivide, x, y)
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

    def tex(name, asset, uv, x, y):
        n = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        sp(n, "parameter_name", name)
        sp(n, "group", "Smoke")
        t = EAL.load_asset(TEX_DIR + "/" + asset)
        if t is None:
            fail("missing texture " + asset)
        sp(n, "texture", t)
        # Linear colour: the texture holds two numbers, and a sampler type that
        # disagrees with the asset's compression is the other half of the same
        # trap the _D suffix exists to avoid.
        sp(n, "sampler_type",
           unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        link(uv, "", n, ["UVs", "Coordinates"])
        return n

    # ------------------------------------------------------------- inputs
    uv = expr(unreal.MaterialExpressionTextureCoordinate, -2400, 0)
    # Set the tiling EXPLICITLY and read it back. A TextureCoordinate created
    # through Python does not necessarily arrive with the C++ default of 1.0,
    # and a tiling of zero makes every pixel of the card sample one single texel
    # - which renders as flat grey and looks exactly like "the texture is
    # broken" or "the erosion maths is wrong". Two iterations were spent on
    # those two guesses.
    sp(uv, "coordinate_index", 0)
    sp(uv, "u_tiling", 1.0)
    sp(uv, "v_tiling", 1.0)
    L("TextureCoordinate tiling readback u=%s v=%s index=%s"
      % (uv.get_editor_property("u_tiling"), uv.get_editor_property("v_tiling"),
         uv.get_editor_property("coordinate_index")))
    rand = expr(unreal.MaterialExpressionPerInstanceRandom, -2400, 200)
    time = scalar("SmokeTime", 0.0, -2400, 300)
    age = scalar("Age01", 0.0, -2400, 380)

    # The card's own soft disc: 1 at the middle, 0 at the rim.
    centre = const2(0.5, 0.5, -2200, 120)
    r = expr(unreal.MaterialExpressionDistance, -2050, 40)
    link(uv, "", r, A_IN)
    link(centre, "", r, B_IN)
    blob = sat(sub(const(1.0, -1900, 120), mul(r, const(2.0, -1900, 40),
                                               -1750, 40), -1600, 40), -1450, 40)

    # --------------------------------------------------------------- noise
    # Two sheets, different scale, panned in different directions, plus a
    # per-instance offset so eighteen cards are not eighteen copies. An offset
    # rather than a rotation: on a tiling texture it decorrelates just as well
    # and costs two nodes instead of twelve.
    def sheet(name, scale, sx, sy, ox, oy, x, y):
        base = mul(uv, const(scale, x, y + 80), x + 140, y)
        drift = mul(time, const2(sx, sy, x + 140, y + 160), x + 280, y + 120)
        jitter = mul(rand, const2(ox, oy, x + 140, y + 260), x + 280, y + 220)
        return add(add(base, drift, x + 420, y), jitter, x + 560, y)

    uv1 = sheet("a", 0.7, 0.035, 0.017, 0.37, 0.61, -2200, 400)
    uv2 = sheet("b", 1.9, -0.019, 0.026, 0.83, 0.29, -2200, 800)
    if os.environ.get("PS_SMOKE_NOISE", "proc") == "proc":
        # PROCEDURAL, not a texture. The textured version sampled dead flat at
        # exactly the texture's mean value, through five ruled-out causes
        # (channel packing, sRGB, grayscale compression, coordinate tiling,
        # streaming). Taking the texture out of the question entirely settles
        # whether the fault was ever in the sampling: a Noise node needs no UVs,
        # no import settings and no streaming.
        def pnoise(scale, levels, x, y):
            n = expr(unreal.MaterialExpressionNoise, x, y)
            sp(n, "scale", scale)
            sp(n, "levels", levels)
            sp(n, "output_min", 0.0)
            sp(n, "output_max", 1.0)
            sp(n, "turbulence", True)
            # The Position input is left UNCONNECTED on purpose: unconnected, a
            # Noise node uses absolute world position, which is exactly what is
            # wanted here - the cloud is carved out of a volume of noise that the
            # cards move through, instead of every card carrying its own copy of
            # one pattern. (The pin is not called "Position"; the hard assert in
            # this file caught that rather than letting it pass as a WARN.)
            return n
        # A weighted SUM, not a product. Two turbulence fields each average
        # about 0.35; multiplying them gives 0.12, and the erosion threshold
        # started at 0.36 - so the alpha was zero everywhere from the first
        # frame and the smoke was mathematically invisible before it was ever
        # drawn. Measured rather than assumed: with the noise wired straight to
        # an ADDITIVE emissive and opacity forced to 1, the cards still barely
        # brightened the frame, which is what a value near 0.1 looks like.
        # A sum keeps the mean where the threshold ramp expects it.
        n1 = pnoise(0.0035, 3, -1500, 400)
        n2 = pnoise(0.0140, 2, -1500, 800)
        noise = add(mul(n1, const(0.62, -1300, 480), -1200, 400),
                    mul(n2, const(0.38, -1300, 880), -1200, 800), -1050, 560)
        L("noise source: PROCEDURAL (world-space)")
    else:
        t1 = tex("SmokeNoise", "T_Smoke_D", uv1, -1500, 400)
        t2 = tex("SmokeNoiseB", "T_Smoke_D", uv2, -1500, 800)
        # R is the billow, G the curd: shape times froth.
        noise = mul(t1, t2, -1200, 500, ao="R", bo="G")
        L("noise source: TEXTURE")

    # -------------------------------------------------------- the erosion
    mixed = lerp(blob, mul(blob, noise, -1100, 100), const(0.75, -1100, 220),
                 -950, 60)

    # Threshold climbs with age, staggered per card so they do not all die on
    # the same frame.
    t_lo = scalar("ThresholdStart", 0.10, -1500, 1200)
    t_hi = scalar("ThresholdEnd", 0.88, -1500, 1280)
    stagger = add(const(0.75, -1500, 1360),
                  mul(rand, const(0.5, -1500, 1440), -1350, 1400), -1200, 1360)
    threshold = mul(lerp(t_lo, t_hi, age, -1200, 1240), stagger, -1000, 1280)

    soft = scalar("EdgeSoftness", 0.28, -1000, 1400)
    alpha = sat(div(sub(mixed, threshold, -800, 60), soft, -650, 60), -500, 60)

    # SOFT PARTICLES: without this a card crossing the sea draws a hard line
    # across it, and a hard line is exactly what says "this is a quad".
    fade = expr(unreal.MaterialExpressionDepthFade, -800, 300)
    sp(fade, "fade_distance_default", 220.0)
    link(alpha, "", fade, ["Opacity", "In Opacity", "InOpacity"])

    # PS_SMOKE_DEBUG=noise puts the sampled noise straight onto the emissive and
    # forces opacity to 1. If the card then shows the texture, sampling works and
    # the fault is downstream in the erosion arithmetic; if it shows flat grey,
    # the sample itself is the problem. One variable, one run, no guessing -
    # which is what I should have done before the last two iterations.
    if os.environ.get("PS_SMOKE_DEBUG") == "red":
        # Is the thing on screen even being drawn by THIS material? An additive
        # blend can only ever brighten, yet the puff renders DARKER than the sky
        # behind it - which additive cannot do. Either the material is not the
        # one in use, or the shape is not the cards. A saturated red settles it
        # in one run: red means mine, grey means something else entirely.
        # A VectorParameter, not a Constant3Vector. The red probe drew BLACK
        # cards, and an emissive of 6.0 cannot be black - which means
        # `sp(node, "constant", ...)` had silently failed and the sp() helper
        # swallowed it into a WARN. The probe meant to test the material was
        # itself broken, and it cost a wrong conclusion ("the material is not on
        # screen") that happened to be right for a different reason.
        red = vector("ProbeRed", unreal.LinearColor(6.0, 0.0, 0.0, 1.0), -300, 0)
        MEL.connect_material_property(red, "",
                                      unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.connect_material_property(const(1.0, -300, 200), "",
                                      unreal.MaterialProperty.MP_OPACITY)
        MEL.recompile_material(mat)
        EAL.save_asset(MAT)
        L("DEBUG build: flat RED emissive, opacity = 1")
        return

    if os.environ.get("PS_SMOKE_DEBUG") == "alpha":
        # ALPHA, the one number in this graph that has never been looked at.
        # Everything upstream of it has been verified; everything downstream is
        # a multiply. If this comes back black, the erosion is still killing the
        # card; if it shows a pattern, the fault is in the final opacity.
        MEL.connect_material_property(
            mul(alpha, vector("ProbeGain", unreal.LinearColor(4, 4, 4, 1), -300, 120),
                -150, 60), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.connect_material_property(const(1.0, -300, 260), "",
                                      unreal.MaterialProperty.MP_OPACITY)
        MEL.recompile_material(mat)
        EAL.save_asset(MAT)
        L("DEBUG build: alpha x4 -> emissive, opacity = 1")
        return

    if os.environ.get("PS_SMOKE_DEBUG") == "noise":
        MEL.connect_material_property(noise, "",
                                      unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.connect_material_property(const(1.0, -300, 400), "",
                                      unreal.MaterialProperty.MP_OPACITY)
        MEL.recompile_material(mat)
        EAL.save_asset(MAT)
        L("DEBUG build: noise -> emissive, opacity = 1")
        return

    opacity_scale = scalar("OpacityScale", 0.34, -500, 300)
    opacity = mul(fade, opacity_scale, -300, 200)
    if not MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
        fail("could not connect MP_OPACITY")

    # ------------------------------------------------------------- colour
    # Unlit, so the light is painted: dark core, bright rim towards the sun.
    wp = expr(unreal.MaterialExpressionWorldPosition, -2400, 1700)
    puff = vector("PuffCentre", unreal.LinearColor(0, 0, 0, 0), -2400, 1820)
    sun = vector("SunDir", unreal.LinearColor(0.4, 0.75, 0.53, 0), -2400, 1900)
    out = expr(unreal.MaterialExpressionNormalize, -2000, 1700)
    link(sub(wp, puff, -2200, 1700), "", out, IN1)
    d = expr(unreal.MaterialExpressionDotProduct, -1800, 1700)
    link(out, "", d, A_IN)
    link(sun, "", d, B_IN)
    facing = sat(add(mul(d, const(0.5, -1650, 1780), -1500, 1700),
                     const(0.5, -1500, 1780), -1350, 1700), -1200, 1700)

    core = vector("CoreColor", unreal.LinearColor(0.055, 0.057, 0.062, 1), -1500, 1900)
    lit = vector("LitColor", unreal.LinearColor(0.78, 0.76, 0.70, 1), -1500, 1990)
    # Per-card brightness, so thirty cards are not thirty copies of one grey.
    # Without it the mass is uniform and reads as a single surface no matter how
    # well the individual cards are shaped.
    shade = add(const(0.72, -1200, 1980),
                mul(rand, const(0.62, -1200, 2060), -1050, 2020), -900, 1990)
    # An additive blend already computes Dest + Emissive * Opacity, so the
    # erosion alpha must NOT also be multiplied into the colour here: doing both
    # squares it, and 0.085 squared is 0.007 a card - thirty of those are
    # invisible, which is exactly what the first additive build rendered.
    colour = mul(lerp(core, lit, facing, -900, 1800), shade, -700, 1830)
    if not MEL.connect_material_property(colour, "",
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
    raise RuntimeError("SMOKEMAT %d failures: %s" % (len(FAILURES), FAILURES[:4]))
L("DONE with no failures")




