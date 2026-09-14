# Prints what the imported meshes actually measure, in Unreal centimetres.
#
# Written because the island came into the world a hundred times too big:
# Blender numbers leave as metres and Unreal reads them as centimetres. The
# same export settings built the sea, so the same question has to be asked of
# it before anything is fixed in one place only.
import unreal

EAL = unreal.EditorAssetLibrary

for path in ("/Game/Meshes/SM_Island", "/Game/Meshes/SM_SeaSurface",
             "/Game/Meshes/SM_PirateShip"):
    mesh = EAL.load_asset(path)
    if mesh is None:
        unreal.log("BOUNDS %s MISSING" % path)
        continue
    b = mesh.get_bounds()
    unreal.log("BOUNDS %s origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f) tris=%d"
               % (path, b.origin.x, b.origin.y, b.origin.z,
                  b.box_extent.x, b.box_extent.y, b.box_extent.z,
                  mesh.get_num_triangles(0)))
