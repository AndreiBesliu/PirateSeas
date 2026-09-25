"""
Makes SM_PirateHull collide as its own triangles, and READS IT BACK.

Sets BodySetup.CollisionTraceFlag to CTF_UseComplexAsSimple on the hull skin,
saves, reloads from disk, and prints what the asset now says. This is the step
reimport_hull.py cannot do (the import kills the commandlet), and the read-back
is the whole point: an asset with no collision does not fail, it lets every
ball through and reports 0 hits.
"""
import unreal

ASSET = "/Game/Meshes/SM_PirateHull"
EAL = unreal.EditorAssetLibrary
WANT = unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE


def L(m):
    unreal.log("HULLCOL " + m)


def report(tag, mesh):
    body = mesh.get_editor_property("body_setup")
    agg = body.get_editor_property("agg_geom") if body else None
    b = mesh.get_bounds()
    L("%s tris=%d extent=(%.0f,%.0f,%.0f) convex_elems=%d trace_flag=%s"
      % (tag, mesh.get_num_triangles(0), b.box_extent.x, b.box_extent.y,
         b.box_extent.z,
         len(agg.get_editor_property("convex_elems")) if agg else -1,
         str(body.get_editor_property("collision_trace_flag")) if body else "?"))
    return body


mesh = EAL.load_asset(ASSET)
if mesh is None:
    L("asset MISSING - run reimport_hull.py first")
else:
    body = report("before", mesh)
    if body is None:
        unreal.log_error("HULLCOL no BodySetup on the asset; cannot set collision")
    else:
        body.set_editor_property("collision_trace_flag", WANT)
        mesh.modify()
        EAL.save_asset(ASSET)
        # Read it back from DISK, not from the object just edited.
        mesh2 = EAL.load_asset(ASSET)
        body2 = report("after", mesh2)
        flag = body2.get_editor_property("collision_trace_flag") if body2 else None
        if flag != WANT:
            unreal.log_error("HULLCOL the flag did not stick: %s" % flag)
        else:
            L("DONE complex-as-simple")
