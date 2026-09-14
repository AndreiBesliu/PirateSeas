# Imports the island mesh and writes the material that colours it.
#
# No textures: the colour comes from the height of the ground itself, which is
# the one thing the mesh already knows. Wet sand at the waterline, dry sand up
# the beach, scrub and rock above that. A shoreline drawn this way moves with
# the shape of the land instead of needing UVs that match it.
#
# The mesh is imported with complex collision used as simple, so the land a
# hull runs onto is the land you can see, not a box around it.
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

MAT_DIR = "/Game/Materials"
MESH_DIR = "/Game/Meshes"
ISLE_MAT = MAT_DIR + "/M_Island"
ISLE_MESH = MESH_DIR + "/SM_Island"
FBX = r"C:\Users\besli\Documents\Unreal Projects\PirateSeas\Scripts\SM_Island.fbx"


def L(msg):
    unreal.log("ISLEASSET " + msg)


def sp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:
        L("WARN set %s failed: %s" % (name, exc))
        return False


def link(a, a_out, b, names):
    for name in names:
        if MEL.connect_material_expressions(a, a_out, b, name):
            return True
    L("WARN could not link %s -> %s %s" % (a.get_name(), b.get_name(), names))
    return False


IN1 = ["", "Input", "VectorInput"]
A_IN = ["A"]
B_IN = ["B"]


def build_material():
    # SUPERSEDED by Scripts/island_material.py, which rebuilds M_Island with the
    # height-and-slope blend. This one still built the old vertical colour ramp,
    # it still ran standalone under run_py.ps1, and running it would have
    # silently thrown away the island material without any error - so it refuses
    # rather than sits there waiting to be run by mistake.
    raise RuntimeError(
        "island_assets.build_material() is superseded by island_material.py; "
        "run that instead. Running this would replace M_Island with the old "
        "colour ramp and nothing would report it.")


    if EAL.does_asset_exist(ISLE_MAT):
        EAL.delete_asset(ISLE_MAT)
    mat = AT.create_asset("M_Island", MAT_DIR, unreal.Material,
                          unreal.MaterialFactoryNew())

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def const(value, x, y):
        node = expr(unreal.MaterialExpressionConstant, x, y)
        sp(node, "r", value)
        return node

    def colour(r, g, b, x, y):
        node = expr(unreal.MaterialExpressionConstant3Vector, x, y)
        sp(node, "constant", unreal.LinearColor(r, g, b, 1.0))
        return node

    # Height above the waterline, in the 0..1 the Lerp wants. The island's own
    # origin sits at the waterline, so world Z IS height above the sea.
    world = expr(unreal.MaterialExpressionWorldPosition, -1400, 0)
    z = expr(unreal.MaterialExpressionComponentMask, -1200, 0)
    sp(z, "r", False)
    sp(z, "g", False)
    sp(z, "b", True)
    sp(z, "a", False)
    link(world, "", z, IN1)

    # Beach over the first four metres, scrub from there to the summit.
    beach = expr(unreal.MaterialExpressionDivide, -1000, 0)
    link(z, "", beach, A_IN)
    link(const(700.0, -1200, 140), "", beach, B_IN)
    beach_c = expr(unreal.MaterialExpressionClamp, -820, 0)
    link(beach, "", beach_c, IN1)

    upland = expr(unreal.MaterialExpressionDivide, -1000, 260)
    link(z, "", upland, A_IN)
    link(const(2200.0, -1200, 400), "", upland, B_IN)
    upland_c = expr(unreal.MaterialExpressionClamp, -820, 260)
    link(upland, "", upland_c, IN1)

    # Darker and more saturated than they look on paper. The first pass used
    # values that read as "sand" and "scrub" in a swatch and rendered as a
    # uniform pale dune under this sky: a beach and a hillside a hundred metres
    # apart in height came out the same colour on screen.
    wet = colour(0.09, 0.075, 0.055, -820, -420)
    sand = colour(0.42, 0.36, 0.24, -820, -280)
    scrub = colour(0.055, 0.105, 0.035, -820, 420)

    shore = expr(unreal.MaterialExpressionLinearInterpolate, -560, -200)
    link(wet, "", shore, A_IN)
    link(sand, "", shore, B_IN)
    link(beach_c, "", shore, ["Alpha"])

    ground = expr(unreal.MaterialExpressionLinearInterpolate, -300, 0)
    link(shore, "", ground, A_IN)
    link(scrub, "", ground, B_IN)
    link(upland_c, "", ground, ["Alpha"])

    MEL.connect_material_property(ground, "",
                                  unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(0.92, -300, 220), "",
                                  unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(const(0.0, -300, 340), "",
                                  unreal.MaterialProperty.MP_METALLIC)

    MEL.recompile_material(mat)
    EAL.save_asset(ISLE_MAT)
    L("material=%s" % ISLE_MAT)
    return mat


def fix_up():
    mesh = EAL.load_asset(ISLE_MESH)
    if mesh is None:
        L("ERROR could not load %s" % ISLE_MESH)
        return None
    # The hull must run onto the shape it can see. A convex hull around an
    # island is a box around a beach: it would stop a ship a hundred metres
    # offshore in deep water.
    body = mesh.get_editor_property("body_setup")
    if body:
        sp(body, "collision_trace_flag",
           unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    EAL.save_asset(ISLE_MESH)
    b = mesh.get_bounds()
    L("mesh=%s extent=(%.0f,%.0f,%.0f) tris=%d"
      % (ISLE_MESH, b.box_extent.x, b.box_extent.y, b.box_extent.z,
         mesh.get_num_triangles(0)))
    return mesh


def import_mesh():
    # Import only when it is missing. import_asset_tasks ends by telling the
    # Content Browser to select what it imported, and in a commandlet there is
    # no Slate application to tell: the editor asserts and dies AFTER the mesh
    # is already written and saved. Skipping the import on a second pass lets
    # the fix-up below actually run.
    if EAL.does_asset_exist(ISLE_MESH):
        # Skip the IMPORT, not the fix-up. The first version returned here, so
        # on every run after the first the complex-collision flag below was
        # never set - and the first run is exactly the one that crashes before
        # reaching it.
        L("mesh already present, skipping import")
        return fix_up()

    opts = unreal.FbxImportUI()
    sp(opts, "import_mesh", True)
    sp(opts, "import_as_skeletal", False)
    sp(opts, "import_materials", False)
    sp(opts, "import_textures", False)
    sm = opts.static_mesh_import_data
    sp(sm, "combine_meshes", True)
    sp(sm, "generate_lightmap_u_vs", True)
    # No auto hull: the collision comes from the triangles themselves, below.
    sp(sm, "auto_generate_collision", False)
    sp(sm, "convert_scene", True)

    task = unreal.AssetImportTask()
    sp(task, "filename", FBX)
    sp(task, "destination_path", MESH_DIR)
    sp(task, "destination_name", "SM_Island")
    sp(task, "automated", True)
    sp(task, "replace_existing", True)
    sp(task, "save", True)
    sp(task, "options", opts)
    AT.import_asset_tasks([task])

    return fix_up()


mat = build_material()
mesh = import_mesh()
if mesh and mat:
    mesh.set_material(0, mat)
    EAL.save_asset(ISLE_MESH)
    L("done mesh+material")
