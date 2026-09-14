"""
Re-imports SM_PirateShip over itself, keeping the asset path so BP_PirateShip's
reference survives.

Separate from reimport_meshes.py because that one only imports when the asset
is MISSING - it is a repair tool, not a refresh tool. This one always imports,
which is what you want when the generator changed.

Run it TWICE: import_asset_tasks finishes by telling the Content Browser to
select what it imported, and a commandlet has no Slate to tell, so the editor
dies AFTER the asset is written. The second pass finds the new mesh and only
reports.

It does NOT touch materials. The slots come back named from the FBX and
ship_materials.py assigns them; running this without running that afterwards
leaves the ship in default grey.
"""
import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

ASSET = "/Game/Meshes/SM_PirateShip"
FBX = r"C:\Users\besli\Documents\Unreal Projects\PirateSeas\Scripts\SM_PirateShip.fbx"
# Faces reported by the Blender run that produced the FBX now on disk. If the
# asset comes back with far fewer triangles than this, the import quietly took
# an older file and everything downstream would be judged on stale geometry.
EXPECT_TRIS_AT_LEAST = 5000


def L(m):
    unreal.log("SHIPIMP " + m)


def sp(o, n, v):
    try:
        o.set_editor_property(n, v)
        return True
    except Exception as e:
        L("WARN %s: %s" % (n, str(e)[:80]))
        return False


def stamp():
    mesh = EAL.load_asset(ASSET)
    if mesh is None:
        L("asset MISSING")
        return None
    b = mesh.get_bounds()
    tris = mesh.get_num_triangles(0)
    slots = [str(s.get_editor_property("material_slot_name"))
             for s in mesh.get_editor_property("static_materials")]
    L("tris=%d extent=(%.0f,%.0f,%.0f) slots=%s"
      % (tris, b.box_extent.x, b.box_extent.y, b.box_extent.z, slots))
    return tris, slots


before = stamp()

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
sp(task, "filename", FBX)
sp(task, "destination_path", ASSET.rsplit("/", 1)[0])
sp(task, "destination_name", ASSET.rsplit("/", 1)[1])
sp(task, "automated", True)
sp(task, "replace_existing", True)
sp(task, "save", True)
sp(task, "options", opts)
L("importing %s" % FBX)
AT.import_asset_tasks([task])
L("import returned")

after = stamp()
if after and after[0] < EXPECT_TRIS_AT_LEAST:
    L("SUSPECT only %d triangles - expected at least %d; is the FBX stale?"
      % (after[0], EXPECT_TRIS_AT_LEAST))
L("DONE")
