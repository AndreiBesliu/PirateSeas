# Builds the sea surface mesh: a radial grid centred on the ship.
#
# A uniform grid dense enough for a nine-metre wave would need millions of
# triangles to reach the horizon. A radial grid spends its vertices where the
# camera is: rings grow geometrically outwards, so the spacing near the hull is
# centimetres and the spacing at four kilometres is tens of metres, for thirty
# thousand triangles in total.
import bpy, bmesh, math, os, sys

ANGULAR = 160          # segments around
RINGS = 96             # rings outwards
R_INNER = 250.0        # cm, first ring
R_OUTER = 600000.0     # cm, last ring: past the ocean's five kilometres

# UNITS. Blender models in METRES and the FBX import multiplies by a hundred
# to reach Unreal centimetres - which is why ship.py, whose LENGTH is 30.0,
# arrives as a 30 m ship. The constants below are in CENTIMETRES, because C++
# works in centimetres and has to agree with this file, so every coordinate is
# divided by CM_PER_UNIT on its way into the mesh. Without that division the
# mesh is a hundred times too big and nothing says so: measured, SM_SeaSurface
# was 600 km across instead of 6 km, which put the innermost ring of the
# "centimetres of detail underfoot" grid a quarter of a kilometre from the hull.
CM_PER_UNIT = 100.0

OUT = sys.argv[sys.argv.index("--") + 1]

# Clean slate.
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for block in (bpy.data.meshes, bpy.data.objects, bpy.data.materials):
    for item in list(block):
        block.remove(item)

mesh = bpy.data.meshes.new("SM_SeaSurface")
bm = bmesh.new()

ratio = (R_OUTER / R_INNER) ** (1.0 / RINGS)
radii = [R_INNER * (ratio ** i) for i in range(RINGS + 1)]

centre = bm.verts.new((0.0, 0.0, 0.0))
rings = []
for r in radii:
    ring = []
    for a in range(ANGULAR):
        theta = 2.0 * math.pi * a / ANGULAR
        ring.append(bm.verts.new((r * math.cos(theta) / CM_PER_UNIT,
                                  r * math.sin(theta) / CM_PER_UNIT, 0.0)))
    rings.append(ring)
bm.verts.ensure_lookup_table()

# Centre fan.
for a in range(ANGULAR):
    bm.faces.new((centre, rings[0][a], rings[0][(a + 1) % ANGULAR]))

# Quads between successive rings.
for i in range(RINGS):
    inner, outer = rings[i], rings[i + 1]
    for a in range(ANGULAR):
        b = (a + 1) % ANGULAR
        bm.faces.new((inner[a], outer[a], outer[b], inner[b]))

# UVs carry world position in metres, so a detail texture can tile on them
# without the radial layout smearing it.
uv = bm.loops.layers.uv.new("UVMap")
for face in bm.faces:
    for loop in face.loops:
        co = loop.vert.co
        loop[uv].uv = (co.x, co.y)

bm.normal_update()
bm.to_mesh(mesh)
bm.free()

obj = bpy.data.objects.new("SM_SeaSurface", mesh)
bpy.context.collection.objects.link(obj)

mat = bpy.data.materials.new("M_Sea")
mat.diffuse_color = (0.02, 0.12, 0.25, 1.0)
mesh.materials.append(mat)

bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
bpy.context.view_layer.objects.active = obj

bpy.ops.export_scene.fbx(
    filepath=OUT,
    use_selection=True,
    global_scale=1.0,
    apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_NONE",
    object_types={"MESH"},
    mesh_smooth_type="FACE",
    use_mesh_modifiers=False,
    add_leaf_bones=False,
    bake_space_transform=False,
    axis_forward="-Z",
    axis_up="Y",
)

outer_radius = max(abs(v.co.x) for v in mesh.vertices) * CM_PER_UNIT
print("SEA verts=%d tris=%d inner=%.1f outer=%.0f ratio=%.4f outerRadiusCm=%.0f -> %s"
      % (len(mesh.vertices), len(mesh.polygons) * 2 - ANGULAR, R_INNER, R_OUTER,
         ratio, outer_radius, OUT))
