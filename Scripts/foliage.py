"""
Two plants for the island, in Blender. Headless.

  blender --background --python foliage.py -- <out_dir>

OPAQUE geometry, deliberately. The obvious way to make a palm is a few alpha
cards, but this project has never shipped a masked or translucent material and
the last two attempts to introduce one cost a session each. Solid fronds are
chunkier than cards and they are the honest first tier: what the island needs
most is a BROKEN SILHOUETTE on the skyline, and a silhouette does not care
whether the leaf has a cut-out edge.

No UVs here either - the ship has none and its material handles that by
projecting in local space, so the same material serves these.
"""
import bpy
import bmesh
import math
import os
import sys
from mathutils import Vector

argv = sys.argv
OUT = argv[argv.index("--") + 1] if "--" in argv else os.getcwd()
os.makedirs(OUT, exist_ok=True)


def new_mat(name, colour, rough=0.85):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = colour
    b.inputs["Roughness"].default_value = rough
    return m


def obj_from_bm(bm, name, mat):
    me = bpy.data.meshes.new(name + "Mesh")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    me.materials.append(mat)
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return o


def add_tube(bm, pts, radii, segments=6):
    """A tapered tube through a list of points - trunk, and frond spine."""
    rings = []
    for i, p in enumerate(pts):
        nxt = pts[min(i + 1, len(pts) - 1)]
        prv = pts[max(i - 1, 0)]
        axis = (Vector(nxt) - Vector(prv))
        if axis.length < 1e-6:
            axis = Vector((0, 0, 1))
        axis.normalize()
        ref = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
        ex = axis.cross(ref).normalized()
        ey = axis.cross(ex).normalized()
        ring = []
        for k in range(segments):
            a = 2 * math.pi * k / segments
            ring.append(bm.verts.new(
                Vector(p) + ex * (radii[i] * math.cos(a))
                + ey * (radii[i] * math.sin(a))))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(segments):
            j = (k + 1) % segments
            bm.faces.new((rings[i][k], rings[i][j],
                          rings[i + 1][j], rings[i + 1][k]))
    return rings


def build_palm(mat):
    """Six metres of leaning trunk and a crown of eight fronds.

    The LEAN is the point. A vertical palm reads as a lamp-post; a palm leans
    away from the prevailing wind, and a hillside of them leaning the same way
    is what says "there is weather here" from half a mile off."""
    bm = bmesh.new()
    H, LEAN = 6.2, 1.35
    pts, radii = [], []
    for i in range(7):
        t = i / 6.0
        pts.append((LEAN * t * t, 0.0, H * t))
        radii.append(0.19 * (1.0 - 0.45 * t))
    add_tube(bm, pts, radii, segments=6)

    top = Vector(pts[-1])
    for f in range(8):
        a = 2 * math.pi * f / 8 + 0.3
        out = Vector((math.cos(a), math.sin(a), 0.0))
        # A frond arches out and then droops: three points, the last below the
        # crown. Straight fronds make a starfish.
        fp = [top,
              top + out * 1.1 + Vector((0, 0, 0.55)),
              top + out * 2.3 + Vector((0, 0, 0.15)),
              top + out * 3.1 - Vector((0, 0, 0.95))]
        add_tube(bm, fp, [0.10, 0.34, 0.26, 0.05], segments=4)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj_from_bm(bm, "SM_Palm", mat)


def build_scrub(mat):
    """A low irregular bush: a lumpy dome, not a sphere. The lumps are what
    stop a hillside of them reading as a tray of marbles."""
    bm = bmesh.new()
    import random
    rng = random.Random(11)
    RINGS, SEG, R, H = 4, 8, 1.25, 1.05
    rows = []
    for i in range(RINGS + 1):
        t = i / RINGS
        rr = R * math.cos(t * math.pi * 0.5) ** 0.7
        z = H * math.sin(t * math.pi * 0.5)
        row = []
        for k in range(SEG):
            a = 2 * math.pi * k / SEG
            lump = 1.0 + 0.30 * math.sin(a * 3 + i) + rng.uniform(-0.12, 0.12)
            row.append(bm.verts.new((rr * lump * math.cos(a),
                                     rr * lump * math.sin(a), z)))
        rows.append(row)
    for i in range(RINGS):
        for k in range(SEG):
            j = (k + 1) % SEG
            bm.faces.new((rows[i][k], rows[i][j], rows[i + 1][j], rows[i + 1][k]))
    # cap
    bm.faces.new(tuple(rows[-1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj_from_bm(bm, "SM_Scrub", mat)


def export(obj, name):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    path = os.path.join(OUT, name + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True,
                             apply_unit_scale=True, global_scale=1.0,
                             apply_scale_options="FBX_SCALE_NONE",
                             object_types={"MESH"}, mesh_smooth_type="FACE",
                             add_leaf_bones=False, bake_space_transform=False)
    print("FOLIAGE %s faces=%d dims=%.1f x %.1f x %.1f m"
          % (name, len(obj.data.polygons), obj.dimensions.x,
             obj.dimensions.y, obj.dimensions.z))


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    m = new_mat("M_Foliage", (0.055, 0.095, 0.035, 1))
    export(build_palm(m), "SM_Palm")
    export(build_scrub(m), "SM_Scrub")
    print("FOLIAGE_DONE")


main()
