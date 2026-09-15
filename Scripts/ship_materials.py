"""
Rebuilds M_ShipMaster as a textured, physically-shaded master, and re-points
the six instances at it.

THE PROBLEM THIS SOLVES. The hull was one flat linear brown. Not "a simple
texture" - no texture, a constant. Every pixel of a thirty-one-metre ship was
the same three numbers, which is why the captures read as a toy.

THE PROBLEM THAT MADE IT AWKWARD. Scripts/ship.py builds the hull from
cross-sections in Blender and never creates a UV layer - checked, not assumed:
there is no `uv_layers.new` anywhere in the file, while island.py has one. A
mesh with no UVs samples texel (0,0) everywhere, so naively assigning a texture
would have produced... one flat colour, and it would have looked like the
texture "didn't work" rather than like the mesh was missing something.

THE ANSWER. Project the texture in the mesh's OWN space, from two directions:

    sides (|normal.z| low)  ->  UV = (local.x, local.z)
    top   (|normal.z| high) ->  UV = (local.x, local.y)

Two samples, not the three of a full triplanar, and they are the two that
happen to be right: on the side of a hull, x-along/z-up makes the planking run
fore-and-aft the way strakes actually do, and on the deck, x/y makes the deck
planks run fore-and-aft too. A world-space projection would have been cheaper
still and would have made the timber SWIM across the hull as she sails.

And the waterline. Below the boot-top the timber is soaked: darker, and far
glossier. It costs two lerps and it is the single detail that most makes a hull
look like it is IN the water rather than sitting on a picture of it.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT_DIR = "/Game/Materials"
TEX_DIR = "/Game/Textures"
MASTER = MAT_DIR + "/M_ShipMaster"
MESH = "/Game/Meshes/SM_PirateShip"

A_IN, B_IN = ["A"], ["B"]
IN1 = ["", "Input", "VectorInput"]

# slot name -> (albedo, normal, rough tex or None, tint, cm per tile,
#               roughness floor, roughness ceiling, metallic, normal strength)
PARTS = {
    # albedo, normal, roughness (or None), tint, cm per tile, roughness floor,
    # roughness ceiling, metallic, normal strength,
    # PLANE WEIGHTS X, Y, Z  (which of the three projections this part uses),
    # ROPE COLLAPSE (1 = shrink to a point at distance; only the cordage),
    # SWAY (how much this part bends in the wind; 0 for anything that should
    # not move at all - a hull does not bend, and a deck that breathes is a
    # bug, not weather), SWAY HEIGHT in cm (the height over which that bend
    # builds up).
    #
    # THE WHOLE RIG MOVES AS ONE. Masts, sails and cordage carry the same amount
    # over the same height on purpose: a shroud is made fast to the masthead, so
    # if it swayed by four times what the mast does - which is what a "ropes are
    # more flexible" reading of these numbers produced - it would visibly come
    # away from the spar it is tied to. What a sail does on its own, luffing
    # against its bolt ropes, is a separate and faster motion that is not built
    # yet, and is not this.
    #
    # The weights replace the old TopWeight/SideSwap pair. There are three
    # planes now, not two: with only (x, z) and (x, y) every surface lying in a
    # plane of constant x - the transom, the bow cap, the cabin ends, the whole
    # side of a mast facing forward - took its u from x and sampled ONE COLUMN
    # of texels, stretched along the piece. A 1 here means "this part may use
    # the plane whose axis is this one".
    #
    # A sail is (1, 0, 0) deliberately: it is modelled in the Y-Z plane, so x is
    # its BULGE, and any projection that uses x as a coordinate draws the
    # contour lines of the bulge - a set of concentric RINGS on every sail. That
    # cost three runs to diagnose once; it is not being reintroduced for the
    # sake of symmetry.
    "M_Hull":     ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (0.72, 0.62, 0.58), 520.0, 0.45, 0.88, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 600.0),
    "M_Deck":     ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (1.55, 1.42, 1.18), 330.0, 0.60, 0.95, 0.0, 1.2, 1.0, 1.0, 1.0, 0.0, 0.0, 600.0),
    # Masts and yards: round in XY, and nobody sees the top of a mast.
    "M_Wood":     ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (1.05, 0.92, 0.74), 240.0, 0.55, 0.90, 0.0, 0.8, 1.0, 1.0, 0.0, 0.0, 0.10, 1800.0),
    "M_DarkWood": ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (0.42, 0.36, 0.33), 300.0, 0.40, 0.80, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 600.0),
    "M_Sail":     ("T_Canvas_C", "T_Canvas_N", None,
                   (1.0, 1.0, 1.0), 900.0, 0.78, 0.96, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.10, 1800.0),
    "M_Iron":     ("T_Iron_C", "T_Iron_N", None,
                   (1.0, 1.0, 1.0), 90.0, 0.32, 0.62, 0.82, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 600.0),
    # Tarred hemp, tiled small: a rope is a few centimetres across and the lay
    # has to be visible at that size or the rigging reads as wire.
    "M_Rope":     ("T_Rope_C", "T_Rope_N", None,
                   (1.0, 1.0, 1.0), 45.0, 0.74, 0.94, 0.0, 1.1, 1.0, 1.0, 0.0, 1.0, 0.10, 1800.0),
}


def L(m):
    unreal.log("SHIPMAT " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s: %s" % (n, str(e)[:80]))
        return False


def build_master():
    if EAL.does_asset_exist(MASTER):
        EAL.delete_asset(MASTER)
    mat = AT.create_asset("M_ShipMaster", MAT_DIR, unreal.Material,
                          unreal.MaterialFactoryNew())

    # The island's foliage is drawn with this same master on HIERARCHICAL
    # INSTANCED meshes, and a material must DECLARE that it may be. Without the
    # flag the engine silently substitutes the default material and says so once
    # in a log line that does not contain the words "failed to compile" - which
    # cost most of a session on the gun smoke before it was found. Set, and read
    # BACK, because sp() swallows a failure into a WARN.
    sp(mat, "used_with_instanced_static_meshes", True)
    if not mat.get_editor_property("used_with_instanced_static_meshes"):
        raise RuntimeError("M_ShipMaster: used_with_instanced_static_meshes did "
                           "not stick; the foliage would draw in default grey")

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def link(a, ao, b, pins):
        for p in pins:
            if MEL.connect_material_expressions(a, ao, b, p):
                return True
        L("WARN link %s->%s %s" % (a.get_name(), b.get_name(), pins))
        return False

    def const(v, x, y):
        n = expr(unreal.MaterialExpressionConstant, x, y)
        sp(n, "r", v)
        return n

    def scalar(name, v, x, y, group="Surface"):
        n = expr(unreal.MaterialExpressionScalarParameter, x, y)
        sp(n, "parameter_name", name)
        sp(n, "default_value", v)
        sp(n, "group", group)
        return n

    def vector(name, c, x, y, group="Surface"):
        n = expr(unreal.MaterialExpressionVectorParameter, x, y)
        sp(n, "parameter_name", name)
        sp(n, "default_value", c)
        sp(n, "group", group)
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

    def mask(src, x, y, r, g, b, so=""):
        n = expr(unreal.MaterialExpressionComponentMask, x, y)
        sp(n, "r", r); sp(n, "g", g); sp(n, "b", b); sp(n, "a", False)
        link(src, so, n, IN1)
        return n

    def sat(src, x, y, so=""):
        n = expr(unreal.MaterialExpressionClamp, x, y)
        sp(n, "min_default", 0.0)
        sp(n, "max_default", 1.0)
        link(src, so, n, IN1 + ["Input"])
        return n

    def lerp(a, b, al, x, y, ao="", bo="", alo=""):
        n = expr(unreal.MaterialExpressionLinearInterpolate, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        link(al, alo, n, ["Alpha"])
        return n

    def absn(src, x, y, so=""):
        n = expr(unreal.MaterialExpressionAbs, x, y)
        link(src, so, n, IN1)
        return n

    def crossn(a, b, x, y, ao="", bo=""):
        n = expr(unreal.MaterialExpressionCrossProduct, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def normalizen(src, x, y, so=""):
        n = expr(unreal.MaterialExpressionNormalize, x, y)
        link(src, so, n, IN1)
        return n

    def sinn(src, x, y, so=""):
        n = expr(unreal.MaterialExpressionSine, x, y)
        link(src, so, n, IN1)
        return n

    def app(a, b, x, y, ao="", bo=""):
        """(float2 or float, float) -> one vector. Three scalars into a float3
        takes two of these; there is no three-input version."""
        n = expr(unreal.MaterialExpressionAppendVector, x, y)
        link(a, ao, n, A_IN)
        link(b, bo, n, B_IN)
        return n

    def vec3(c, x, y):
        n = expr(unreal.MaterialExpressionConstant3Vector, x, y)
        sp(n, "constant", c)
        return n

    def tex(name, asset, x, y, normal=False, grey=False):
        n = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        sp(n, "parameter_name", name)
        sp(n, "group", "Textures")
        t = EAL.load_asset(TEX_DIR + "/" + asset)
        if t is None:
            L("MISSING texture %s" % asset)
        sp(n, "texture", t)
        if normal:
            sp(n, "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        elif grey:
            sp(n, "sampler_type",
               unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        return n

    # ----------------------------------------------- the three projections
    # INSTANCE space, not local. For an ordinary mesh the two are the same
    # thing, but the island's palms and scrub are drawn from this master as
    # HIERARCHICAL INSTANCED meshes, and for those the shader's "local" is the
    # COMPONENT's frame - one frame shared by every plant on the hill. Each palm
    # is planted with its own yaw, lean and scale, and in local space none of
    # that reached the projection: the texture stood still in the island's frame
    # while the plants turned inside it. Instance space is per-plant, and for a
    # non-instanced mesh the engine falls back to the primitive transform, so
    # the ship is unchanged.
    wp = expr(unreal.MaterialExpressionWorldPosition, -2600, 0)
    lp = expr(unreal.MaterialExpressionTransformPosition, -2400, 0)
    sp(lp, "transform_source_type",
       unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    sp(lp, "transform_type",
       unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_INSTANCE)
    link(wp, "", lp, IN1)
    if lp.get_editor_property("transform_type") != \
            unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_INSTANCE:
        raise RuntimeError("M_ShipMaster: the position transform did not take "
                           "instance space, so the foliage would project in the "
                           "island's frame")

    scale = scalar("TexScaleCm", 400.0, -2600, 160)
    luv = div(lp, scale, -2200, 0)
    # One plane per axis, each using the OTHER two coordinates. The plane is
    # named for the axis it is projected ALONG.
    uv_x = mask(luv, -2000, -240, False, True, True)    # (y, z)
    uv_y = mask(luv, -2000, -80, True, False, True)     # (x, z)
    uv_z = mask(luv, -2000, 80, True, True, False)      # (x, y)

    # The blend: the vertex normal in the same instance space, so a hull that
    # rolls does not have her planking slide from one projection to another.
    vn = expr(unreal.MaterialExpressionVertexNormalWS, -2600, 320)
    ln_raw = expr(unreal.MaterialExpressionTransform, -2400, 320)
    sp(ln_raw, "transform_source_type",
       unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    sp(ln_raw, "transform_type",
       unreal.MaterialVectorCoordTransform.TRANSFORM_INSTANCE)
    link(vn, "", ln_raw, IN1)
    if ln_raw.get_editor_property("transform_type") != \
            unreal.MaterialVectorCoordTransform.TRANSFORM_INSTANCE:
        raise RuntimeError("M_ShipMaster: the normal transform did not take "
                           "instance space; its default is TANGENT")
    # Normalised before anything reads it: a world-to-instance transform on a
    # component with non-uniform scale does not preserve length, and the blend
    # weights below are a function of that length.
    ln = normalizen(ln_raw, -2250, 320)

    sharp = scalar("ProjectionSharpness", 6.0, -2200, 430)
    # Raising |n| to a power makes each changeover a narrow band instead of a
    # long smear across the bilge.
    pw = expr(unreal.MaterialExpressionPower, -1900, 400)
    link(absn(ln, -2100, 320), "", pw, ["Base"])
    link(sharp, "", pw, ["Exponent", "Exp"])

    # Per-part gating, one scalar per plane. This replaces TopWeight and
    # SideSwap: the old pair could only say "less top" or "swap the side", and
    # could not say "this part has a third face nobody is projecting onto".
    gate = app(app(scalar("PlaneWeightX", 1.0, -2200, 520),
                   scalar("PlaneWeightY", 1.0, -2200, 600), -2000, 520),
               scalar("PlaneWeightZ", 1.0, -2200, 680), -1850, 520)
    w_raw = mul(pw, "", gate, "", -1700, 460)
    # Renormalised, and by the SUM rather than by an assumption: a part that
    # gates two of the three planes off still has to come out at full strength.
    ones = vec3(unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -1700, 600)
    w_sum = expr(unreal.MaterialExpressionDotProduct, -1550, 520)
    link(w_raw, "", w_sum, A_IN)
    link(ones, "", w_sum, B_IN)
    w = div(w_raw, add(w_sum, const(0.0001, -1450, 600), -1400, 560), -1300, 500)
    wx = mask(w, -1150, 440, True, False, False)
    wy = mask(w, -1150, 500, False, True, False)
    wz = mask(w, -1150, 560, False, False, True)

    def triplanar(param, asset, x, y, normal=False, grey=False):
        """The same texture sampled once per plane and blended by how much the
        surface faces each one. Returns the blend, or the three taps when the
        caller needs them apart (the normal does)."""
        tx = tex(param + "X", asset, x, y, normal, grey)
        link(uv_x, "", tx, ["UVs", "Coordinates"])
        ty = tex(param, asset, x, y + 260, normal, grey)
        link(uv_y, "", ty, ["UVs", "Coordinates"])
        tz = tex(param + "Top", asset, x, y + 520, normal, grey)
        link(uv_z, "", tz, ["UVs", "Coordinates"])
        return tx, ty, tz

    def blend3(tx, ty, tz, x, y):
        return add(add(mul(tx, "", wx, "", x, y),
                       mul(ty, "", wy, "", x, y + 120), x + 150, y),
                   mul(tz, "", wz, "", x, y + 240), x + 300, y)

    # ------------------------------------------------------------- albedo
    alb = blend3(*triplanar("BaseTex", "T_Timber_C", -1500, -900), x=-1000, y=-300)
    tint = vector("Tint", unreal.LinearColor(1, 1, 1, 1), -1500, 120)
    tinted = mul(alb, "", tint, "", -900, -200)

    # --------------------------------------------------------- the wetline
    # Local Z, in centimetres, against the modelled waterline at zero.
    lz = mask(lp, -2200, 700, False, False, True)
    wet_top = scalar("WetlineTopCm", 40.0, -2200, 800)
    wet_fade = scalar("WetlineFadeCm", 130.0, -2200, 880)
    # How far BELOW the boot-top this pixel is, as a 0..1 ramp.
    below = expr(unreal.MaterialExpressionSubtract, -2000, 700)
    link(wet_top, "", below, A_IN)
    link(lz, "", below, B_IN)
    wet = sat(div(below, wet_fade, -1850, 700), -1700, 700)
    wet_amount = scalar("WetAmount", 1.0, -1700, 820)
    wetness = mul(wet, "", wet_amount, "", -1550, 700)

    soak = vector("SoakTint", unreal.LinearColor(0.34, 0.30, 0.28, 1.0), -1550, 830)
    wet_col = mul(tinted, "", soak, "", -700, 0)
    base = lerp(tinted, wet_col, wetness, -500, -100)
    MEL.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # ------------------------------------------------------------- normal
    #
    # THE FRAME, third attempt, and the two failures are why this is written out
    # at length.
    #
    # A tangent-space normal map is a promise that the mesh has a tangent frame,
    # and a tangent frame comes from UVs. No mesh in this project has UVs - that
    # is the whole reason this material projects - so the bumps were being
    # tilted in a basis nobody had defined.
    #
    # Attempt one rebuilt the sample in the frame of the projection PLANE and
    # handed the engine a world-space normal. A plane-frame normal always points
    # along that plane's axis, so it is right only where the surface faces the
    # way the plane assumes. The masts came out black.
    #
    # Attempt two kept the geometric normal and tilted it in a frame made by
    # crossing that normal with a fixed vector. It looked right in a capture and
    # was wrong by a hundred and sixty degrees: cross(N, cross(N, h)) is the
    # fixed vector h flattened into the surface and NEGATED - a function of h,
    # not of the projection - so the map's G channel, which carries 99.5% of
    # this texture's relief because the plank seams run along rows, was applied
    # very nearly backwards over the whole hull. Every caulked seam was lit as a
    # raised batten.
    #
    # This one does not invent a frame at all. A projected sample IS a height
    # gradient in the plane's own two axes, so the gradient is rebuilt in those
    # axes, the three planes' gradients are blended (a linear operation, so no
    # seam and no discontinuity to gate against), the part of the gradient along
    # the surface normal is removed, and what is left tilts the geometric
    # normal. Degenerate nowhere. Exactly the geometric normal at strength zero.
    # On a mast facing +X the X-plane's gradient simply loses the component that
    # projection cannot see, instead of pointing somewhere arbitrary.
    sp(mat, "tangent_space_normal", False)
    if mat.get_editor_property("tangent_space_normal"):
        raise RuntimeError("M_ShipMaster: tangent_space_normal did not stick, so "
                           "the engine would read an instance-space normal as "
                           "though it were tangent-space")

    nx, ny, nz = triplanar("NormalTex", "T_Timber_N", -1500, 1000, normal=True)
    nstr = scalar("NormalStrength", 1.0, -1500, 1760)
    zero = const(0.0, -1150, 1000)

    def grad(t, axis, x, y):
        """(r, g) of a normal map are -dh/du and -dh/dv in the plane's own axes,
        so the height gradient is r along u and g along v, and zero along the
        axis the sample was taken down."""
        r = mask(t, x, y, True, False, False)
        g = mask(t, x, y + 60, False, True, False)
        if axis == "x":            # uv = (y, z)
            return app(app(zero, r, x + 150, y), g, x + 300, y)
        if axis == "y":            # uv = (x, z)
            return app(app(r, zero, x + 150, y), g, x + 300, y)
        return app(app(r, g, x + 150, y), zero, x + 300, y)   # uv = (x, y)

    grad3 = add(add(mul(grad(nx, "x", -1100, 1000), "", wx, "", -700, 1000),
                    mul(grad(ny, "y", -1100, 1160), "", wy, "", -700, 1160),
                    -520, 1000),
                mul(grad(nz, "z", -1100, 1320), "", wz, "", -700, 1320),
                -380, 1000)
    # Only the part along the surface: a gradient has no business pushing the
    # normal along itself.
    along = expr(unreal.MaterialExpressionDotProduct, -300, 1140)
    link(ln, "", along, A_IN)
    link(grad3, "", along, B_IN)
    flat = sub(grad3, mul(ln, "", along, "", -200, 1200), -120, 1140)
    n_local = normalizen(add(ln, mul(flat, "", nstr, "", -40, 1180), 40, 1140),
                         120, 1140)

    nworld = expr(unreal.MaterialExpressionTransform, 220, 1140)
    sp(nworld, "transform_source_type",
       unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_INSTANCE)
    sp(nworld, "transform_type",
       unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(n_local, "", nworld, IN1)
    if nworld.get_editor_property("transform_type") != \
            unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD:
        raise RuntimeError("M_ShipMaster: the normal's instance-to-world "
                           "transform did not take, and its default is TANGENT")
    MEL.connect_material_property(nworld, "", unreal.MaterialProperty.MP_NORMAL)

    # ---------------------------------------------------------- roughness
    rtex = blend3(*triplanar("RoughTex", "T_Timber_R", -1500, 2000, grey=True),
                  x=-1000, y=1900)
    rmin = scalar("RoughMin", 0.45, -1500, 2160)
    rmax = scalar("RoughMax", 0.90, -1500, 2240)
    rough_dry = lerp(rmin, rmax, rtex, -700, 1900)
    wet_rough = scalar("WetRoughness", 0.11, -700, 2050)
    rough = lerp(rough_dry, wet_rough, wetness, -500, 1900)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # ------------------------------------------------- the vanishing ropes
    # Three-centimetre cordage is sub-pixel past a couple of hundred metres, and
    # sub-pixel high-contrast geometry does not average, it SPARKLES. The honest
    # fix is to stop drawing it: past RopeFadeStartCm the vertices are pulled
    # toward the mesh origin until the whole rope is a point.
    #
    # Per-instance, via RopeCollapse: 0 for every other slot, so the hull, deck
    # and sails have an identically zero world-position offset and cannot be
    # affected by this at all.
    # ------------------------------------------------------- wind in the parts
    #
    # The one thing that made every still frame of this project read as a
    # diorama: nothing moved. The sea moved, the ship moved, and every rope,
    # sail and frond stood exactly still in a twelve-metre breeze.
    #
    # Shaped like actual wind on a flexible thing rather than like a sine wave:
    # it is BENT DOWNWIND FIRST and oscillates about that bend, the bend grows
    # with wind speed, a slow gust term swells and eases it, and the whole thing
    # is anchored - the displacement is scaled by height above the piece's own
    # origin, squared, so the root never moves and the tip moves most. A frond
    # that slides sideways at its base is a mesh coming loose, not weather.
    #
    # All four wind numbers are PUSHED FROM C++ and default to a dead calm, so a
    # part whose owner pushes nothing is exactly as still as it is today.
    wind_x = scalar("WindVecX", 1.0, -2600, 1900, group="Wind")
    wind_y = scalar("WindVecY", 0.0, -2600, 1980, group="Wind")
    wind_s = scalar("WindSpeedMS", 0.0, -2600, 2060, group="Wind")
    wind_t = scalar("WindTime", 0.0, -2600, 2140, group="Wind")
    sway_amt = scalar("SwayAmount", 0.0, -2600, 2220, group="Wind")
    # The height at which the bend reaches full, in centimetres above the
    # piece's own origin. Per instance: a palm is six metres, a bush is one, and
    # a mast is eighteen.
    sway_h = scalar("SwayHeightCm", 600.0, -2600, 2300, group="Wind")
    # Centimetres of bend per metre per second of wind, at the top of the
    # piece. SIZED PHYSICALLY, not to a pixel metric: at six, a six-metre palm's
    # crown travels about eighty centimetres in a fourteen-metre breeze, of
    # which seventy per cent is the oscillation - a few pixels of movement at
    # the distance the island is usually watched from, and a plain sway from the
    # beach. It was sixteen for one build, which put the crown through two and a
    # quarter metres: a palm tree behaving like a windscreen wiper.
    sway_gain = scalar("SwayCmPerMS", 6.0, -2600, 2380, group="Wind")

    # Anchored: 0 at the origin of the piece, 1 at its reference height, and
    # SQUARED so the movement is a bend and not a slide.
    lz_sway = mask(lp, -2400, 1900, False, False, True)
    # An explicit height per instance, in centimetres. It was the piece's own
    # bounds for one build - which is the right idea and does not work here: a
    # probe with this ramp routed to emissive at daylight strength showed hn = 1
    # over the WHOLE ship, hull bottom included, because ObjectBounds came back
    # at about zero for that mesh and the divide collapsed. The plants ramped
    # correctly in the same frame, which is exactly the kind of half-working
    # that a number in a log would never have shown.
    hn = sat(div(lz_sway, sway_h, -2100, 1820), -2050, 1820)
    hn2 = mul(hn, "", hn, "", -1950, 1900)

    # Per-instance phase, so a hillside of palms does not beat time together.
    opos = expr(unreal.MaterialExpressionObjectPositionWS, -2400, 2460)
    oph = mul(mask(opos, -2250, 2460, True, False, False), "",
              const(0.0013, -2250, 2540), "", -2100, 2460)

    # Two rates, a fast flutter over a slower swing. One sine reads as a
    # metronome; two that do not share a period do not.
    rate = add(const(1.6, -2250, 2620), mul(wind_s, "", const(0.09, -2250, 2700),
                                            "", -2100, 2660), -1950, 2620)
    phase = add(mul(wind_t, "", rate, "", -1800, 2620), oph, -1650, 2620)
    osc = add(mul(sinn(phase, -1500, 2560), "", const(0.6, -1500, 2640), "",
                  -1350, 2560),
              mul(sinn(add(mul(phase, "", const(2.3, -1500, 2720), "", -1350, 2700),
                           const(1.7, -1350, 2780), -1200, 2700), -1050, 2700),
                  "", const(0.4, -1050, 2780), "", -900, 2700), -750, 2600)
    # The gust: slow, and never all the way to nothing.
    gust = add(const(0.65, -1500, 2860),
               mul(sinn(mul(wind_t, "", const(0.31, -1650, 2940), "", -1500, 2940),
                        -1350, 2940), "", const(0.35, -1350, 3020), "", -1200, 2940),
               -1050, 2860)

    # Bent downwind, oscillating about the bend.
    bend = add(const(0.30, -900, 2380), mul(osc, "", const(0.70, -900, 2460), "",
                                            -750, 2400), -600, 2380)
    amp = mul(mul(mul(sway_amt, "", hn2, "", -1800, 2180), "",
                  mul(wind_s, "", sway_gain, "", -1800, 2260), "", -1650, 2180),
              "", mul(gust, "", bend, "", -600, 2440), "", -450, 2200)
    wind_dir = app(app(wind_x, wind_y, -2400, 2060), const(0.0, -2400, 2140),
                   -2250, 2060)
    sway = mul(wind_dir, "", amp, "", -300, 2160)

    collapse = scalar("RopeCollapse", 0.0, -1500, 2600)
    fade_start = scalar("RopeFadeStartCm", 24000.0, -1500, 2680)
    fade_range = scalar("RopeFadeRangeCm", 16000.0, -1500, 2760)
    depth = expr(unreal.MaterialExpressionPixelDepth, -1500, 2840)
    far = sat(div(sub(depth, fade_start, -1300, 2700), fade_range, -1150, 2700),
              -1000, 2700)
    obj = expr(unreal.MaterialExpressionObjectPositionWS, -1300, 2900)
    toward = sub(obj, wp, -1100, 2880)
    wpo = mul(toward, "", mul(far, "", collapse, "", -900, 2760), "", -700, 2820)
    # The two offsets ADD: a rope that is both swaying and collapsing does both,
    # and every part that is neither contributes an identically zero vector.
    MEL.connect_material_property(add(wpo, sway, -500, 2820), "",
                                  unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    metal = scalar("Metallic", 0.0, -500, 2150)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    spec = scalar("Specular", 0.5, -500, 2240)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    MEL.recompile_material(mat)
    EAL.save_asset(MASTER)
    L("master built, expressions=%d" % MEL.get_num_material_expressions(mat))
    return mat


def build_instances(master):
    out = {}
    for slot, (alb, nrm, rgh, tint, scale, rmin, rmax, metal, nstr,
               wx, wy, wz, collapse, sway, swayh) in PARTS.items():
        name = "MI_" + slot.replace("M_", "")
        path = MAT_DIR + "/" + name
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        mi = AT.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)

        def T(pname, asset):
            t = EAL.load_asset(TEX_DIR + "/" + asset)
            if t is None:
                L("MISSING %s for %s" % (asset, name))
                return
            MEL.set_material_instance_texture_parameter_value(mi, pname, t)
            MEL.set_material_instance_texture_parameter_value(mi, pname + "Top", t)
            MEL.set_material_instance_texture_parameter_value(mi, pname + "X", t)

        T("BaseTex", alb)
        T("NormalTex", nrm)
        if rgh:
            T("RoughTex", rgh)
        MEL.set_material_instance_vector_parameter_value(
            mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        for k, v in (("TexScaleCm", scale), ("RoughMin", rmin),
                     ("RoughMax", rmax), ("Metallic", metal),
                     ("NormalStrength", nstr), ("PlaneWeightX", wx),
                     ("PlaneWeightY", wy), ("PlaneWeightZ", wz),
                     ("RopeCollapse", collapse), ("SwayAmount", sway),
                     ("SwayHeightCm", swayh)):
            MEL.set_material_instance_scalar_parameter_value(mi, k, v)
        # Only the hull and the lower works get soaked; a sail does not have a
        # waterline and neither does a topmast.
        MEL.set_material_instance_scalar_parameter_value(
            mi, "WetAmount", 1.0 if slot in ("M_Hull", "M_DarkWood") else 0.0)
        EAL.save_asset(path)
        out[slot] = mi
        L("instance %s scale=%.0fcm metal=%.2f" % (name, scale, metal))
    return out


def assign(instances):
    mesh = EAL.load_asset(MESH)
    slots = mesh.get_editor_property("static_materials")
    rebuilt, changed = [], 0
    for i, slot in enumerate(slots):
        sname = str(slot.get_editor_property("material_slot_name"))
        mi = instances.get(sname)
        ns = unreal.StaticMaterial()
        ns.set_editor_property("material_slot_name",
                               slot.get_editor_property("material_slot_name"))
        try:
            ns.set_editor_property(
                "imported_material_slot_name",
                slot.get_editor_property("imported_material_slot_name"))
        except Exception:
            pass
        ns.set_editor_property(
            "material_interface",
            mi if mi is not None else slot.get_editor_property("material_interface"))
        if mi is not None:
            changed += 1
        else:
            L("slot %d '%s' has no entry" % (i, sname))
        rebuilt.append(ns)
    mesh.set_editor_property("static_materials", rebuilt)
    EAL.save_asset(MESH)
    # Read back from a fresh load, so the report is not my own in-memory copy.
    for i, s in enumerate(EAL.load_asset(MESH).get_editor_property("static_materials")):
        m = s.get_editor_property("material_interface")
        L("verify slot %d -> %s" % (i, m.get_name() if m else "NONE"))
    L("assigned %d/%d" % (changed, len(slots)))


def build_foliage(master, name, mesh_path, sway_height_cm, sway_amount):
    """One instance PER PLANT, reusing the ship's timber-and-canvas master.

    It was one instance for both until the wind arrived, and the wind is what
    separates them: the bend builds up over the plant's own height, and a palm
    is six metres where a bush is one. Sharing one instance meant sharing one
    height, which would have moved the palm properly and left the scrub - a
    sixth as tall, and the ramp is squared - very nearly rigid."""
    path = MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mi = AT.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                         unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, master)
    for pname, asset in (("BaseTex", "T_Turf_C"), ("BaseTexTop", "T_Turf_C"),
                         ("BaseTexX", "T_Turf_C"),
                         ("NormalTex", "T_Turf_N"), ("NormalTexTop", "T_Turf_N"),
                         ("NormalTexX", "T_Turf_N")):
        t = EAL.load_asset(TEX_DIR + "/" + asset)
        if t is None:
            L("MISSING %s" % asset)
            continue
        MEL.set_material_instance_texture_parameter_value(mi, pname, t)
    MEL.set_material_instance_vector_parameter_value(
        mi, "Tint", unreal.LinearColor(0.85, 1.0, 0.70, 1.0))
    for k, v in (("TexScaleCm", 260.0), ("RoughMin", 0.72), ("RoughMax", 0.95),
                 ("Metallic", 0.0), ("NormalStrength", 1.0),
                 # All three planes. This was one plane, because a blend
                 # between two FRAMES leaves a seam and on a curved frond that
                 # seam is a closed curve - the same ring the sails grew. The
                 # blend is between GRADIENTS now, which is linear and has no
                 # seam to hide, and one plane on a frond meant the whole upper
                 # surface of every leaf and the entire crown of every bush -
                 # about a quarter of the plant, measured - was drawn from a
                 # single stretched column of texels.
                 ("PlaneWeightX", 1.0), ("PlaneWeightY", 1.0),
                 ("PlaneWeightZ", 1.0), ("RopeCollapse", 0.0),
                 ("SwayAmount", sway_amount), ("SwayHeightCm", sway_height_cm)):
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    EAL.save_asset(path)

    mesh = EAL.load_asset(mesh_path)
    if mesh is None:
        L("foliage mesh missing: %s" % mesh_path)
        return
    mesh.set_material(0, mi)
    EAL.save_asset(mesh_path)
    fresh = EAL.load_asset(mesh_path)
    got = fresh.get_editor_property("static_materials")[0].get_editor_property("material_interface")
    L("verify %s -> %s (sway %.2f over %.0f cm)"
      % (mesh_path.split("/")[-1], got.get_name() if got else "NONE",
         sway_amount, sway_height_cm))


master = build_master()
assign(build_instances(master))
# A palm bends over six metres of trunk; a bush is a metre of springy scrub and
# moves proportionally more of itself, so it gets the larger amount over the
# smaller height.
build_foliage(master, "MI_Palm", "/Game/Meshes/SM_Palm", 620.0, 1.0)
build_foliage(master, "MI_Scrub", "/Game/Meshes/SM_Scrub", 110.0, 1.35)
L("DONE")
