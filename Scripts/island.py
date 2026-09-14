# Builds an island: a radial grid whose height is a profile of the radius,
# bent by a little angular noise so it is not a cone.
#
# Radial for the same reason the sea is radial - the detail belongs where a
# hull comes close, which is the shore, not the summit. Rings are dense across
# the beach and coarse inland and offshore.
#
# The profile is the whole design of the shape:
#   r < R_SUMMIT      a rounded summit
#   ... R_SHORE       falls to the waterline
#   ... R_SHELF       a shallow shelf under the water, gentle
#   beyond            the sea floor, flat and far below
#
# R_SHORE and R_SHELF are shared with C++: AIsland uses exactly these two
# numbers for the shoal. That is deliberate. The shoal a ship takes the ground
# on has to be the shape the player can see, and the surest way to make two
# descriptions of one island disagree is to write the shape down twice. So the
# coastline here is a CIRCLE of exactly R_SHORE: the angular relief is windowed
# so it dies at the waterline, which leaves the hill irregular and the shore
# round, and leaves one radius to keep in step instead of a noise function.
#
# Deterministic: the noise comes from a fixed seed, so the island that gets
# measured is the island that gets shipped.
import bpy, bmesh, math, os, sys

ANGULAR = 96
RINGS = 64
R_SUMMIT = 4000.0       # cm
R_SHORE = 11000.0
R_SHELF = 16000.0
R_OUTER = 26000.0
PEAK_Z = 4200.0         # cm above the waterline
SHELF_Z = -150.0        # cm: shallow water over the shelf
# Shallow, and it has to be. The C++ bank runs from R_SHORE out to R_SHELF and
# is described as the ship taking the ground; with the shelf at -900 there were
# nine metres of water under a hull that draws seventy-four centimetres at the
# very edge where she was said to touch. At -150 the whole band really is water
# a ship of this draught has no business in.
FLOOR_Z = -6000.0

# UNITS. Blender models in METRES and the FBX import multiplies by a hundred
# to reach Unreal centimetres - which is why ship.py, whose LENGTH is 30.0,
# arrives as a 30 m ship. The constants below are in CENTIMETRES, because C++
# works in centimetres and has to agree with this file, so every coordinate is
# divided by CM_PER_UNIT on its way into the mesh. Without that division the
# mesh is a hundred times too big and nothing says so: measured, SM_SeaSurface
# was 600 km across instead of 6 km, which put the innermost ring of the
# "centimetres of detail underfoot" grid a quarter of a kilometre from the hull.
CM_PER_UNIT = 100.0

SEED = 20260912
OUT = sys.argv[sys.argv.index("--") + 1]

rng_state = SEED


def rnd():
    """A tiny deterministic generator: no dependence on Python's own RNG."""
    global rng_state
    rng_state = (1103515245 * rng_state + 12345) % (1 << 31)
    return rng_state / float(1 << 31)


# One noise amplitude per angular harmonic, fixed once, so the coastline is
# the same shape all the way up the hill instead of crinkling per ring.
HARMONICS = [(n, 0.18 + 0.24 * rnd(), 2.0 * math.pi * rnd()) for n in (2, 3, 5, 7)]


def relief(theta):
    s = 0.0
    for n, amp, phase in HARMONICS:
        s += amp * math.cos(n * theta + phase)
    return s


def height(r, theta):
    if r <= R_SHORE:
        if r <= R_SUMMIT:
            # Rounded, not pointed: cos gives zero slope at the summit.
            # 0.875/0.125, not 0.75/0.25: the outer branch STARTS at
            # 0.75 * PEAK_Z, so the first version stepped from 0.50 to 0.75 at
            # exactly r = R_SUMMIT - a ten-metre cliff in a ring round the
            # summit, in a shape whose whole point is a shelving beach.
            h = PEAK_Z * (0.875 + 0.125 * math.cos(math.pi * r / R_SUMMIT))
        else:
            t = (r - R_SUMMIT) / (R_SHORE - R_SUMMIT)
            # Smoothstep: flat at the top, flat at the waterline, steep between.
            h = PEAK_Z * 0.75 * (1.0 - (t * t * (3.0 - 2.0 * t)))
        # Ridges and gullies, scaled by a window that is zero at the summit and
        # zero at the waterline. So the hill is irregular and the coastline is
        # a circle of exactly R_SHORE - which is the radius C++ works from.
        window = math.sin(math.pi * r / R_SHORE)
        return h * (1.0 + 0.30 * relief(theta) * window)
    if r <= R_SHELF:
        t = (r - R_SHORE) / (R_SHELF - R_SHORE)
        return SHELF_Z * (t * t * (3.0 - 2.0 * t))
    t = min(1.0, (r - R_SHELF) / (R_OUTER - R_SHELF))
    return SHELF_Z + (FLOOR_Z - SHELF_Z) * (t * t * (3.0 - 2.0 * t))


bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for block in (bpy.data.meshes, bpy.data.objects, bpy.data.materials):
    for item in list(block):
        block.remove(item)

mesh = bpy.data.meshes.new("SM_Island")
bm = bmesh.new()

# Rings bunched around the shore, where a hull gets close enough to care.
radii = []
for i in range(RINGS + 1):
    t = i / float(RINGS)
    # Ease the parameter so samples cluster near R_SHORE.
    bent = t * t * (3.0 - 2.0 * t)
    radii.append(R_OUTER * (0.35 * t + 0.65 * bent))
# R_SHORE itself is a ring. Without it the waterline falls between two rings
# and the DRAWN coastline is a chord across them - close to the circle C++
# works from, but not it, and the difference is invisible in any check that
# reads the formula instead of the mesh.
radii.append(R_SHORE)
radii = sorted(set(radii))

centre_theta = 0.0
centre = bm.verts.new((0.0, 0.0, height(0.0, centre_theta) / CM_PER_UNIT))
rings = []
for r in radii[1:]:
    ring = []
    for a in range(ANGULAR):
        theta = 2.0 * math.pi * a / ANGULAR
        ring.append(bm.verts.new((r * math.cos(theta) / CM_PER_UNIT,
                                  r * math.sin(theta) / CM_PER_UNIT,
                                  height(r, theta) / CM_PER_UNIT)))
    rings.append(ring)
bm.verts.ensure_lookup_table()

for a in range(ANGULAR):
    bm.faces.new((centre, rings[0][a], rings[0][(a + 1) % ANGULAR]))
for i in range(len(rings) - 1):
    inner, outer = rings[i], rings[i + 1]
    for a in range(ANGULAR):
        b = (a + 1) % ANGULAR
        bm.faces.new((inner[a], outer[a], outer[b], inner[b]))

uv = bm.loops.layers.uv.new("UVMap")
for face in bm.faces:
    for loop in face.loops:
        co = loop.vert.co
        loop[uv].uv = (co.x, co.y)

bm.normal_update()
bm.to_mesh(mesh)
bm.free()

obj = bpy.data.objects.new("SM_Island", mesh)
bpy.context.collection.objects.link(obj)

mat = bpy.data.materials.new("M_Island")
mat.diffuse_color = (0.28, 0.24, 0.16, 1.0)
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

above = sum(1 for v in mesh.vertices if v.co.z > 0.0)
# The claim "the coastline is a circle of R_SHORE" needs a check that can FAIL.
# The first one evaluated height() AT R_SHORE, where the profile returns a
# product with an exact zero in it whatever the angle - so it reported 0.000 by
# construction and would have gone on reporting it with the coastline anywhere
# at all. This one finds where the profile actually crosses the waterline, by
# bisection, and reports how far that is from the radius C++ works from.
AUTHORED_SHORE_CM = 11000.0    # must equal AIsland::AuthoredShoreCm


def mesh_waterline_error():
    """Where the BUILT MESH crosses z = 0, column by column, against the
    radius C++ works from. Reading the formula instead of the triangles is how
    the first two versions of this check came back 0.000 by construction."""
    cols = {}
    for v in mesh.vertices:
        x, y, z = v.co.x * CM_PER_UNIT, v.co.y * CM_PER_UNIT, v.co.z * CM_PER_UNIT
        r = math.hypot(x, y)
        if r < 1.0:
            continue
        key = round(math.atan2(y, x), 6)
        cols.setdefault(key, []).append((r, z))
    worst_err = 0.0
    for key, pts in cols.items():
        pts.sort()
        for (r0, z0), (r1, z1) in zip(pts, pts[1:]):
            if (z0 > 0.0) != (z1 > 0.0):
                cross = r0 + (r1 - r0) * (0.0 - z0) / (z1 - z0)
                worst_err = max(worst_err, abs(cross - AUTHORED_SHORE_CM))
                break
    return worst_err


worst = mesh_waterline_error()
assert abs(R_SHORE - AUTHORED_SHORE_CM) < 1e-6, (
    "R_SHORE and AIsland::AuthoredShoreCm have drifted apart")
outer_radius = max(abs(v.co.x) for v in mesh.vertices) * CM_PER_UNIT
print("ISLAND verts=%d tris=%d above_water=%d peak=%.0f shore=%.0f shelf=%.0f "
      "outer=%.0f coast_error=%.3fcm outerRadiusCm=%.0f -> %s"
      % (len(mesh.vertices), len(mesh.polygons) * 2 - ANGULAR, above,
         PEAK_Z, R_SHORE, R_SHELF, R_OUTER, worst, outer_radius, OUT))
