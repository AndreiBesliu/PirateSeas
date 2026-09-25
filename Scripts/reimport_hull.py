"""
Imports SM_PirateHull - the hull as a cannonball meets it - from
Scripts/SM_PirateHull.fbx. The asset is the closed hull skin alone.

It CANNOT verify its own result: the commandlet dies the moment the asset is
written, exactly as reimport_ship.py documents, so nothing after
import_asset_tasks() runs. The collision flag and its read-back live in
Scripts/hull_collision.py, which runs afterwards and survives because it only
sets a property and saves. Run this once, then that.

(The first version exported six UCX_ convex slabs and expected the importer to
take them as simple collision. It dropped them silently: convex_elems=0, which
the game would have reported as "0 hits". Complex-as-simple on the skin needs
no import convention at all.)
"""
import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
ESML = unreal.EditorStaticMeshLibrary

ASSET = "/Game/Meshes/SM_PirateHull"
FBX = r"C:\Users\besli\Documents\Unreal Projects\PirateSeas\Scripts\SM_PirateHull.fbx"


def L(m):
    unreal.log("HULLCOL " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s: %s" % (n, str(e)[:80]))
        return False


def report(tag):
    mesh = EAL.load_asset(ASSET)
    if mesh is None:
        L("%s asset MISSING" % tag)
        return None, 0
    b = mesh.get_bounds()
    convex = ESML.get_convex_collision_count(mesh)
    simple = ESML.get_simple_collision_count(mesh)
    L("%s tris=%d extent=(%.0f,%.0f,%.0f) convex=%d simple=%d"
      % (tag, mesh.get_num_triangles(0), b.box_extent.x, b.box_extent.y,
         b.box_extent.z, convex, simple))
    return mesh, convex


report("before")

opts = unreal.FbxImportUI()
sp(opts, "import_mesh", True)
sp(opts, "import_as_skeletal", False)
sp(opts, "import_materials", False)
sp(opts, "import_textures", False)
sm = opts.static_mesh_import_data
# ONE mesh: the UCX_ pieces attach to the mesh whose name they carry, and the
# render skin is that mesh.
sp(sm, "combine_meshes", True)
sp(sm, "generate_lightmap_u_vs", False)
# Off, or the importer would ALSO build its own box/DOP and the count below
# could read 7 with the six real slabs plus one lie.
sp(sm, "auto_generate_collision", False)
sp(sm, "convert_scene", True)

task = unreal.AssetImportTask()
sp(task, "filename", FBX)
sp(task, "destination_path", ASSET.rsplit("/", 1)[0])
sp(task, "destination_name", ASSET.rsplit("/", 1)[1])
sp(task, "automated", True)
sp(task, "replace_existing", True)
sp(task, "save", True)
sp(task, "options", opts)
L("importing %s" % FBX)
AT.import_asset_tasks([task])

# Nothing runs past here: the commandlet exits inside import_asset_tasks.
