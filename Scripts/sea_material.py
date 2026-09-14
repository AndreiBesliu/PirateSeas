"""
Rebuilds M_Sea: the same six Gerstner waves, plus everything that makes water
look like water rather than like poured plastic.

What the first version had: the swell, one flat deep colour, roughness 0.06.
That is a mirror with a slow ripple in it, and a mirror is exactly what the
capture showed - a milky sheet with a smeared reflection.

What it has now, in the order the eye notices it:

  1. MICRO-DETAIL. Two tiling ripple normal maps panned across the surface at
     different scales and different speeds. This matters more than anything
     else in the file: between the crests of a six-wave swell the surface is
     geometrically FLAT, so a mirror is the physically correct answer to the
     question the old material asked. Real water is never flat at that scale -
     the centimetre chop is what breaks the reflection into glitter.
  2. FOAM on the crests, masked by a bubble texture so it breaks into clumps.
     Rough where it is white, which is the whole reason foam reads as foam and
     not as white paint: it stops reflecting.
  3. SCATTER. Crests are lit from within and troughs are not, so the colour
     runs from a deep blue in the hollows to a green-blue on the tops.
  4. A DISTANCE FADE on the detail, because a ripple that is a tenth of a pixel
     across at four kilometres is not detail, it is noise - it crawls and
     fizzes and costs a lot to do it.

Every new number is a named parameter, so it can be swept from an instance
without rebuilding the graph.
"""
import math
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT_DIR = "/Game/Materials"
TEX_DIR = "/Game/Textures"
SEA_MAT = MAT_DIR + "/M_Sea"
WAVES = 6
TWO_PI = 2.0 * math.pi


def L(m):
    unreal.log("SEAMAT " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN set %s: %s" % (n, str(e)[:90]))
        return False


IN1 = ["", "Input", "VectorInput"]
A_IN = ["A"]
B_IN = ["B"]


def build():
    if EAL.does_asset_exist(SEA_MAT):
        EAL.delete_asset(SEA_MAT)
    mat = AT.create_asset("M_Sea", MAT_DIR, unreal.Material,
                          unreal.MaterialFactoryNew())
    sp(mat, "two_sided", True)
    # World-space normals: the surface is a horizontal sheet, so its tangent
    # frame IS world XY, and that is what lets a tangent-space ripple map be
    # added straight onto the Gerstner slope with no basis transform.
    sp(mat, "tangent_space_normal", False)

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def link(a, a_out, b, pins):
        for p in pins:
            if MEL.connect_material_expressions(a, a_out, b, p):
                return True
        L("WARN link %s -> %s %s" % (a.get_name(), b.get_name(), pins))
        return False

    def const(v, x, y):
        n = expr(unreal.MaterialExpressionConstant, x, y)
        sp(n, "r", v)
        return n

    def scalar(name, v, x, y):
        n = expr(unreal.MaterialExpressionScalarParameter, x, y)
        sp(n, "parameter_name", name)
        sp(n, "default_value", v)
        sp(n, "group", "Sea")
        return n

    def vector(name, c, x, y):
        n = expr(unreal.MaterialExpressionVectorParameter, x, y)
        sp(n, "parameter_name", name)
        sp(n, "default_value", c)
        sp(n, "group", "Sea")
        return n

    def mul(a, ao, b, bo, x, y):
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

    def mask(src, x, y, r=True, g=True, b=False, a=False, so=""):
        n = expr(unreal.MaterialExpressionComponentMask, x, y)
        sp(n, "r", r); sp(n, "g", g); sp(n, "b", b); sp(n, "a", a)
        link(src, so, n, IN1)
        return n

    def sat(src, x, y, so=""):
        n = expr(unreal.MaterialExpressionClamp, x, y)
        sp(n, "min_default", 0.0)
        sp(n, "max_default", 1.0)
        link(src, so, n, IN1 + ["Input"])
        return n

    def lerp(a, b, alpha, x, y, ao="", bo="", alo=""):
        n = expr(unreal.MaterialExpressionLinearInterpolate, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        link(alpha, alo, n, ["Alpha"])
        return n

    def power(base, exp_node, x, y, bo=""):
        n = expr(unreal.MaterialExpressionPower, x, y)
        link(base, bo, n, ["Base"])
        link(exp_node, "", n, ["Exponent", "Exp"])
        return n

    def tex_param(name, asset, x, y, normal=False):
        n = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        sp(n, "parameter_name", name)
        sp(n, "group", "Sea")
        t = EAL.load_asset(TEX_DIR + "/" + asset)
        if t is None:
            L("MISSING texture %s" % asset)
        sp(n, "texture", t)
        if normal:
            sp(n, "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        else:
            sp(n, "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        return n

    def expr_dist(a, b, x, y):
        n = expr(unreal.MaterialExpressionDistance, x, y)
        link(a, "", n, A_IN)
        link(b, "", n, B_IN)
        return n

    def absn(src, x, y, so=""):
        n = expr(unreal.MaterialExpressionAbs, x, y)
        link(src, so, n, IN1)
        return n

    def maxn(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionMax, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def panner(uv, time, sx, sy, x, y):
        n = expr(unreal.MaterialExpressionPanner, x, y)
        sp(n, "speed_x", sx)
        sp(n, "speed_y", sy)
        link(uv, "", n, ["Coordinate"])
        link(time, "", n, ["Time"])
        return n

    # ------------------------------------------------------------ shared
    world = expr(unreal.MaterialExpressionWorldPosition, -3000, 0)
    world_xy = mask(world, -2800, 0)
    time = scalar("WaveTime", 0.0, -3000, 140)
    neg_one = const(-1.0, -3000, 220)

    # ------------------------------------------------------------ swell
    offsets, slopes = [], []
    for i in range(WAVES):
        y0 = -1400 + i * 420
        wave = vector("WaveA%d" % i, unreal.LinearColor(0, 0, 0, 0), -2600, y0)
        qk = scalar("WaveQK%d" % i, 0.0, -2600, y0 + 150)
        wave_xy = mask(wave, -2400, y0)

        dot = expr(unreal.MaterialExpressionDotProduct, -2200, y0)
        link(world_xy, "", dot, A_IN)
        link(wave_xy, "", dot, B_IN)

        wt = mul(wave, "B", time, "", -2200, y0 + 150)
        phase = sub(dot, wt, -2000, y0)

        sine = expr(unreal.MaterialExpressionSine, -1800, y0)
        sp(sine, "period", TWO_PI)
        link(phase, "", sine, IN1)
        cosine = expr(unreal.MaterialExpressionCosine, -1800, y0 + 150)
        sp(cosine, "period", TWO_PI)
        link(phase, "", cosine, IN1)

        height = mul(cosine, "", wave, "A", -1600, y0 + 150)
        swing = mul(sine, "", qk, "", -1600, y0)
        swing_neg = mul(swing, "", neg_one, "", -1450, y0)
        horizontal = mul(wave_xy, "", swing_neg, "", -1300, y0)

        off = expr(unreal.MaterialExpressionAppendVector, -1150, y0)
        link(horizontal, "", off, A_IN)
        link(height, "", off, B_IN)
        offsets.append(off)

        slope_scale = mul(sine, "", wave, "A", -1600, y0 + 300)
        slopes.append(mul(wave_xy, "", slope_scale, "", -1450, y0 + 300))

    total_offset = offsets[0]
    for i in range(1, WAVES):
        total_offset = add(total_offset, offsets[i], -900 + i * 25, -1400 + i * 90)
    MEL.connect_material_property(total_offset, "",
                                  unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    total_slope = slopes[0]
    for i in range(1, WAVES):
        total_slope = add(total_slope, slopes[i], -900 + i * 25, 200 + i * 90)

    # Crest height in centimetres, which is what both the foam and the scatter
    # key off. Taken from the SAME sum that displaces the vertex, so white
    # water cannot appear anywhere the surface is not actually heaped up.
    crest = mask(total_offset, -700, 700, r=False, g=False, b=True)

    # ------------------------------------------------- detail ripples
    # Distance fade first, because everything below is multiplied by it.
    depth = expr(unreal.MaterialExpressionPixelDepth, -2800, 1600)
    fade_far = scalar("DetailFadeCm", 45000.0, -2800, 1700)
    fade_raw = sat(div(depth, fade_far, -2600, 1600), -2450, 1600)
    near_w = const(1.0, -2600, 1760)
    far_w = scalar("DetailFarWeight", 0.18, -2600, 1830)
    fade = lerp(near_w, far_w, fade_raw, -2300, 1700)

    detail_strength = scalar("DetailStrength", 1.45, -2300, 1900)
    detail_gain = mul(fade, "", detail_strength, "", -2150, 1800)

    detail_xy = None
    # scale in cm per tile, pan speed in tiles per second
    SHEETS = [("T_WaterRipple0_N", "RippleScaleA", 1400.0, 0.013, 0.006, 0.62),
              ("T_WaterRipple1_N", "RippleScaleB", 430.0, -0.021, 0.017, 0.38)]
    for idx, (asset, sname, scale, sx, sy, weight) in enumerate(SHEETS):
        y0 = 2100 + idx * 500
        s = scalar(sname, scale, -2800, y0 + 120)
        uv = div(world_xy, s, -2600, y0)
        pan = panner(uv, time, sx, sy, -2400, y0)
        tex = tex_param("Ripple%d" % idx, asset, -2200, y0, normal=True)
        link(pan, "", tex, ["UVs", "Coordinates"])
        xy = mask(tex, -1900, y0)
        w = const(weight, -1900, y0 + 160)
        weighted = mul(xy, "", w, "", -1750, y0)
        detail_xy = weighted if detail_xy is None else add(detail_xy, weighted, -1600, y0)

    detail_final = mul(detail_xy, "", detail_gain, "", -1450, 2300)
    slope_with_detail = add(total_slope, detail_final, -1250, 2300)

    one = const(1.0, -1250, 2420)
    normal_vec = expr(unreal.MaterialExpressionAppendVector, -1050, 2340)
    link(slope_with_detail, "", normal_vec, A_IN)
    link(one, "", normal_vec, B_IN)
    normalised = expr(unreal.MaterialExpressionNormalize, -880, 2340)
    link(normal_vec, "", normalised, IN1)
    MEL.connect_material_property(normalised, "", unreal.MaterialProperty.MP_NORMAL)

    # ------------------------------------------------------------ foam
    # Where the swell is heaped above FoamStartCm, and only where the bubble
    # texture says there are bubbles. Without the texture the foam is a smooth
    # white band following the crest, which reads as paint.
    # Both of these are OVERWRITTEN from C++ at BeginPlay with values derived
    # from the wave set actually in play; the defaults here are only what an
    # opened material editor shows.
    foam_start = scalar("FoamStartCm", 110.0, -900, 900)
    foam_range = scalar("FoamRangeCm", 32.0, -900, 980)
    crest_over = sat(div(sub(crest, foam_start, -700, 900), foam_range, -560, 900),
                     -420, 900)

    fscale = scalar("FoamScaleCm", 1100.0, -900, 1120)
    fuv = div(world_xy, fscale, -700, 1100)
    fpan = panner(fuv, time, 0.008, -0.005, -560, 1100)
    ftex = tex_param("FoamMask", "T_Foam_M", -420, 1100)
    link(fpan, "", ftex, ["UVs", "Coordinates"])

    foam_gain = scalar("FoamGain", 1.5, -420, 1260)
    foam_raw = mul(mul(crest_over, "", ftex, "", -200, 950),
                   "", foam_gain, "", -60, 950)
    foam_sharp = scalar("FoamSharpness", 2.4, -200, 1100)
    crest_foam = power(sat(foam_raw, 80, 950), foam_sharp, 220, 950)

    # --------------------------------------------------------------- surf
    # White water where the sea meets the land. Until this, the water was
    # completely unaware that islands existed - it slid over a beach and up a
    # hillside without a fleck of white, which is what most gives away that the
    # two are separate objects that merely overlap.
    #
    # Eight islands, each packed by C++ as (X, Y, shore radius, shoal outer).
    # An ABSENT island is all zeros, and that has to fall out to zero surf
    # rather than to a division by zero: with shore=outer=0 the width below is
    # clamped to one centimetre and the ramp is saturated to 0 for any pixel
    # further out than nothing, which is every pixel.
    ISLANDS = 8
    # Where the breakers stand, as a fraction of the shoal's width out from the
    # beach, and how wide the band is in the same units.
    surf_peak_mid = scalar("SurfPeak", 0.12, -3000, 3100)
    surf_half = scalar("SurfHalfWidth", 0.30, -3000, 3180)

    # The SWASH: the line of breakers runs in and draws back, which is what a
    # beach actually does and what a static ring never does. Done by moving the
    # band's PEAK rather than by brightening it, so the white water travels
    # instead of blinking.
    #
    # The first attempt did neither. Its phase was
    #     -surf_curved / 900  -  WaveTime * 55
    # whose spatial term varies by 0.001 radians across the whole band - no
    # travel at all - while the time term runs at 55 rad/s, which is a 9 Hz
    # global flicker. It was a strobe, not a wave, and nothing in the capture
    # said so because a still frame cannot show a frequency.
    surf_period = scalar("SurfPeriodS", 7.0, -3000, 3260)
    swash_sin = expr(unreal.MaterialExpressionSine, -2800, 3260)
    sp(swash_sin, "period", 1.0)          # node computes sin(2*pi*x), x in cycles
    link(div(time, surf_period, -2900, 3260), "", swash_sin, IN1)
    surf_swash = scalar("SurfSwashAmount", 0.07, -3000, 3340)
    surf_peak = add(surf_peak_mid,
                    mul(swash_sin, "", surf_swash, "", -2700, 3300), -2550, 3260)

    surf = None
    for i in range(ISLANDS):
        y0 = 3200 + i * 260
        isle = vector("Isle%d" % i, unreal.LinearColor(0, 0, 0, 0), -2800, y0)
        isle_xy = mask(isle, -2600, y0)
        d = expr(unreal.MaterialExpressionDistance, -2400, y0)
        link(world_xy, "", d, A_IN)
        link(isle_xy, "", d, B_IN)

        # The third and fourth components come off the parameter's own NAMED
        # OUTPUT PINS, not off a ComponentMask. A VectorParameter's default
        # output is a float3, so masking .a compiles to "Not enough components
        # in float3 for component mask 0001" and the whole material falls back
        # to the grey checkerboard - which is exactly what the first capture
        # showed, a sea replaced by a chessboard. The wave code above already
        # used the pins; I had the pattern in front of me and did not follow it.
        width = maxn(sub(isle, isle, -2500, y0 + 120, ao="A", bo="B"),
                     const(1.0, -2500, y0 + 190), -2400, y0 + 120)

        # A BAND with a peak, not a ramp to the shore. Surf breaks a little way
        # OUTSIDE the beach, where the bottom first shoals enough to trip the
        # swell; a ramp that maxes at the shore line puts its white water where
        # the sand already hides the sea, and the first two attempts showed
        # exactly that - either a wide blotchy wash offshore (shape 2.2) or
        # almost nothing at all (shape 4.2).
        #
        #   u = (d - shore) / (outer - shore)      0 at the beach, 1 at the edge
        #   band = saturate(1 - |u - peak| / halfwidth)
        u = sat(div(sub(d, isle, -2200, y0, bo="B"), width, -2050, y0), -1900, y0)
        off = expr(unreal.MaterialExpressionAbs, -1800, y0)
        link(sub(u, surf_peak, -1870, y0 + 60), "", off, IN1)
        band = sat(sub(const(1.0, -1700, y0 + 60),
                       div(off, surf_half, -1700, y0), -1600, y0), -1500, y0)
        surf = band if surf is None else maxn(surf, band, -1750, y0)

    # Shaped: the white water heaps up towards the beach rather than lying in
    # an even ring, and it BREATHES - a band travelling shoreward, so it reads
    # as surf running in rather than as a painted collar.
    surf_shape = scalar("SurfShape", 1.5, -1550, 3300)
    surf_curved = power(surf, surf_shape, -1400, 3200)

    # The bubble texture BREAKS the surf up; it must not GATE it. Multiplied
    # straight through, a clump of texture with a hole in it puts a hole in the
    # line of breakers, and the first capture showed exactly that - blotches
    # offshore instead of white water running along the beach. A floor keeps
    # the line continuous and lets the texture add the froth on top.
    surf_floor = scalar("SurfTextureFloor", 0.45, -1550, 3680)
    surf_gain = scalar("SurfGain", 2.1, -1550, 3600)
    broken = add(surf_floor,
                 mul(ftex, "", sub(const(1.0, -1400, 3680), surf_floor,
                                   -1250, 3680), "", -1100, 3620), -950, 3650)
    surf_final = sat(mul(surf_curved, "",
                         mul(broken, "", surf_gain, "", -350, 3420), "", -200, 3300),
                     -50, 3300)

    # --------------------------------------------------------------- wake
    # The trail a hull leaves. C++ drops a breadcrumb behind each tracked ship
    # every nine metres and pushes them as WakePt%d, packed (x, y, strength,
    # half-width); here each consecutive PAIR inside a ship's block becomes a
    # capsule, and the capsules are MAXed together.
    #
    # Consecutive pairs, not a formula from the ship's heading, is the whole
    # point: the crumbs stay where the water was, so putting the helm over
    # leaves a CURVED wake instead of swinging two hundred metres of it round
    # like a stick.
    WAKE_SHIPS, WAKE_CRUMBS = 3, 8
    wake = None
    wake_widen = scalar("WakeWidthScale", 1.0, -3000, 4400)
    for ship in range(WAKE_SHIPS):
        for k in range(WAKE_CRUMBS - 1):
            y0 = 4600 + (ship * (WAKE_CRUMBS - 1) + k) * 300
            a = vector("WakePt%d" % (ship * WAKE_CRUMBS + k),
                       unreal.LinearColor(0, 0, 0, 0), -3000, y0)
            b = vector("WakePt%d" % (ship * WAKE_CRUMBS + k + 1),
                       unreal.LinearColor(0, 0, 0, 0), -3000, y0 + 120)

            pa = mask(a, -2800, y0)
            pb = mask(b, -2800, y0 + 120)

            # Distance from this pixel to the SEGMENT ab: project, clamp, measure.
            ab = sub(pb, pa, -2600, y0 + 60)
            ap = sub(world_xy, pa, -2600, y0 - 60)
            ab_len2 = expr(unreal.MaterialExpressionDotProduct, -2400, y0 + 60)
            link(ab, "", ab_len2, A_IN)
            link(ab, "", ab_len2, B_IN)
            ap_ab = expr(unreal.MaterialExpressionDotProduct, -2400, y0 - 60)
            link(ap, "", ap_ab, A_IN)
            link(ab, "", ap_ab, B_IN)
            t = sat(div(ap_ab, maxn(ab_len2, const(1.0, -2250, y0 + 140),
                                    -2200, y0 + 60), -2050, y0), -1900, y0)
            closest = add(pa, mul(ab, "", t, "", -1750, y0 + 60), -1600, y0)
            d = expr(unreal.MaterialExpressionDistance, -1450, y0)
            link(world_xy, "", d, A_IN)
            link(closest, "", d, B_IN)

            # Half-width lives in the ALPHA of the packed vector, so it comes off
            # the named "A" pin. A VectorParameter's default output is a float3
            # and a mask on the fourth component does not compile - the same
            # trap the island surf fell into, in the same file.
            #
            # Taken from the OLDER end (the larger width), so the trail fans out
            # astern smoothly rather than stepping wider in jumps.
            half = mul(maxn(a, b, -1750, y0 + 200, ao="A", bo="A"), "",
                       wake_widen, "", -1600, y0 + 200)
            inside = sat(sub(const(1.0, -1450, y0 + 260),
                             div(d, maxn(half, const(1.0, -1450, y0 + 320),
                                         -1300, y0 + 200), -1150, y0), -1000, y0),
                         -850, y0)

            # Both ends must be alive, or a dead crumb's slot at the origin
            # would draw a capsule from the ship to (0,0).
            live = mul(a, "B", b, "B", -1000, y0 + 200)
            seg = mul(inside, "", live, "", -700, y0)
            wake = seg if wake is None else maxn(wake, seg, -550, y0)

    # --------------------------------------- the collar and the bow arms
    # The breadcrumbs are HISTORY. These two need where the ship is this frame.
    #
    # A = (x, y, trackX, trackY), B = (half-length, half-beam, strength, tanK).
    # Everything below works in the ship's own frame: `along` is how far ahead
    # of her centre the pixel is, `across` how far to the side. Two dot products
    # and the rest is arithmetic.
    for ship in range(WAKE_SHIPS):
        y0 = 7200 + ship * 700
        sa = vector("WakeShip%dA" % ship, unreal.LinearColor(0, 0, 0, 0), -3000, y0)
        sb = vector("WakeShip%dB" % ship, unreal.LinearColor(0, 0, 0, 0), -3000, y0 + 110)

        centre = mask(sa, -2800, y0)
        # Built from the named B and A pins, not by masking: the default output
        # of a VectorParameter is a float3, so the fourth component cannot be
        # masked at all. Third time this file has taught me that.
        fwd = expr(unreal.MaterialExpressionAppendVector, -2800, y0 + 60)
        link(sa, "B", fwd, A_IN)
        link(sa, "A", fwd, B_IN)
        rel = sub(world_xy, centre, -2600, y0)

        along = expr(unreal.MaterialExpressionDotProduct, -2400, y0)
        link(rel, "", along, A_IN)
        link(fwd, "", along, B_IN)
        # The perpendicular of (x, y) is (-y, x); built by masking the pair the
        # other way round and negating the first component.
        side = expr(unreal.MaterialExpressionAppendVector, -2500, y0 + 130)
        link(mul(sa, "A", neg_one, "", -2650, y0 + 130), "", side, A_IN)
        link(sa, "B", side, B_IN)
        across = expr(unreal.MaterialExpressionDotProduct, -2400, y0 + 130)
        link(rel, "", across, A_IN)
        link(side, "", across, B_IN)
        across_abs = expr(unreal.MaterialExpressionAbs, -2250, y0 + 130)
        link(across, "", across_abs, IN1)

        # COLLAR: inside the hull's own capsule, plus a band outside it.
        half_len = mask(sb, -2800, y0 + 200, True, False, False)
        half_beam = mask(sb, -2800, y0 + 260, False, True, False)
        collar_w = scalar("CollarWidthCm", 220.0, -2800, 320 + y0)
        # max(x, 0), NOT saturate. These are DISTANCES in centimetres and the
        # only thing that needs clipping is the negative side; saturate caps
        # them at one centimetre, so the distance outside the hull was never
        # more than 1.4 cm anywhere in the world and the collar read ~0.99
        # across the whole leash. The sea grew a solid disc of foam ninety
        # metres across around every ship.
        zero = const(0.0, -2150, y0 + 320)
        over_len = maxn(sub(absn(along, -2200, y0 + 200), half_len,
                            -2050, y0 + 200), zero, -1900, y0 + 200)
        over_beam = maxn(sub(across_abs, half_beam, -2050, y0 + 260), zero,
                         -1900, y0 + 260)
        # Distance outside the capsule, in the two axes together.
        out = expr(unreal.MaterialExpressionAppendVector, -1750, y0 + 200)
        link(over_len, "", out, A_IN)
        link(over_beam, "", out, B_IN)
        out_d = expr(unreal.MaterialExpressionLength, -1600, y0 + 200)
        link(out, "", out_d, IN1)
        collar = sat(sub(const(1.0, -1450, y0 + 260),
                         div(out_d, collar_w, -1300, y0 + 200), -1150, y0 + 200),
                     -1000, y0 + 200)

        # BOW ARMS: the Kelvin wedge. A pixel is on an arm when its distance
        # across the track is within a hair of |along| * tan(19.47 deg), astern
        # of the bow and inside the arm's length.
        tan_k = sb   # read off the "A" pin at the point of use
        astern = sat(div(mul(along, "", neg_one, "", -2200, y0 + 380),
                         scalar("BowArmLengthCm", 5200.0, -2800, y0 + 440),
                         -2050, y0 + 380), -1900, y0 + 380)
        want = mul(absn(along, -2200, y0 + 440), "", tan_k, "A", -2050, y0 + 440)
        arm_w = scalar("BowArmHalfWidthCm", 130.0, -2800, y0 + 500)
        arm = sat(sub(const(1.0, -1600, y0 + 500),
                      div(absn(sub(across_abs, want, -1750, y0 + 440),
                               -1600, y0 + 440), arm_w, -1450, y0 + 440),
                      -1300, y0 + 440), -1150, y0 + 440)
        # Only ASTERN, and bounded. The first version multiplied by
        # (1 - astern), and `astern` is zero for any pixel AHEAD of the ship -
        # so the arms ran at full strength forward, along two straight lines, to
        # the horizon. The whole sea went white. A Kelvin wedge is a pair of
        # infinite lines unless something bounds it; bound it twice.
        #
        #   ramp  : 0 at the stem, 1 a short way astern (and 0 anywhere ahead,
        #           because -along is negative there and saturates to zero)
        #   taper : 1 at the stem, 0 at the end of the arm
        arm_ramp = sat(div(mul(along, "", neg_one, "", -1150, y0 + 560),
                           scalar("BowArmRiseCm", 700.0, -1300, y0 + 620),
                           -1000, y0 + 560), -850, y0 + 560)
        arm_taper = sat(sub(const(1.0, -1000, y0 + 680), astern,
                            -850, y0 + 680), -700, y0 + 680)
        arm = mul(mul(arm, "", arm_ramp, "", -600, y0 + 440), "",
                  arm_taper, "", -450, y0 + 440)

        strength = sb   # read off the "B" pin at the point of use
        live = maxn(collar, arm, -550, y0 + 200)

        # And a hard leash on the whole live term: nothing a ship draws may
        # reach further than this from her, whatever the arithmetic above does.
        # A structural bound is worth more than a correct formula, because the
        # formula was correct right up until it was not.
        reach = scalar("WakeReachCm", 9000.0, -800, y0 + 740)
        near = sat(sub(const(1.0, -650, y0 + 800),
                       div(expr_dist(world_xy, centre, -800, y0 + 700),
                           reach, -500, y0 + 700), -350, y0 + 700), -200, y0 + 700)
        wake = maxn(wake,
                    mul(mul(live, "", strength, "B", -400, y0 + 200), "",
                        near, "", -250, y0 + 200), -100, y0 + 200)

    wake_gain = scalar("WakeGain", 1.9, -3000, 4480)
    wake_final = sat(mul(mul(wake, "", wake_gain, "", -400, 4600),
                         "", broken, "", -250, 4600), -100, 4600)

    # The three whites do not add - they take the largest, so a wake crossing
    # the surf does not stack into a blown-out patch.
    foam = maxn(maxn(crest_foam, surf_final, 380, 950), wake_final, 520, 950)

    # ------------------------------------------------------------ colour
    deep = vector("DeepColor", unreal.LinearColor(0.0055, 0.0180, 0.0330, 1.0), -900, 200)
    shallow = vector("ShallowColor", unreal.LinearColor(0.0200, 0.0720, 0.0790, 1.0), -900, 300)
    foam_col = vector("FoamColor", unreal.LinearColor(0.400, 0.430, 0.445, 1.0), -900, 400)

    # Scatter: the crest is thin and lit through, the trough is deep and dark.
    scatter_scale = scalar("ScatterRangeCm", 150.0, -700, 500)
    scatter = sat(add(mul(div(crest, scatter_scale, -560, 420), "",
                          const(0.5, -560, 500), "", -420, 420),
                      const(0.5, -420, 500), -280, 420), -140, 420)
    water_col = lerp(deep, shallow, scatter, 0, 300)
    base = lerp(water_col, foam_col, foam, 220, 300)
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Foam is rough and it does not reflect; that contrast is what sells it.
    water_rough = scalar("Roughness", 0.022, -900, 600)
    foam_rough = scalar("FoamRoughness", 0.82, -900, 680)
    MEL.connect_material_property(lerp(water_rough, foam_rough, foam, 220, 600),
                                  "", unreal.MaterialProperty.MP_ROUGHNESS)

    water_spec = scalar("Specular", 1.0, -900, 760)
    foam_spec = scalar("FoamSpecular", 0.22, -900, 830)
    MEL.connect_material_property(lerp(water_spec, foam_spec, foam, 220, 760),
                                  "", unreal.MaterialProperty.MP_SPECULAR)

    metal = scalar("Metallic", 0.0, 220, 860)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)

    MEL.recompile_material(mat)
    EAL.save_asset(SEA_MAT)
    n = MEL.get_num_material_expressions(mat)
    L("built, expressions=%d (was 63 before the detail pass)" % n)
    return n


n = build()
L("DONE expressions=%d" % n)
