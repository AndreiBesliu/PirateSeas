"""
Makes SM_PirateHull collide as its own triangles, and checks what it can.

Sets BodySetup.CollisionTraceFlag to CTF_UseComplexAsSimple on the hull skin,
saves (and FAILS if the save reports failure), then checks three things the
game depends on: the flag, that the skin has its bulwark (the top of its
bounds must be above the rail, not at the gunwale - the first skin closed at
the gunwale and let a ball through the very band the gun ports are cut in),
and that it has the triangle count of a closed skin.

What it cannot do: read the asset back from DISK inside the same process -
load_asset returns the object already in memory. The disk truth is the
"before" line of the NEXT run, so run it twice and read the second one; that
is the same discipline as reimport_ship.py.
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
        if not EAL.save_asset(ASSET):
            unreal.log_error("HULLCOL save_asset returned False - nothing on disk changed")
        mesh2 = EAL.load_asset(ASSET)   # the in-memory object; disk = next run's "before"
        body2 = report("after", mesh2)
        flag = body2.get_editor_property("collision_trace_flag") if body2 else None
        top = mesh2.get_bounds().origin.z + mesh2.get_bounds().box_extent.z
        tris = mesh2.get_num_triangles(0)
        ok = True
        if flag != WANT:
            unreal.log_error("HULLCOL the flag did not stick: %s" % flag); ok = False
        # Rail top amidships is ~325 cm and the bow's ~530; a skin closed at the
        # gunwale tops out around 415 at the bow. 480 separates the two.
        if top < 480.0:
            unreal.log_error("HULLCOL skin top at %.0f cm: no bulwark in the collision" % top); ok = False
        if tris < 2400:
            unreal.log_error("HULLCOL only %d triangles: not the closed skin with bulwark" % tris); ok = False
        if ok:
            L("DONE complex-as-simple, top=%.0f cm, tris=%d" % (top, tris))
