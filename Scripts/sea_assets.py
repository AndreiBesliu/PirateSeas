# Builds the sea surface assets: the material that moves the water, and the
# import of the radial grid it moves.
#
# The material displaces vertices by the SAME Gerstner sum the buoyancy reads,
# so the surface you see is the surface the hull floats on. The wave set is not
# baked in: C++ reads it off the water body at BeginPlay and pushes it into the
# material instance, six waves at a time.
#
# Per wave, with P the world XY of the vertex:
#     phase = dot(P, WaveVector) - WaveSpeed * Time
#     z    += Amplitude * cos(phase)
#     xy   -= WaveVector * (QoverK * sin(phase))
# WaveVector is the direction times the wave number, so passing Q/k instead of
# Q lets the whole horizontal term reuse WaveVector with no division and no
# normalise, which also means an unused wave (all zeros) contributes exactly
# zero instead of a NaN.
import math
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT_DIR = "/Game/Materials"
MESH_DIR = "/Game/Meshes"
SEA_MAT = MAT_DIR + "/M_Sea"
SEA_MESH = MESH_DIR + "/SM_SeaSurface"
FBX = r"C:\Users\besli\AppData\Local\Temp\claude\C--Users-besli-Desktop-MyWork-Apps\dee4a521-4b0b-4d9a-9072-6918d586207b\scratchpad\SM_SeaSurface.fbx"

WAVES = 6
TWO_PI = 2.0 * math.pi


def L(msg):
    unreal.log("SEAASSET " + msg)


def sp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:
        L("WARN set %s failed: %s" % (name, exc))
        return False


def link(a, a_out, b, b_in_candidates):
    """Connect two expressions, trying the input pin names the API may use."""
    for name in b_in_candidates:
        if MEL.connect_material_expressions(a, a_out, b, name):
            return True
    L("WARN could not link %s -> %s %s" % (a.get_name(), b.get_name(), b_in_candidates))
    return False


IN1 = ["", "Input", "VectorInput"]
A_IN = ["A"]
B_IN = ["B"]


def build_material():
    if EAL.does_asset_exist(SEA_MAT):
        EAL.delete_asset(SEA_MAT)
    mat = AT.create_asset("M_Sea", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    # Seen from under the surface as well, when a sinking ship takes the
    # camera down with it.
    sp(mat, "two_sided", True)
    # The normal is computed in world space from the wave slopes, not sampled
    # from a texture in tangent space.
    sp(mat, "tangent_space_normal", False)

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def const(value, x, y):
        node = expr(unreal.MaterialExpressionConstant, x, y)
        sp(node, "r", value)
        return node

    def mul(a, a_out, b, b_out, x, y):
        node = expr(unreal.MaterialExpressionMultiply, x, y)
        link(a, a_out, node, A_IN)
        link(b, b_out, node, B_IN)
        return node

    def add(a, b, x, y):
        node = expr(unreal.MaterialExpressionAdd, x, y)
        link(a, "", node, A_IN)
        link(b, "", node, B_IN)
        return node

    # ---- shared inputs -------------------------------------------------
    world = expr(unreal.MaterialExpressionWorldPosition, -2400, 0)
    world_xy = expr(unreal.MaterialExpressionComponentMask, -2200, 0)
    sp(world_xy, "r", True)
    sp(world_xy, "g", True)
    sp(world_xy, "b", False)
    sp(world_xy, "a", False)
    link(world, "", world_xy, IN1)

    time = expr(unreal.MaterialExpressionScalarParameter, -2400, 140)
    sp(time, "parameter_name", "WaveTime")
    sp(time, "default_value", 0.0)

    neg_one = const(-1.0, -2400, 220)

    offsets = []
    normals = []
    for i in range(WAVES):
        y0 = -900 + i * 420

        # (WaveVector.X, WaveVector.Y, WaveSpeed, Amplitude)
        wave = expr(unreal.MaterialExpressionVectorParameter, -2000, y0)
        sp(wave, "parameter_name", "WaveA%d" % i)
        sp(wave, "default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))

        # Q divided by the wave number, so the horizontal term can reuse the
        # wave vector directly.
        qk = expr(unreal.MaterialExpressionScalarParameter, -2000, y0 + 150)
        sp(qk, "parameter_name", "WaveQK%d" % i)
        sp(qk, "default_value", 0.0)

        wave_xy = expr(unreal.MaterialExpressionComponentMask, -1800, y0)
        sp(wave_xy, "r", True)
        sp(wave_xy, "g", True)
        sp(wave_xy, "b", False)
        sp(wave_xy, "a", False)
        link(wave, "", wave_xy, IN1)

        dot = expr(unreal.MaterialExpressionDotProduct, -1600, y0)
        link(world_xy, "", dot, A_IN)
        link(wave_xy, "", dot, B_IN)

        wt = mul(wave, "B", time, "", -1600, y0 + 150)

        phase = expr(unreal.MaterialExpressionSubtract, -1400, y0)
        link(dot, "", phase, A_IN)
        link(wt, "", phase, B_IN)

        # The Sine node computes sin(x * 2pi / Period), so a period of 2pi is
        # what makes it a plain sine of its input.
        sine = expr(unreal.MaterialExpressionSine, -1200, y0)
        sp(sine, "period", TWO_PI)
        link(phase, "", sine, IN1)

        cosine = expr(unreal.MaterialExpressionCosine, -1200, y0 + 150)
        sp(cosine, "period", TWO_PI)
        link(phase, "", cosine, IN1)

        height = mul(cosine, "", wave, "A", -1000, y0 + 150)

        swing = mul(sine, "", qk, "", -1000, y0)
        swing_neg = mul(swing, "", neg_one, "", -850, y0)
        horizontal = mul(wave_xy, "", swing_neg, "", -700, y0)

        offset = expr(unreal.MaterialExpressionAppendVector, -520, y0)
        link(horizontal, "", offset, A_IN)
        link(height, "", offset, B_IN)
        offsets.append(offset)

        # Slope, for the normal: direction * sin * waveNumber * amplitude,
        # which is just the wave vector times sin times amplitude.
        slope_scale = mul(sine, "", wave, "A", -1000, y0 + 300)
        normals.append(mul(wave_xy, "", slope_scale, "", -850, y0 + 300))

    # ---- sums ----------------------------------------------------------
    total_offset = offsets[0]
    for i in range(1, WAVES):
        total_offset = add(total_offset, offsets[i], -300 + i * 20, -900 + i * 90)
    MEL.connect_material_property(total_offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    total_slope = normals[0]
    for i in range(1, WAVES):
        total_slope = add(total_slope, normals[i], -300 + i * 20, 400 + i * 90)

    one = const(1.0, -300, 900)
    normal_vec = expr(unreal.MaterialExpressionAppendVector, -150, 860)
    link(total_slope, "", normal_vec, A_IN)
    link(one, "", normal_vec, B_IN)
    normalised = expr(unreal.MaterialExpressionNormalize, 0, 860)
    link(normal_vec, "", normalised, IN1)
    MEL.connect_material_property(normalised, "", unreal.MaterialProperty.MP_NORMAL)

    # ---- surface -------------------------------------------------------
    colour = expr(unreal.MaterialExpressionVectorParameter, -300, 1050)
    sp(colour, "parameter_name", "DeepColor")
    sp(colour, "default_value", unreal.LinearColor(0.012, 0.048, 0.085, 1.0))
    MEL.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = expr(unreal.MaterialExpressionScalarParameter, -300, 1180)
    sp(rough, "parameter_name", "Roughness")
    sp(rough, "default_value", 0.06)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    spec = expr(unreal.MaterialExpressionScalarParameter, -300, 1280)
    sp(spec, "parameter_name", "Specular")
    sp(spec, "default_value", 1.0)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    metal = expr(unreal.MaterialExpressionScalarParameter, -300, 1380)
    sp(metal, "parameter_name", "Metallic")
    sp(metal, "default_value", 0.0)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)

    MEL.recompile_material(mat)
    EAL.save_asset(SEA_MAT)
    L("material built, expressions=%d" % MEL.get_num_material_expressions(mat))
    return mat


def import_mesh():
    opts = unreal.FbxImportUI()
    sp(opts, "import_mesh", True)
    sp(opts, "import_as_skeletal", False)
    sp(opts, "import_materials", False)
    sp(opts, "import_textures", False)
    sm = opts.static_mesh_import_data
    sp(sm, "combine_meshes", True)
    sp(sm, "generate_lightmap_u_vs", False)
    sp(sm, "auto_generate_collision", False)
    sp(sm, "convert_scene", True)

    task = unreal.AssetImportTask()
    sp(task, "filename", FBX)
    sp(task, "destination_path", MESH_DIR)
    sp(task, "destination_name", "SM_SeaSurface")
    sp(task, "automated", True)
    sp(task, "replace_existing", True)
    sp(task, "save", True)
    sp(task, "options", opts)
    AT.import_asset_tasks([task])


if EAL.does_asset_exist(SEA_MESH):
    L("mesh already present, skipping import")
else:
    build_material()
    L("importing the grid; the task may take the process down AFTER it lands")
    import_mesh()
    L("import returned")

if EAL.does_asset_exist(SEA_MAT):
    L("material asset present")
L("done, mesh=%s" % EAL.does_asset_exist(SEA_MESH))
