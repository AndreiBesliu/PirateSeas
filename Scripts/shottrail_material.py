"""
Builds M_ShotTrail: the smoke a ball drags behind her.

IT IS FOR A RIBBON NOW, not for instanced cards, and that changes what the
material has to do. The previous version carried a per-instance age in custom
data, because sixty cards of sixty ages shared one draw call. A ribbon carries
its age on the VERTEX COLOUR instead: the mesh is rebuilt every frame anyway, so
the C++ writes each sample's fade straight into the alpha channel. One material,
no parameter that has to be pushed per segment, nothing that can silently bind
to nothing.

WHAT THE CARDS TAUGHT, kept because it cost a whole session: on this project's
InstancedStaticMeshComponent every colour came out BLACK. Proved, not guessed -
a five-value tint sweep on ONE binary and ONE material, where tint 0 and tint 40
gave pixels identical to the unit. It was not the blend mode, the shading model,
the parameter type, per-instance random, the fog, Lumen, the translucency pass
or shader warm-up, and it was not the authoring API either: a material written
through MakeMaterialAttributes was just as black. What settled it was swapping in
M_ShipMaster, which renders brown wood on the hull, and watching the component
draw NOTHING AT ALL. A ProceduralMeshComponent is an ordinary primitive and goes
nowhere near any of that.

THE EROSION IDEA IS KEPT, because it is the right one: alpha is the noise minus a
threshold that climbs as the wisp dies. Multiplying by (1 - age) alone gives
smoke that turns into a ghost; eroding it against noise makes it break into holes
and die ragged, which is what thin smoke does.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT = "/Game/Materials/M_ShotTrail"
A_IN, B_IN = ["A"], ["B"]
IN1 = ["", "Input", "VectorInput"]

FAILURES = []


def L(m):
    unreal.log("TRAILMAT " + m)


def fail(m):
    FAILURES.append(m)
    unreal.log_error("TRAILMAT FAIL " + m)


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
    mat = AT.create_asset("M_ShotTrail", "/Game/Materials", unreal.Material,
                          unreal.MaterialFactoryNew())

    # SET AND READ BACK. sp() swallows a failure into a WARN and run_py.ps1 only
    # fails on a Python exception, so a material that quietly stayed opaque and
    # lit would print "ok" and then draw grey slabs across the sea.
    sp(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    if mat.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        fail("blend_mode did not stick")
    # UNLIT, and the emissive is authored in PHYSICAL UNITS, which is the whole
    # story of why every unlit material in this project renders black.
    #
    # Config/DefaultEngine.ini turns on ExtendDefaultLuminanceRange and fences
    # auto-exposure to EV100 12.5-16, so the renderer divides scene colour by a
    # white point of at least 2^12.5 = 5793 cd/m2. An emissive of 2.35 lands at
    # four ten-thousandths of white and the filmic toe crushes it to nothing;
    # so does 30, and so does 40, which is exactly why a five-value tint sweep
    # came back pixel-identical and looked like a dead input. Lit surfaces are
    # fine because the sun hands them 110,000 lux to reflect.
    #
    # Sunlit powder smoke is about 110000 * 0.85 / pi = 30,000 cd/m2. That is
    # the number this material has to be authored in.
    sp(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    if mat.get_editor_property("shading_model") != unreal.MaterialShadingModel.MSM_UNLIT:
        fail("shading_model did not stick")
    # Two-sided: a ribbon is a surface with no inside. Seen from the other beam
    # it is the same wisp, and one-sided it would vanish the moment the fight
    # crossed the bow.
    sp(mat, "two_sided", True)
    if not mat.get_editor_property("two_sided"):
        fail("two_sided did not stick - the ribbon would vanish from one side")
    # Composite into scene colour rather than the separate-translucency buffer.
    sp(mat, "translucency_pass", unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    # Vertex fog OFF. Measured on the cards: with it on the wisps took the fog's
    # colour instead of their own and read as soot at every tint.
    sp(mat, "use_translucency_vertex_fog", False)

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

    def sat(a, x, y, ao=""):
        n = expr(unreal.MaterialExpressionClamp, x, y)
        sp(n, "min_default", 0.0)
        sp(n, "max_default", 1.0)
        link(a, ao, n, IN1 + ["Input"])
        return n

    # ---------------------------------------------------------------- inputs
    # The C++ writes each sample's fade into the vertex colour's ALPHA. That is
    # the whole ageing mechanism: no per-instance data, no parameter pushed per
    # segment, nothing that can bind to the wrong name and still look alive.
    vcol = expr(unreal.MaterialExpressionVertexColor, -2000, 240)

    # V runs 0..1 ACROSS the ribbon, so |2v - 1| is the distance from its spine.
    uv = expr(unreal.MaterialExpressionTextureCoordinate, -2000, 0)
    sp(uv, "coordinate_index", 0)
    sp(uv, "u_tiling", 1.0)
    sp(uv, "v_tiling", 1.0)
    L("TextureCoordinate readback u=%s v=%s"
      % (uv.get_editor_property("u_tiling"), uv.get_editor_property("v_tiling")))

    # A ComponentMask, not an output called "G": a TextureCoordinate hands back
    # a float2 with no named channels, so asking for "G" fails the link - and
    # the link helper says so out loud rather than leaving the multiply reading
    # a float2 and the ribbon's soft edge silently wrong.
    vonly = expr(unreal.MaterialExpressionComponentMask, -1850, 20)
    sp(vonly, "r", False)
    sp(vonly, "g", True)
    sp(vonly, "b", False)
    sp(vonly, "a", False)
    link(uv, "", vonly, IN1)
    twov = mul(vonly, const(2.0, -1850, 90), -1700, 20)
    shifted = sub(twov, const(1.0, -1700, 150), -1550, 20)
    absv = expr(unreal.MaterialExpressionAbs, -1400, 20)
    link(shifted, "", absv, IN1)
    # A soft edge across the width: 1 on the spine, 0 at both rims, so the
    # ribbon has no hard selvedge and does not read as a strip of tape.
    across = sat(sub(const(1.0, -1250, -50), absv, -1100, 20), -950, 20)

    # -------------------------------------------------------------- the noise
    # World-space procedural, Position deliberately unconnected: the ribbon is
    # carved out of one volume of noise it hangs in, rather than every piece
    # carrying its own copy of the pattern. A cell of about two metres against a
    # ribbon a couple of metres wide is variation; finer is gravel, which the
    # card version rendered and which read as shrapnel rather than smoke.
    noise = expr(unreal.MaterialExpressionNoise, -2000, 480)
    sp(noise, "scale", 0.004)
    sp(noise, "levels", 2)
    sp(noise, "output_min", 0.0)
    sp(noise, "output_max", 1.0)
    sp(noise, "turbulence", True)

    # -------------------------------------------------------------- erosion
    # Thirty per cent curd. More was tried on the cards and made a fresh wisp
    # full of holes, so the trail read as scattered grit instead of a line - and
    # a LINE is the entire reason this exists.
    curd = add(const(0.70, -1700, 560),
               mul(noise, const(0.30, -1700, 640), -1550, 600), -1400, 560)
    body = mul(across, curd, -800, 200)

    # The vertex alpha carries the age, already shaped in C++ as (1-age)^1.4.
    faded = mul(body, vcol, -650, 260, bo="A")

    strength = expr(unreal.MaterialExpressionScalarParameter, -800, 400)
    sp(strength, "parameter_name", "TrailOpacity")
    sp(strength, "default_value", 0.45)
    sp(strength, "group", "Trail")

    opacity = sat(mul(faded, strength, -500, 320), -350, 320)
    if not MEL.connect_material_property(opacity, "",
                                         unreal.MaterialProperty.MP_OPACITY):
        fail("could not connect MP_OPACITY")

    # --------------------------------------------------------------- colour
    # Unlit, so the grey is authored - and it has to be brighter than the sea it
    # is drawn over. A perfectly good 0.72 grey reads as soot against sunlit
    # water, because the water behind it is brighter than that.
    colour = expr(unreal.MaterialExpressionVectorParameter, -800, 660)
    sp(colour, "parameter_name", "TrailColor")
    sp(colour, "default_value", unreal.LinearColor(5000.0, 4900.0, 4700.0, 1.0))
    sp(colour, "group", "Trail")
    back = colour.get_editor_property("default_value")
    L("TrailColor readback r=%.2f g=%.2f b=%.2f" % (back.r, back.g, back.b))
    if max(back.r, back.g, back.b) < 100.0:
        fail("TrailColor is far below the scene white point - it would render BLACK")

    if not MEL.connect_material_property(colour, "",
                                         unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("could not connect MP_EMISSIVE_COLOR")

    MEL.recompile_material(mat)
    EAL.save_asset(MAT)

    # Read the graph back off the BUILT ASSET, not off the objects in hand. The
    # question that matters is what the asset says, and a setter that returned
    # True has been wrong in this project before.
    for name in ("MP_EMISSIVE_COLOR", "MP_OPACITY"):
        node = MEL.get_material_property_input_node(
            mat, getattr(unreal.MaterialProperty, name))
        L("%s -> %s" % (name, node.get_name() if node else "NOTHING"))
        if node is None:
            fail(name + " came back unconnected from the saved asset")

    L("built, expressions=%d" % MEL.get_num_material_expressions(mat))
    L("blend=%s shading=%s twoSided=%s pass=%s fog=%s"
      % (mat.get_editor_property("blend_mode"),
         mat.get_editor_property("shading_model"),
         mat.get_editor_property("two_sided"),
         mat.get_editor_property("translucency_pass"),
         mat.get_editor_property("use_translucency_vertex_fog")))


build()
if FAILURES:
    raise RuntimeError("TRAILMAT %d failures: %s" % (len(FAILURES), FAILURES[:4]))
L("DONE with no failures")
