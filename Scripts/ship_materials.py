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
    # roughness ceiling, metallic, normal strength, TOP-PROJECTION WEIGHT,
    # SIDE-PLANE SWAP (1 = project with (y,z), for anything lying in Y-Z),
    # ROPE COLLAPSE (1 = shrink to a point at distance; only the cordage).
    # The last one is 0 for anything curved: on a bulged surface the biplanar
    # seam is a closed curve, and a closed curve on a sail is a ring.
    "M_Hull":     ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (0.72, 0.62, 0.58), 520.0, 0.45, 0.88, 0.0, 1.0, 1.0, 0.0, 0.0),
    "M_Deck":     ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (1.55, 1.42, 1.18), 330.0, 0.60, 0.95, 0.0, 1.2, 1.0, 0.0, 0.0),
    "M_Wood":     ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (1.05, 0.92, 0.74), 240.0, 0.55, 0.90, 0.0, 0.8, 0.0, 0.0, 0.0),
    "M_DarkWood": ("T_Timber_C", "T_Timber_N", "T_Timber_R",
                   (0.42, 0.36, 0.33), 300.0, 0.40, 0.80, 0.0, 1.0, 1.0, 0.0, 0.0),
    "M_Sail":     ("T_Canvas_C", "T_Canvas_N", None,
                   (1.0, 1.0, 1.0), 900.0, 0.78, 0.96, 0.0, 1.0, 0.0, 1.0, 0.0),
    "M_Iron":     ("T_Iron_C", "T_Iron_N", None,
                   (1.0, 1.0, 1.0), 90.0, 0.32, 0.62, 0.82, 1.0, 0.0, 0.0, 0.0),
    # Tarred hemp, tiled small: a rope is a few centimetres across and the lay
    # has to be visible at that size or the rigging reads as wire.
    "M_Rope":     ("T_Rope_C", "T_Rope_N", None,
                   (1.0, 1.0, 1.0), 45.0, 0.74, 0.94, 0.0, 1.1, 0.0, 0.0, 1.0),
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

    # ------------------------------------------------- the two projections
    # Local space: the texture must travel WITH the hull, not stand still in
    # the world while she sails through it.
    wp = expr(unreal.MaterialExpressionWorldPosition, -2600, 0)
    lp = expr(unreal.MaterialExpressionTransformPosition, -2400, 0)
    sp(lp, "transform_source_type",
       unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    sp(lp, "transform_type",
       unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    link(wp, "", lp, IN1)

    scale = scalar("TexScaleCm", 400.0, -2600, 160)
    luv = div(lp, scale, -2200, 0)
    # Which vertical plane the "side" projection uses. (x, z) is right for a
    # hull, whose length runs along X. It is WRONG for a sail: a sail is
    # modelled in the Y-Z plane, so X is its BULGE - and projecting with the
    # bulge as a texture coordinate draws the contour lines of the bulge, which
    # on a pillow-shaped sail is a set of concentric RINGS. That ring was
    # visible on all four sails from the first textured capture and I blamed it
    # on the mast's shadow, then on the biplanar seam, then on shadow bias.
    # Three runs. It was the coordinate.
    side_xz = mask(luv, -2000, -180, True, False, True)
    side_yz = mask(luv, -2000, -60, False, True, True)
    swap = scalar("SideSwap", 0.0, -2000, 60)
    side_uv = lerp(side_xz, side_yz, swap, -1820, -120)
    top_uv = mask(luv, -2000, 180, True, True, False)     # (x, y)

    # The blend: a vertex normal taken into local space too, so a hull that
    # rolls does not have her planking slide from side projection to top.
    vn = expr(unreal.MaterialExpressionVertexNormalWS, -2600, 320)
    ln = expr(unreal.MaterialExpressionTransform, -2400, 320)
    sp(ln, "transform_source_type",
       unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    sp(ln, "transform_type",
       unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    link(vn, "", ln, IN1)
    nz = absn(mask(ln, -2200, 320, False, False, True), -2050, 320)
    sharp = scalar("ProjectionSharpness", 6.0, -2200, 430)
    # Raising |n.z| to a power makes the changeover between the two
    # projections a narrow band instead of a long smear across the bilge.
    pw = expr(unreal.MaterialExpressionPower, -1900, 400)
    link(nz, "", pw, ["Base"])
    link(sharp, "", pw, ["Exponent", "Exp"])
    # TopWeight kills the second projection where it does more harm than good.
    # On a BULGED surface - a sail, a rope - the locus where the normal crosses
    # the blend threshold is a closed CURVE, so the seam draws a visible RING on
    # every sail in the ship. I spent a capture blaming that on the mast's
    # shadow. A sail is flat-ish and vertical; it only ever needed the side one.
    top_cap = scalar("TopWeight", 1.0, -1900, 540)
    blend = sat(mul(pw, "", top_cap, "", -1750, 400), -1600, 400)

    def biplanar(param, asset, x, y, normal=False, grey=False):
        """Same texture sampled twice, blended by how much the surface faces
        up. Returns the blended sample."""
        a = tex(param, asset, x, y, normal, grey)
        link(side_uv, "", a, ["UVs", "Coordinates"])
        b = tex(param + "Top", asset, x, y + 260, normal, grey)
        link(top_uv, "", b, ["UVs", "Coordinates"])
        return lerp(a, b, blend, x + 320, y + 120)

    # ------------------------------------------------------------- albedo
    alb = biplanar("BaseTex", "T_Timber_C", -1500, -300)
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
    # THE FRAME. A tangent-space normal map is a promise that the mesh has a
    # tangent frame, and a tangent frame is derived from UVs. NOT ONE MESH IN
    # THIS PROJECT HAS UVs - that is the whole reason this material projects.
    # So every bump on the hull, the deck, the sails, the cordage and the
    # island's plants was being tilted in a basis nobody ever defined. It looked
    # like lighting, which is why it went three sessions without being noticed.
    #
    # The obvious repair is to rebuild the sample in the frame of the projection
    # PLANE and hand the engine a world-space normal. It is also wrong, and it
    # was tried first: a plane-frame normal always points along that plane's
    # axis, so it is right only where the surface happens to face the way the
    # plane assumes. The two planes here cover +-Y and +-Z. A MAST FACES +X.
    # The masts came out black, which is what the old capture next to the new
    # one showed, and what no amount of reading the graph had shown.
    #
    # So: keep the geometric normal and TILT it. A frame is built here, per
    # pixel, from the normal itself - which is exactly what the tangent-space
    # path was doing, minus the part where its frame did not exist. The bump
    # detail still comes from the projected sample; only the basis changed.
    sp(mat, "tangent_space_normal", False)
    if mat.get_editor_property("tangent_space_normal"):
        raise RuntimeError("M_ShipMaster: tangent_space_normal did not stick, so "
                           "the engine would read a local-space normal as though "
                           "it were tangent-space")

    nrm = biplanar("NormalTex", "T_Timber_N", -1500, 1100, normal=True)
    nstr = scalar("NormalStrength", 1.0, -1500, 1460)

    # A helper that is deliberately NOT an axis. Crossing the normal with it
    # gives a tangent everywhere except where the surface faces exactly along
    # it, and nothing on this ship - hull, deck, mast, sail, rope, frond - faces
    # (0.31, 0.57, 0.76). Crossing with an axis instead would have gone
    # degenerate on the hull sides or the decks, which is to say on the ship.
    helper = vec3(unreal.LinearColor(0.31, 0.57, 0.76, 1.0), -1150, 1560)
    tang = normalizen(crossn(ln, helper, -950, 1500), -820, 1500)
    bitan = crossn(ln, tang, -680, 1500)

    # Only the map's X and Y tilt the normal; its Z is the part that says "and
    # this much along the surface's own normal", which is already the thing
    # being tilted.
    n_u = mask(nrm, -950, 1180, True, False, False)
    n_v = mask(nrm, -950, 1260, False, True, False)
    tilt = add(mul(tang, "", n_u, "", -560, 1180),
               mul(bitan, "", n_v, "", -560, 1280), -420, 1200)
    # Strength zero is then EXACTLY the geometric normal, with no special case.
    n_local = normalizen(add(ln, mul(tilt, "", nstr, "", -280, 1240),
                             -160, 1200), -60, 1200)

    nworld = expr(unreal.MaterialExpressionTransform, 60, 1200)
    sp(nworld, "transform_source_type",
       unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    sp(nworld, "transform_type",
       unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(n_local, "", nworld, IN1)
    if nworld.get_editor_property("transform_type") != \
            unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD:
        raise RuntimeError("M_ShipMaster: the normal's local-to-world transform "
                           "did not take, and its default is local-to-TANGENT")
    MEL.connect_material_property(nworld, "", unreal.MaterialProperty.MP_NORMAL)

    # ---------------------------------------------------------- roughness
    rtex = biplanar("RoughTex", "T_Timber_R", -1500, 1800, grey=True)
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
    collapse = scalar("RopeCollapse", 0.0, -1500, 2600)
    fade_start = scalar("RopeFadeStartCm", 24000.0, -1500, 2680)
    fade_range = scalar("RopeFadeRangeCm", 16000.0, -1500, 2760)
    depth = expr(unreal.MaterialExpressionPixelDepth, -1500, 2840)
    far = sat(div(sub(depth, fade_start, -1300, 2700), fade_range, -1150, 2700),
              -1000, 2700)
    obj = expr(unreal.MaterialExpressionObjectPositionWS, -1300, 2900)
    toward = sub(obj, wp, -1100, 2880)
    wpo = mul(toward, "", mul(far, "", collapse, "", -900, 2760), "", -700, 2820)
    MEL.connect_material_property(wpo, "",
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
               topw, swap, collapse) in PARTS.items():
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

        T("BaseTex", alb)
        T("NormalTex", nrm)
        if rgh:
            T("RoughTex", rgh)
        MEL.set_material_instance_vector_parameter_value(
            mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        for k, v in (("TexScaleCm", scale), ("RoughMin", rmin),
                     ("RoughMax", rmax), ("Metallic", metal),
                     ("NormalStrength", nstr), ("TopWeight", topw),
                     ("SideSwap", swap), ("RopeCollapse", collapse)):
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


def build_foliage(master):
    """One instance for both plants, reusing the ship's timber-and-canvas
    master. No new material: the master already projects biplanar in LOCAL
    space, which is exactly what a mesh with no UV layer needs, and the palms
    have none for the same reason the ship has none."""
    name = "MI_Foliage"
    path = MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mi = AT.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant,
                         unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, master)
    for pname, asset in (("BaseTex", "T_Turf_C"), ("BaseTexTop", "T_Turf_C"),
                         ("NormalTex", "T_Turf_N"), ("NormalTexTop", "T_Turf_N")):
        t = EAL.load_asset(TEX_DIR + "/" + asset)
        if t is None:
            L("MISSING %s" % asset)
            continue
        MEL.set_material_instance_texture_parameter_value(mi, pname, t)
    MEL.set_material_instance_vector_parameter_value(
        mi, "Tint", unreal.LinearColor(0.85, 1.0, 0.70, 1.0))
    for k, v in (("TexScaleCm", 260.0), ("RoughMin", 0.72), ("RoughMax", 0.95),
                 ("Metallic", 0.0), ("NormalStrength", 1.0),
                 # A palm frond is curved in every direction; a biplanar seam on
                 # it would draw the same ring the sails grew. One plane only.
                 ("TopWeight", 0.0), ("SideSwap", 0.0), ("RopeCollapse", 0.0)):
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    EAL.save_asset(path)

    for mesh_path in ("/Game/Meshes/SM_Palm", "/Game/Meshes/SM_Scrub"):
        mesh = EAL.load_asset(mesh_path)
        if mesh is None:
            L("foliage mesh missing: %s" % mesh_path)
            continue
        mesh.set_material(0, mi)
        EAL.save_asset(mesh_path)
        fresh = EAL.load_asset(mesh_path)
        got = fresh.get_editor_property("static_materials")[0]             .get_editor_property("material_interface")
        L("verify %s -> %s" % (mesh_path.split("/")[-1],
                               got.get_name() if got else "NONE"))


master = build_master()
assign(build_instances(master))
build_foliage(master)
L("DONE")
