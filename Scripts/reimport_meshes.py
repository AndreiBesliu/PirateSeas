# Re-imports the generated meshes and re-attaches what an import forgets.
#
# Separate from the *_assets.py scripts on purpose: those build materials, and
# rebuilding a working material to fix a geometry bug risks the material. This
# only touches geometry, and it is idempotent - it imports when the asset is
# missing and fixes up whatever is there afterwards.
#
# Run it TWICE. import_asset_tasks finishes by telling the Content Browser to
# select what it imported, and in a commandlet there is no Slate application to
# tell: the editor asserts and dies after the asset is already written. The
# second pass finds the asset present, skips the import, and does the fix-up.
import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

SCRIPTS = r"C:\Users\besli\Documents\Unreal Projects\PirateSeas\Scripts"

JOBS = [
    {
        "asset": "/Game/Meshes/SM_Island",
        "name": "SM_Island",
        "fbx": SCRIPTS + r"\SM_Island.fbx",
        "material": "/Game/Materials/M_Island",
        # A hull never touches the island, so complex collision carries no
        # tunnelling risk here; round shot is swept against it and should stop
        # on the shape that is drawn, not on a box around the beach.
        "complex": True,
    },
    {
        "asset": "/Game/Meshes/SM_SeaSurface",
        "name": "SM_SeaSurface",
        "fbx": SCRIPTS + r"\SM_SeaSurface.fbx",
        "material": "/Game/Materials/M_Sea",
        "complex": False,
    },
]


def L(msg):
    unreal.log("REIMPORT " + msg)


def sp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:
        L("WARN set %s failed: %s" % (name, exc))
        return False


def do_import(job):
    opts = unreal.FbxImportUI()
    sp(opts, "import_mesh", True)
    sp(opts, "import_as_skeletal", False)
    sp(opts, "import_materials", False)
    sp(opts, "import_textures", False)
    sm = opts.static_mesh_import_data
    sp(sm, "combine_meshes", True)
    sp(sm, "generate_lightmap_u_vs", True)
    sp(sm, "auto_generate_collision", False)
    sp(sm, "convert_scene", True)

    task = unreal.AssetImportTask()
    sp(task, "filename", job["fbx"])
    sp(task, "destination_path", job["asset"].rsplit("/", 1)[0])
    sp(task, "destination_name", job["name"])
    sp(task, "automated", True)
    sp(task, "replace_existing", True)
    sp(task, "save", True)
    sp(task, "options", opts)
    AT.import_asset_tasks([task])


for job in JOBS:
    if not EAL.does_asset_exist(job["asset"]):
        L("importing %s" % job["name"])
        do_import(job)
        continue

    mesh = EAL.load_asset(job["asset"])
    if mesh is None:
        L("ERROR could not load %s" % job["asset"])
        continue

    if job["complex"]:
        body = mesh.get_editor_property("body_setup")
        if body:
            sp(body, "collision_trace_flag",
               unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)

    mat = EAL.load_asset(job["material"])
    if mat:
        mesh.set_material(0, mat)
    EAL.save_asset(job["asset"])

    # The size is the whole reason this script exists, so it is what gets
    # printed: half-extent in Unreal centimetres, straight off the asset.
    b = mesh.get_bounds()
    L("%s extent=(%.0f,%.0f,%.0f) tris=%d material=%s"
      % (job["name"], b.box_extent.x, b.box_extent.y, b.box_extent.z,
         mesh.get_num_triangles(0), "yes" if mat else "MISSING"))
