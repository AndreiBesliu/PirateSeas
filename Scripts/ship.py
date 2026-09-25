"""
Procedural stylised pirate brig for Blender 4.3 (headless).
Modelled in metres, ~30 m long, origin at the waterline centre.

  blender --background --python ship.py -- <out_dir>
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

LENGTH = 30.0        # stern to bow
BEAM = 8.4           # max width
DRAFT = 2.6          # keel below waterline
FREEBOARD = 2.1      # deck above waterline amidships
SHEER = 1.5          # extra deck rise towards bow/stern
BULWARK = 1.15       # rail height above deck

NS = 46              # stations along the hull
NR = 12              # points per half section


# ------------------------------------------------------------------ helpers
def smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def half_beam(t):
    """t: 0 = stern, 1 = bow."""
    if t < 0.45:
        f = 0.55 + 0.45 * smoothstep(0.0, 1.0, t / 0.45)
    else:
        f = 1.0 - 0.945 * smoothstep(0.0, 1.0, (t - 0.45) / 0.55) ** 1.35
    return 0.5 * BEAM * f


def draft(t):
    if t <= 0.58:
        return DRAFT * (0.86 + 0.14 * (t / 0.58))
    return DRAFT * (1.0 - 0.72 * smoothstep(0.0, 1.0, (t - 0.58) / 0.42) ** 1.2)


def deck_z(t):
    """Sheer line: deck rises towards both ends, more at the bow."""
    s = (2.0 * t - 1.0) ** 2
    bow_extra = 0.55 * smoothstep(0.55, 1.0, t)
    return FREEBOARD + SHEER * s + bow_extra


def station_x(t):
    return (t - 0.5) * LENGTH


def section_point(t, v):
    """v: 0 = keel, 1 = gunwale. Returns (y, z) for the starboard side."""
    a = v * (math.pi * 0.5)
    d = draft(t)
    fz = deck_z(t)
    y = half_beam(t) * math.sin(a) ** 1.12
    z = fz - (d + fz) * math.cos(a) ** 1.25
    return y, z


def new_mat(name, color, rough=0.75, metallic=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = color
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metallic
    return m


def obj_from_bm(bm, name, mat, smooth=True):
    me = bpy.data.meshes.new(name + "Mesh")
    bm.to_mesh(me)
    bm.free()
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    me.materials.append(mat)
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return o


# ------------------------------------------------------------------ hull
def build_hull(mat_hull, mat_deck):
    bm = bmesh.new()

    # grid of verts: [station][ring] for starboard, mirrored to port
    grid_s, grid_p = [], []
    for i in range(NS + 1):
        t = i / NS
        x = station_x(t)
        col_s, col_p = [], []
        for j in range(NR + 1):
            v = j / NR
            y, z = section_point(t, v)
            col_s.append(bm.verts.new((x, y, z)))
            col_p.append(bm.verts.new((x, -y, z)))
        grid_s.append(col_s)
        grid_p.append(col_p)
    bm.verts.ensure_lookup_table()

    for i in range(NS):
        for j in range(NR):
            bm.faces.new((grid_s[i][j], grid_s[i + 1][j],
                          grid_s[i + 1][j + 1], grid_s[i][j + 1]))
            bm.faces.new((grid_p[i][j], grid_p[i][j + 1],
                          grid_p[i + 1][j + 1], grid_p[i + 1][j]))

    # keel seam: stitch the two halves along the centreline (j = 0)
    for i in range(NS):
        bm.faces.new((grid_s[i][0], grid_p[i][0],
                      grid_p[i + 1][0], grid_s[i + 1][0]))

    # transom at the stern
    stern_ring = [grid_s[0][j] for j in range(NR + 1)] + \
                 [grid_p[0][j] for j in range(NR, -1, -1)]
    bm.faces.new(stern_ring[::-1])

    # bow cap
    bow_ring = [grid_s[NS][j] for j in range(NR + 1)] + \
               [grid_p[NS][j] for j in range(NR, -1, -1)]
    bm.faces.new(bow_ring)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    hull = obj_from_bm(bm, "Hull", mat_hull)

    # ---- deck: inset planking surface just below the gunwale ----------
    bm = bmesh.new()
    ds, dp = [], []
    for i in range(NS + 1):
        t = i / NS
        x = station_x(t)
        y, z = section_point(t, 0.965)
        ds.append(bm.verts.new((x, y * 0.94, z - 0.12)))
        dp.append(bm.verts.new((x, -y * 0.94, z - 0.12)))
    bm.verts.ensure_lookup_table()
    for i in range(NS):
        bm.faces.new((ds[i], dp[i], dp[i + 1], ds[i + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    deck = obj_from_bm(bm, "Deck", mat_deck, smooth=False)

    # ---- bulwark: rail wall standing on the deck edge ------------------
    bm = bmesh.new()
    for sign in (1, -1):
        lo, hi = [], []
        for i in range(NS + 1):
            t = i / NS
            x = station_x(t)
            y, z = section_point(t, 1.0)
            lo.append(bm.verts.new((x, sign * y, z - 0.15)))
            hi.append(bm.verts.new((x, sign * y * 0.985, z + BULWARK)))
        bm.verts.ensure_lookup_table()
        for i in range(NS):
            if sign > 0:
                bm.faces.new((lo[i], lo[i + 1], hi[i + 1], hi[i]))
            else:
                bm.faces.new((lo[i], hi[i], hi[i + 1], lo[i + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    rail = obj_from_bm(bm, "Bulwark", mat_hull, smooth=False)

    return hull, deck, rail


# ------------------------------------------------------------------ parts
def cylinder(name, mat, radius, height, location, rotation=(0, 0, 0), verts=12,
             taper=1.0):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=verts,
                          radius1=radius, radius2=radius * taper, depth=height)
    o = obj_from_bm(bm, name, mat)
    o.location = location
    o.rotation_euler = rotation
    return o


def box(name, mat, size, location, rotation=(0, 0, 0)):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x *= size[0]
        v.co.y *= size[1]
        v.co.z *= size[2]
    o = obj_from_bm(bm, name, mat, smooth=False)
    o.location = location
    o.rotation_euler = rotation
    return o


# How full a square sail is cut, as a FRACTION OF ITS WIDTH rather than as a
# number of metres. Eleven per cent is an ordinary working draft; the old code
# used a flat 0.9 m, which made a nine-metre course and a seven-metre topsail
# equally deep and therefore differently shaped.
SAIL_CAMBER = 0.11

# The foot is cut with a roach - a convex curve that hangs lower amidships than
# at the clews - again as a fraction of the width.
SAIL_ROACH = 0.055


def sail_surface(u, w, width, height):
    """Where a point of the sail sits, for u across (-0.5 to 0.5) and w down
    (0 at the head, 1 at the foot).

    ONE function, used both by the mesh and by the rigging, because they
    disagreed: build_cordage seized each sheet at `x + 0.35`, a guess at where
    the clew had ended up, while the clew is in fact at zero camber - a sail is
    held at its corners. The sheets were tied thirty-five centimetres in front
    of the corner they were supposed to be tied to.

    The shape itself:
      HEAD    laced to the yard along its whole length, so camber is zero there
              and the head is dead straight.
      FOOT    FREE, held only at the two clews. The old shape pinned it flat,
              which is why the sails read as pillows rather than as canvas: a
              square sail's foot is the part that bellies most.
      LEECHES held by their bolt ropes, so camber goes to zero at both sides.
    """
    # Across the sail: full in the middle, nothing at the leeches.
    across = math.cos(u * math.pi) ** 0.85
    # Down the sail: nothing at the head, full at the foot, and staying full
    # rather than closing again.
    down = math.sin(w * math.pi * 0.5) ** 0.8
    bulge = SAIL_CAMBER * width * across * down
    # The roach: the foot hangs lower in the middle, and nothing happens at the
    # head because it scales with w squared.
    drop = w * height + (w ** 2) * SAIL_ROACH * width * math.cos(u * math.pi)
    return (bulge, u * width, -drop)


def sail_clew(width, height, side):
    """The lower corner on one side, in the sail's own space. The rigging asks
    this instead of guessing, which is the whole point of it existing."""
    return sail_surface(0.5 * side, 1.0, width, height)


def sail(name, mat, width, height, location, nx=12, nz=10):
    """A square sail: head laced straight to the yard, foot free and full."""
    bm = bmesh.new()
    verts = []
    for k in range(nz + 1):
        row = []
        for i in range(nx + 1):
            u = i / nx - 0.5
            w = k / nz
            row.append(bm.verts.new(sail_surface(u, w, width, height)))
        verts.append(row)
    bm.verts.ensure_lookup_table()
    for k in range(nz):
        for i in range(nx):
            bm.faces.new((verts[k][i], verts[k][i + 1],
                          verts[k + 1][i + 1], verts[k + 1][i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    solid = bmesh.ops.solidify(bm, geom=bm.faces[:] + bm.edges[:] + bm.verts[:],
                               thickness=0.04)
    o = obj_from_bm(bm, name, mat)
    o.location = location
    return o


# ------------------------------------------------------------------ cordage
MAST_RAKE_DEG = -3.0
MASTS = (("Fore", 5.6, 20.0, 0.34), ("Main", -3.2, 24.0, 0.40))


def mast_line(x, h):
    """Where a mast actually is, given that cylinder() centres on its location
    and then rakes it. Computed rather than eyeballed, because a shroud that
    misses the masthead by twenty centimetres is a shroud that is visibly
    tied to nothing."""
    th = math.radians(MAST_RAKE_DEG)
    axis = Vector((math.sin(th), 0.0, math.cos(th)))
    centre = Vector((x, 0.0, deck_z(0.5) + h * 0.5 - 1.0))
    return centre - axis * (h * 0.5), axis


def add_tube(bm, p0, p1, radius, segments=4):
    """One rope, as an untapered tube between two points. No end caps: at three
    centimetres across, a cap is a triangle nobody will ever see, and there are
    several hundred of these."""
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    if d.length < 1e-5:
        return
    z = d.normalized()
    ref = Vector((0, 0, 1)) if abs(z.z) < 0.9 else Vector((1, 0, 0))
    ex = z.cross(ref).normalized()
    ey = z.cross(ex).normalized()
    rings = []
    for p in (p0, p1):
        ring = []
        for i in range(segments):
            a = 2.0 * math.pi * i / segments
            ring.append(bm.verts.new(
                p + ex * (radius * math.cos(a)) + ey * (radius * math.sin(a))))
        rings.append(ring)
    for i in range(segments):
        j = (i + 1) % segments
        bm.faces.new((rings[0][i], rings[0][j], rings[1][j], rings[1][i]))


def build_cordage(m_rope):
    """Shrouds, ratlines, stays, braces and sheets - all in ONE mesh.

    This is the single loudest thing missing from the ship. A square-rigged
    hull with bare masts does not read as under-detailed, it reads as a TOY:
    the eye knows that masts that size cannot stand up without standing
    rigging, even when it could not name what it is looking for.

    Built as one object because there are some three hundred separate ropes
    here and three hundred draw calls for eight hundred grams of hemp would be
    absurd."""
    bm = bmesh.new()
    R_SHROUD, R_RAT, R_STAY, R_RUN = 0.036, 0.022, 0.038, 0.026

    # ---- shrouds and ratlines ------------------------------------------
    for tag, x, h, r in MASTS:
        base, axis = mast_line(x, h)
        # The hounds: where the shrouds are seized to the mast, just under the
        # masthead.
        hound = base + axis * (h * 0.80)
        spread = (-1.9, -0.6, 0.7, 2.0)      # deck ends, fore-and-aft of the mast
        for side in (1, -1):
            feet = []
            for k, dx in enumerate(spread):
                t = min(0.94, max(0.06, (x + dx) / LENGTH + 0.5))
                y, z = section_point(t, 1.0)
                # Outboard of the rail, the way a channel puts them, so the
                # shrouds clear the gunwale instead of grazing it.
                foot = Vector((station_x(t), side * (y + 0.30), z + 0.10))
                # Fanned at the mast too, or all four meet in one point and the
                # whole thing looks like a tent.
                top = hound + Vector((0.0, side * (0.16 + 0.05 * k), -0.10 * k))
                add_tube(bm, foot, top, R_SHROUD, segments=5)
                feet.append((foot, top))

            # Ratlines: rungs between neighbouring shrouds, every sixteen
            # inches or so, stopping short of the hounds the way real ones do.
            # Ratlines stop at the top, which on a real ship is the platform
            # under the topmast - about where the lower yard is. Run to the
            # hounds instead and they climb straight across the upper sail and
            # the whole rig turns into a thicket.
            rung = 0.46
            climb_to = 0.56
            n_rungs = int((feet[0][1] - feet[0][0]).length * climb_to / rung)
            for i in range(1, n_rungs):
                f = i * rung / (feet[0][1] - feet[0][0]).length
                for a, b in zip(feet, feet[1:]):
                    pa = a[0].lerp(a[1], f)
                    pb = b[0].lerp(b[1], f)
                    add_tube(bm, pa, pb, R_RAT, segments=3)

    # ---- standing rigging fore and aft ----------------------------------
    fore_base, fore_axis = mast_line(MASTS[0][1], MASTS[0][2])
    main_base, main_axis = mast_line(MASTS[1][1], MASTS[1][2])
    fore_head = fore_base + fore_axis * (MASTS[0][2] * 0.84)
    main_head = main_base + main_axis * (MASTS[1][2] * 0.84)

    # forestay, from the fore masthead down to the bowsprit end
    bows_end = Vector((LENGTH * 0.5 + 2.6, 0.0, deck_z(1.0) + 0.9)) + \
        Vector((math.sin(math.radians(74)), 0.0, math.cos(math.radians(74)))) * 4.75
    add_tube(bm, fore_head, bows_end, R_STAY, segments=5)
    # mainstay: main masthead forward to the foot of the fore mast
    add_tube(bm, main_head, fore_base + fore_axis * 1.2, R_STAY, segments=5)

    # backstays: each masthead to the quarters
    for head in (fore_head, main_head):
        for side in (1, -1):
            t = 0.12
            y, z = section_point(t, 1.0)
            add_tube(bm, head, Vector((station_x(t), side * (y + 0.25), z + 0.10)),
                     R_STAY * 0.85, segments=5)

    # ---- running rigging: braces and sheets ------------------------------
    # Braces lead aft from the yard-arms; sheets lead down from the clews. They
    # do nothing and they are the reason the rig looks WORKED rather than
    # assembled.
    for tag, x, h, r in MASTS:
        base, axis = mast_line(x, h)
        for k, (frac, wf) in enumerate(((0.42, 1.0), (0.70, 0.78))):
            z = deck_z(0.5) + h * frac
            yard_len = (10.5 if tag == "Main" else 9.0) * wf
            sail_w = yard_len * 0.94
            sail_h = h * (0.26 if k == 0 else 0.22)
            for side in (1, -1):
                arm = Vector((x, side * yard_len * 0.5, z))
                # brace, aft and down to the rail
                t = 0.16 if tag == "Main" else 0.34
                y, zz = section_point(t, 1.0)
                add_tube(bm, arm, Vector((station_x(t), side * (y + 0.2), zz + 0.6)),
                         R_RUN, segments=4)
                # sheet, from the sail's lower corner down to the deck. The
                # corner is ASKED FOR, not guessed: sail_clew() returns the same
                # point the mesh is built from, so the two cannot drift apart
                # when the shape changes.
                cx, cy, cz = sail_clew(sail_w, sail_h, side)
                clew = Vector((x + cx, cy, z - 0.15 + cz))
                t2 = min(0.92, max(0.08, (x - 2.4) / LENGTH + 0.5))
                y2, z2 = section_point(t2, 1.0)
                add_tube(bm, clew,
                         Vector((station_x(t2), side * (y2 + 0.1), z2 + 0.3)),
                         R_RUN * 0.85, segments=4)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ropes = len(bm.faces)
    o = obj_from_bm(bm, "Cordage", m_rope)
    print("SHIP cordage quads=%d" % ropes)
    return o


def build_rig(m_wood, m_dark, m_sail, m_metal):
    parts = []
    # masts: fore and main, raked slightly aft
    for tag, x, h, r in MASTS:
        parts.append(cylinder("Mast" + tag, m_wood, r, h,
                              (x, 0, deck_z(0.5) + h * 0.5 - 1.0),
                              rotation=(0, math.radians(MAST_RAKE_DEG), 0),
                              taper=0.75))
        # yards + sails
        for k, (frac, wf) in enumerate(((0.42, 1.0), (0.70, 0.78))):
            z = deck_z(0.5) + h * frac
            yard_len = (10.5 if tag == "Main" else 9.0) * wf
            parts.append(cylinder("Yard%s%d" % (tag, k), m_wood, 0.17, yard_len,
                                  (x, 0, z), rotation=(math.radians(90), 0, 0),
                                  verts=8, taper=0.6))
            parts.append(sail("Sail%s%d" % (tag, k), m_sail,
                              yard_len * 0.94, h * (0.26 if k == 0 else 0.22),
                              (x, 0, z - 0.15)))
    # bowsprit
    parts.append(cylinder("Bowsprit", m_wood, 0.26, 9.5,
                          (LENGTH * 0.5 + 2.6, 0, deck_z(1.0) + 0.9),
                          rotation=(0, math.radians(74), 0), taper=0.5))
    # stern cabin
    parts.append(box("SternCabin", m_dark, (5.0, 4.2, 2.0),
                     (-LENGTH * 0.5 + 3.4, 0, deck_z(0.05) + 1.05)))
    # rudder
    parts.append(box("Rudder", m_dark, (0.35, 0.22, 3.6),
                     (-LENGTH * 0.5 - 0.15, 0, -0.9)))
    # CANNONS POKING THROUGH THE GUNPORTS - and at the height the GUNS FIRE
    # FROM, which is not where they used to be.
    #
    # They were placed at section_point(t, 0.62) + 0.55, which is 37 to 49 cm
    # above the waterline: near enough ON it. The C++ fires from GGunPortZ = 280,
    # the deck plus about seventy centimetres, because the guns stand on the deck
    # and fire through ports cut in the bulwark. So the barrels sat two and a
    # third metres BELOW the muzzle flashes and smoke that come out of them, and
    # the comment beside GGunPortsX in ShipPawn.cpp - "taken from the same
    # positions the barrels were modelled at in Blender" - was true of X only.
    #
    # The gap was 77 cm while GGunPortZ was 120 and nobody saw it; raising it to
    # 280 on 18.09 to put the muzzles above the sea widened it to 235 and the
    # muzzle flash made it obvious. A number shared by two files and written down
    # in both is a number that drifts: this one now names the other.
    GUNPORT_Z = 2.80    # metres; ShipPawn.cpp GGunPortZ = 280 cm
    for side in (1, -1):
        for k in range(4):
            x = -6.0 + k * 3.6
            t = (x / LENGTH) + 0.5
            # The half width at the DECK EDGE, which is where the bulwark stands
            # and therefore where a port is cut. section_point tops out at the
            # gunwale, so it cannot be asked for a height inside the rail.
            y, _ = section_point(t, 1.0)
            parts.append(cylinder("Cannon%s%d" % ("S" if side > 0 else "P", k),
                                  m_metal, 0.19, 2.3,
                                  (x, side * (y + 0.35), GUNPORT_Z),
                                  rotation=(math.radians(90), 0, 0),
                                  verts=10, taper=0.8))
    return parts


# ------------------------------------------------------------------ shot hull
def build_shot_hull():
    """The hull as a cannonball meets it: the lofted skin, CLOSED, and nothing
    else - no masts, no cannon, no cordage. Exported to its own FBX and used
    with "complex collision as simple", so the collision IS these triangles.

    Until 25.09 a ball stopped on ShipPawn's collision BOX, 1550 x 520 x 350 cm,
    and the timber sat 1.7 to 3.4 m behind that face on average, 7 m at the
    ends (tools/probe_hull_hits.py).

    Why the skin and not convex pieces: the first version of this function
    lofted six convex slabs named UCX_* for the importer to take as simple
    collision, and the importer silently dropped them - the asset came back
    with convex_elems=0, which the game would have read as "0 hits", not as
    "no collision". A trimesh needs no import convention at all: one flag on
    the asset, set and read back by Scripts/hull_collision.py.

    CLOSED matters. A sweep against a one-sided surface only stops at faces it
    meets from the front, so an open top would let a plunging ball fall into
    the hull and out through the far side unseen. Transom, bow cap and a deck
    lid at the gunwale close it; the lid is not planar (that is the sheer) and
    Blender triangulates it on export, which is all collision needs."""
    m = new_mat("M_ShotHull", (0.5, 0.5, 0.5, 1), 0.9)
    bm = bmesh.new()
    grid_s, grid_p = [], []
    for i in range(NS + 1):
        t = i / NS
        x = station_x(t)
        cs, cp = [], []
        for j in range(NR + 1):
            y, z = section_point(t, j / NR)
            cs.append(bm.verts.new((x, y, z)))
            cp.append(bm.verts.new((x, -y, z)))
        grid_s.append(cs)
        grid_p.append(cp)
    bm.verts.ensure_lookup_table()
    for i in range(NS):
        for j in range(NR):
            bm.faces.new((grid_s[i][j], grid_s[i + 1][j],
                          grid_s[i + 1][j + 1], grid_s[i][j + 1]))
            bm.faces.new((grid_p[i][j], grid_p[i][j + 1],
                          grid_p[i + 1][j + 1], grid_p[i + 1][j]))
    # keel seam, as the visual hull
    for i in range(NS):
        bm.faces.new((grid_s[i][0], grid_p[i][0],
                      grid_p[i + 1][0], grid_s[i + 1][0]))
    # transom and bow cap
    stern_ring = ([grid_s[0][j] for j in range(NR + 1)]
                  + [grid_p[0][j] for j in range(NR, -1, -1)])
    bm.faces.new(stern_ring[::-1])
    bow_ring = ([grid_s[NS][j] for j in range(NR + 1)]
                + [grid_p[NS][j] for j in range(NR, -1, -1)])
    bm.faces.new(bow_ring)
    # THE BULWARK, or a ball between the deck and the rail sails over the
    # hull. Measured on the first version, which closed at the gunwale: 37 hits
    # where the box had 41, the four missing ones all at bulwark height - and
    # that is exactly the band the gun ports are cut in. Same wall as the
    # visual rail (build_hull): from the gunwale up BULWARK, drawn in 1.5%.
    top_s, top_p = [], []
    for i in range(NS + 1):
        t = i / NS
        x = station_x(t)
        y, z = section_point(t, 1.0)
        top_s.append(bm.verts.new((x, y * 0.985, z + BULWARK)))
        top_p.append(bm.verts.new((x, -y * 0.985, z + BULWARK)))
    bm.verts.ensure_lookup_table()
    for i in range(NS):
        bm.faces.new((grid_s[i][NR], grid_s[i + 1][NR], top_s[i + 1], top_s[i]))
        bm.faces.new((grid_p[i][NR], top_p[i], top_p[i + 1], grid_p[i + 1][NR]))
    # close the bulwark's ends against the transom and the bow cap
    bm.faces.new((grid_s[0][NR], top_s[0], top_p[0], grid_p[0][NR]))
    bm.faces.new((grid_s[NS][NR], grid_p[NS][NR], top_p[NS], top_s[NS]))
    # the lid, at the TOP of the bulwark: starboard aft to bow, port bow to aft
    lid = top_s + top_p[::-1]
    bm.faces.new(lid)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj_from_bm(bm, "SM_PirateHull", m, smooth=False)


# ------------------------------------------------------------------ main
def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)

    m_hull = new_mat("M_Hull", (0.055, 0.030, 0.018, 1), 0.72)
    m_deck = new_mat("M_Deck", (0.28, 0.185, 0.105, 1), 0.85)
    m_wood = new_mat("M_Wood", (0.115, 0.068, 0.035, 1), 0.80)
    m_dark = new_mat("M_DarkWood", (0.040, 0.022, 0.013, 1), 0.70)
    m_sail = new_mat("M_Sail", (0.72, 0.66, 0.52, 1), 0.92)
    m_metal = new_mat("M_Iron", (0.045, 0.045, 0.050, 1), 0.42, metallic=0.9)
    m_rope = new_mat("M_Rope", (0.038, 0.030, 0.022, 1), 0.86)

    hull, deck, rail = build_hull(m_hull, m_deck)
    parts = build_rig(m_wood, m_dark, m_sail, m_metal)
    parts.append(build_cordage(m_rope))

    shot_hull = build_shot_hull()

    everything = [hull, deck, rail] + parts
    for o in everything:
        o.select_set(True)
    bpy.context.view_layer.objects.active = hull
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    ship = bpy.context.object
    ship.name = "SM_PirateShip"

    faces = len(ship.data.polygons)
    dims = ship.dimensions
    print("SHIP faces=%d dims=%.1f x %.1f x %.1f m" % (faces, dims.x, dims.y, dims.z))

    # ---- preview render -------------------------------------------------
    w = bpy.data.worlds.new("W")
    bpy.context.scene.world = w
    w.use_nodes = True
    sky = w.node_tree.nodes.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = math.radians(32)
    sky.altitude = 60.0
    sky.dust_density = 0.4
    w.node_tree.links.new(sky.outputs["Color"],
                          w.node_tree.nodes["Background"].inputs["Color"])

    bpy.ops.object.light_add(type="SUN", location=(0, 0, 40))
    sun = bpy.context.object
    sun.data.energy = 3.2
    sun.rotation_euler = (math.radians(58), 0, math.radians(35))

    tgt = bpy.data.objects.new("T", None)
    tgt.location = (0, 0, 9.0)
    bpy.context.collection.objects.link(tgt)
    cd = bpy.data.cameras.new("C")
    cd.lens = 55
    cam = bpy.data.objects.new("Camera", cd)
    cam.location = (50, -58, 24)
    bpy.context.collection.objects.link(cam)
    c = cam.constraints.new("TRACK_TO")
    c.target = tgt
    c.track_axis = "TRACK_NEGATIVE_Z"
    c.up_axis = "UP_Y"
    bpy.context.scene.camera = cam

    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE_NEXT"
    sc.render.resolution_x = 1280
    sc.render.resolution_y = 720
    sc.eevee.taa_render_samples = 64
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "AgX - Punchy"
    sc.render.filepath = os.path.join(OUT, "ship_preview.png")
    bpy.ops.render.render(write_still=True)

    # ---- export ---------------------------------------------------------
    ship.select_set(True)
    bpy.context.view_layer.objects.active = ship
    fbx = os.path.join(OUT, "SM_PirateShip.fbx")
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True,
                             apply_unit_scale=True, global_scale=1.0,
                             apply_scale_options="FBX_SCALE_NONE",
                             object_types={"MESH"}, mesh_smooth_type="FACE",
                             add_leaf_bones=False, bake_space_transform=False)
    # The shot hull, in its own file, so the visual ship's FBX carries exactly
    # what it did and the importer cannot mistake the collision skin for a
    # part of the ship.
    bpy.ops.object.select_all(action="DESELECT")
    shot_hull.select_set(True)
    bpy.context.view_layer.objects.active = shot_hull
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    hull_fbx = os.path.join(OUT, "SM_PirateHull.fbx")
    bpy.ops.export_scene.fbx(filepath=hull_fbx, use_selection=True,
                             apply_unit_scale=True, global_scale=1.0,
                             apply_scale_options="FBX_SCALE_NONE",
                             object_types={"MESH"}, mesh_smooth_type="FACE",
                             add_leaf_bones=False, bake_space_transform=False)
    # CLOSED, counted: every edge of a watertight mesh has exactly two faces.
    # Printed rather than asserted so a change to the loft shows up as a number
    # in the log and not as a silent hole in the collision.
    me = shot_hull.data
    edge_faces = {}
    for poly in me.polygons:
        for k in poly.edge_keys:
            edge_faces[k] = edge_faces.get(k, 0) + 1
    open_edges = sum(1 for n in edge_faces.values() if n != 2)
    print("SHOT_HULL faces=%d open_edges=%d" % (len(me.polygons), open_edges))
    bpy.ops.object.select_all(action="DESELECT")
    ship.select_set(True)
    bpy.context.view_layer.objects.active = ship

    glb = os.path.join(OUT, "SM_PirateShip.glb")
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB",
                              use_selection=True, export_apply=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "ship.blend"))
    print("SHIP_DONE fbx=%s" % fbx)


main()
